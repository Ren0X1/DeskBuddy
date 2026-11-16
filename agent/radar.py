"""Plane radar for DeskBuddy.

Two modes, both configurable in config.json:
  * radar     - aircraft within `radius_km` of your home, refreshed every few seconds
  * spotlight - worldwide emergencies (squawk 7700/7600/7500) and notable aircraft
                (Air Force One, Beluga, Dreamlifter...) shown full-screen for a minute
                over a world map with their route

Open data only: adsb.lol (live ADS-B, ODbL), hexdb.io (routes/airports),
Natural Earth (map, public domain).
"""
import json
import math
import random
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
CONFIG = HERE / "config.json"
DATA = HERE / "data"
UA = {"User-Agent": "DeskBuddy/2.0 (+https://github.com/Ren0X1/DeskBuddy)"}
ADSB = "https://api.adsb.lol/v2"
CHUNK = 1280  # must match ART_CHUNK in firmware config.h

DEFAULT_CONFIG = {
    # placeholder: set yours in config.json (also used for the weather on the clock)
    "home": {"lat": 40.4168, "lon": -3.7038, "city": ""},
    "radar": {"enabled": True, "radius_km": 15, "interval_s": 5, "auto_show": True},
    "spotlight": {
        "enabled": True,
        "interval_s": 120,
        "show_seconds": 60,
        "repeat_minutes": 120,
        "squawks": ["7700", "7600", "7500"],
        "watch_callsigns": ["AF1", "AF2", "EXEC1F", "SAM"],
        "watch_types": ["A337", "A3ST", "BLCF", "A124", "B52", "U2"],
    },
}

SQUAWK_LABEL = {"7700": "EMERGENCIA", "7600": "SIN RADIO", "7500": "SECUESTRO"}

# 16-colour phosphor palette for the map (index -> RGB)
PALETTE = [
    (0, 6, 4),        # 0 sea
    (6, 36, 22),      # 1 land
    (16, 92, 54),     # 2 coast
    (8, 26, 18),      # 3 graticule
    (57, 255, 136),   # 4 route flown
    (28, 120, 72),    # 5 route ahead
    (190, 255, 220),  # 6 airport
    (12, 58, 36),     # 7 borders
] + [(0, 0, 0)] * 8


def load_config() -> dict:
    cfg = json.loads(json.dumps(DEFAULT_CONFIG))
    if CONFIG.exists():
        user = json.loads(CONFIG.read_text(encoding="utf-8"))
        for k, v in user.items():
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                cfg[k].update(v)
            else:
                cfg[k] = v
    else:
        save_config(cfg)
    return cfg


def save_config(cfg: dict) -> None:
    CONFIG.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")


def http_json(url: str, timeout: float = 15):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def thousands(v: int) -> str:
    return f"{v:,}".replace(",", ".")


def clean_model(name: str) -> str:
    """'Boeing 737NG 8AS/W' -> 'Boeing 737-800', 'Airbus A320 214' -> 'Airbus A320'."""
    import re
    name = re.sub(r"/W\b", "", name or "").strip()
    name = re.sub(r"\b(7[0-9]7)NG (\d)[A-Z0-9]{2}\b", r"\1-\g<2>00", name)   # Boeing customer codes
    name = re.sub(r"\b(7[0-9]7) (\d)[A-Z0-9]{2}\b", r"\1-\g<2>00", name)
    name = re.sub(r"\b(A3[0-9]{2}(?:neo|NEO)?)[ -]\d{3}[A-Z]*\b", r"\1", name)  # Airbus engine/series
    return re.sub(r"\s+", " ", name).strip()


def callsign_of(a: dict) -> str:
    """Flight callsign, or registration/hex when the transponder sends junk like '00000000'."""
    cs = (a.get("flight") or "").strip()
    if not cs or set(cs) <= {"0", "?"}:
        cs = (a.get("r") or a.get("hex") or "?").strip().upper()
    return cs


# ------------------------------------------------------------------ routes (hexdb.io)
class Routes:
    def __init__(self, log):
        self.log = log
        self.routes: dict[str, tuple] = {}
        self.airports: dict[str, dict | None] = {}
        self.models: dict[str, str] = {}

    _ourairports: dict | None = None

    @classmethod
    def _cities(cls) -> dict:
        """ICAO -> (city, lat, lon, iata) from OurAirports (public domain), downloaded once."""
        if cls._ourairports is None:
            import csv
            import io
            import re
            path = DATA / "airports.csv"
            try:
                if not path.exists():
                    DATA.mkdir(exist_ok=True)
                    req = urllib.request.Request(
                        "https://davidmegginson.github.io/ourairports-data/airports.csv", headers=UA)
                    path.write_bytes(urllib.request.urlopen(req, timeout=90).read())
                rows = csv.DictReader(io.StringIO(path.read_text(encoding="utf-8")))
                # "Oslo (Gardermoen)" -> "Oslo"
                cls._ourairports = {r["ident"]: (re.sub(r"\s*\(.*?\)", "", r["municipality"]).strip(),
                                                 float(r["latitude_deg"]),
                                                 float(r["longitude_deg"]), r["iata_code"])
                                    for r in rows if r["type"] != "closed"}
            except Exception:
                cls._ourairports = {}
        return cls._ourairports

    def airport(self, icao: str) -> dict | None:
        if icao not in self.airports:
            hit = self._cities().get(icao)
            if hit:
                city, lat, lon, iata = hit
                self.airports[icao] = {"icao": icao, "iata": iata or icao, "city": city,
                                       "lat": lat, "lon": lon}
            else:
                try:
                    a = http_json(f"https://hexdb.io/api/v1/airport/icao/{icao}", 10)
                    self.airports[icao] = {"icao": icao, "iata": a.get("iata") or icao, "city": "",
                                           "lat": a["latitude"], "lon": a["longitude"]}
                except Exception:
                    self.airports[icao] = None
        return self.airports[icao]

    def model(self, hexid: str, budget: list | None = None) -> str:
        """Full aircraft model ('Boeing 737 MAX 8') from the ICAO hex. Cached forever."""
        h = hexid.strip().upper()
        if h in self.models:
            return self.models[h]
        if budget is not None:
            if budget[0] <= 0:
                return ""
            budget[0] -= 1
        name = ""
        try:
            a = http_json(f"https://hexdb.io/api/v1/aircraft/{h}", 10)
            name = (a.get("Type") or "").strip()
            maker = (a.get("Manufacturer") or "").strip().title()
            if maker and name and not name.lower().startswith(maker.split()[0].lower()):
                name = f"{maker.split()[0]} {name}"
        except Exception:
            pass
        self.models[h] = name = clean_model(name)
        return name

    def lookup(self, callsign: str, budget: list | None = None):
        """-> (origin, destination) airport dicts, or (None, None). Cached for 6 h."""
        cs = callsign.strip().upper()
        hit = self.routes.get(cs)
        if hit and time.time() - hit[2] < 6 * 3600:
            return hit[0], hit[1]
        if budget is not None:  # limit slow lookups per radar refresh
            if budget[0] <= 0:
                return None, None
            budget[0] -= 1
        o = d = None
        try:
            r = http_json(f"https://hexdb.io/api/v1/route/icao/{cs}", 10).get("route", "")
            parts = r.split("-")
            if len(parts) >= 2:
                o, d = self.airport(parts[0]), self.airport(parts[-1])
        except Exception:
            pass
        self.routes[cs] = (o, d, time.time())
        return o, d

    @staticmethod
    def label(a: dict | None) -> str:
        """'LEMG Málaga' (ICAO + city)"""
        if not a:
            return ""
        return f"{a['icao']} {a['city']}".strip() if a.get("city") else a["icao"]

    def text(self, o, d) -> str:
        """'LEMG Málaga → EYKA Kaunas'"""
        return f"{self.label(o)} → {self.label(d)}" if o and d else ""


# ------------------------------------------------------------------ world map
class WorldMap:
    SOURCES = {
        "land": "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/"
                "ne_50m_land.geojson",
        "borders": "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/"
                   "ne_50m_admin_0_boundary_lines_land.geojson",
    }

    def __init__(self):
        self.layers: dict[str, list] = {}

    def _load(self, name: str) -> list:
        if name not in self.layers:
            DATA.mkdir(exist_ok=True)
            path = DATA / f"{name}.geojson"
            if not path.exists():
                req = urllib.request.Request(self.SOURCES[name], headers=UA)
                path.write_bytes(urllib.request.urlopen(req, timeout=60).read())
            shapes = []
            for f in json.loads(path.read_text(encoding="utf-8"))["features"]:
                g = f["geometry"]
                if g["type"] == "Polygon":
                    shapes.append(g["coordinates"][0])
                elif g["type"] == "MultiPolygon":
                    shapes += [p[0] for p in g["coordinates"]]
                elif g["type"] == "LineString":
                    shapes.append(g["coordinates"])
                elif g["type"] == "MultiLineString":
                    shapes += g["coordinates"]
            self.layers[name] = shapes
        return self.layers[name]

    def render(self, cur, heading, origin, dest, trail):
        """240x240 LVGL INDEXED_4BIT image + pixel positions of plane/origin/dest."""
        ref = cur[1]

        def unwrap(lon):
            return ref + ((lon - ref + 180) % 360 - 180)

        pts = [cur] + [(a["lat"], a["lon"]) for a in (origin, dest) if a] + list(trail)
        lat0 = sum(p[0] for p in pts) / len(pts)
        k = math.cos(math.radians(max(-70.0, min(70.0, lat0))))
        xs = [unwrap(p[1]) * k for p in pts]
        ys = [p[0] for p in pts]
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        span = max(max(xs) - min(xs), max(ys) - min(ys), 10.0) * 1.35
        scale = 196 / span

        def px(lat, lon, off=0.0):
            return (120 + ((lon + off) * k - cx) * scale, 120 - (lat - cy) * scale)

        img = Image.new("P", (240, 240), 0)
        flat = [c for rgb in PALETTE for c in rgb]
        img.putpalette(flat)
        d = ImageDraw.Draw(img)

        step = 5 if span < 40 else 10 if span < 100 else 30
        for g in range(-180, 181, step):
            d.line([px(-85, g + round(ref / 360) * 360), px(85, g + round(ref / 360) * 360)], fill=3)
        for g in range(-80, 81, step):
            d.line([px(g, ref - 200), px(g, ref + 200)], fill=3)

        offs = (-360.0, 0.0, 360.0)
        for ring in self._load("land"):
            for off in offs:
                poly = [px(lat, lon, off) for lon, lat in ring]
                xs_ = [p[0] for p in poly]
                if max(xs_) < -50 or min(xs_) > 290:
                    continue
                d.polygon(poly, fill=1, outline=2)
        for line in self._load("borders"):
            for off in offs:
                pl = [px(lat, lon, off) for lon, lat in line]
                if max(p[0] for p in pl) < -50 or min(p[0] for p in pl) > 290:
                    continue
                d.line(pl, fill=7)

        def upx(lat, lon):
            return px(lat, unwrap(lon))

        p_cur = upx(*cur)
        flown = ([upx(origin["lat"], origin["lon"])] if origin else []) + \
            [upx(*t) for t in trail] + [p_cur]
        if len(flown) > 1:
            d.line(flown, fill=4, width=2)
        if dest:
            p_d = upx(dest["lat"], dest["lon"])
            n = max(2, int(math.dist(p_cur, p_d) / 6))
            for i in range(0, n, 2):  # dashed
                a = [p_cur[j] + (p_d[j] - p_cur[j]) * i / n for j in (0, 1)]
                b = [p_cur[j] + (p_d[j] - p_cur[j]) * (i + 1) / n for j in (0, 1)]
                d.line([tuple(a), tuple(b)], fill=5, width=2)
        elif heading is not None:  # no route: hint the direction of travel
            h = math.radians(heading)
            d.line([p_cur, (p_cur[0] + 40 * math.sin(h), p_cur[1] - 40 * math.cos(h))], fill=5, width=2)
        marks = {}
        for name, a in (("o", origin), ("d", dest)):
            if a:
                x, y = upx(a["lat"], a["lon"])
                d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=6)
                marks[name] = (int(x), int(y))

        # LVGL indexed 4 bpp: 16 x (B,G,R,A) palette, then 2 px per byte, high nibble first
        out = bytearray()
        for r, g, b in PALETTE:
            out += bytes((b, g, r, 255))
        data = img.tobytes()
        for i in range(0, len(data), 2):
            out.append((data[i] << 4) | data[i + 1])
        return bytes(out), (int(p_cur[0]), int(p_cur[1])), marks


# ------------------------------------------------------------------ radar engine
class Radar:
    def __init__(self, send, send_raw, log):
        self.send, self.send_raw, self.log = send, send_raw, log
        self.cfg = load_config()
        self.routes = Routes(log)
        self.map = WorldMap()
        self.shown: dict[str, float] = {}
        self.trails: dict[str, list] = {}
        self.alert_id = random.randint(1, 60000)  # survives agent restarts without id clashes
        self.lock = threading.Lock()

    def home(self) -> dict:
        h = self.cfg["home"]
        return {"lat": h["lat"], "lon": h["lon"], "city": h.get("city", "")}

    # -- config helpers for the tray menu
    def enabled(self, mode: str) -> bool:
        return bool(self.cfg[mode]["enabled"])

    def toggle(self, mode: str) -> None:
        with self.lock:
            self.cfg = load_config()
            self.cfg[mode]["enabled"] = not self.cfg[mode]["enabled"]
            save_config(self.cfg)

    def reload(self) -> None:
        with self.lock:
            self.cfg = load_config()

    def start(self) -> None:
        for fn in (self.radar_loop, self.spotlight_loop):
            threading.Thread(target=fn, daemon=True, name=fn.__name__).start()

    # -- nearby traffic
    def radar_loop(self) -> None:
        last = None
        while True:
            rc = self.cfg["radar"]
            wait = max(2, rc.get("interval_s", 5))
            try:
                if rc["enabled"]:
                    last = self.scan_nearby()
                else:
                    last = {"t": "radar", "on": 0, "r": rc["radius_km"], "ac": []}
                self.send(last)
            except Exception as e:  # adsb.lol hiccup / rate limit: back off, keep the screen alive
                self.log.warning("radar: %s", e)
                if last:
                    self.send(last)
                wait = 30
            time.sleep(wait)

    def scan_nearby(self) -> dict:
        home, rc = self.cfg["home"], self.cfg["radar"]
        lat0, lon0, radius = home["lat"], home["lon"], float(rc["radius_km"])
        data = http_json(f"{ADSB}/point/{lat0}/{lon0}/{radius / 1.852 * 1.1:.1f}")
        planes = []
        for a in data.get("ac", []):
            if "lat" not in a or a.get("alt_baro") == "ground":
                continue
            x = (a["lon"] - lon0) * 111.32 * math.cos(math.radians(lat0))
            y = (a["lat"] - lat0) * 110.57
            dist = math.hypot(x, y)
            if dist <= radius:
                planes.append((dist, x, y, a))
        planes.sort(key=lambda p: p[0])
        budget, mbudget = [3], [3]
        out = []
        for dist, x, y, a in planes[:10]:
            cs = callsign_of(a)
            o, dd = self.routes.lookup(cs, budget) if cs == (a.get("flight") or "").strip() else (None, None)
            model = (clean_model(a.get("desc") or "")
                     or self.routes.model(a.get("hex", ""), mbudget) or a.get("t") or "")
            alt = a.get("alt_baro") if isinstance(a.get("alt_baro"), (int, float)) else a.get("alt_geom", 0)
            out.append({"c": cs[:9], "ty": (a.get("t") or "")[:7], "x": round(x, 2), "y": round(y, 2),
                        "al": int((alt or 0) * 0.3048), "s": int((a.get("gs") or 0) * 1.852),
                        "h": int(a.get("track") or a.get("true_heading") or 0),
                        "ro": self.routes.label(o)[:31], "rd": self.routes.label(dd)[:31],
                        "md": model[:31]})
        return {"t": "radar", "on": 1, "r": radius, "auto": bool(rc.get("auto_show", True)), "ac": out}

    # -- spotlight
    def spotlight_loop(self) -> None:
        time.sleep(10)
        while True:
            sc = self.cfg["spotlight"]
            try:
                if sc["enabled"]:
                    self.spotlight_once()
            except Exception as e:
                self.log.warning("spotlight: %s", e)
            time.sleep(max(30, sc.get("interval_s", 60)))

    def _query(self, path: str) -> list:
        """adsb.lol is free but rate-limited: space calls out and back off on 429."""
        time.sleep(1.5)
        try:
            return http_json(f"{ADSB}/{path}").get("ac", [])
        except urllib.error.HTTPError as e:
            if e.code == 429:
                self.log.info("adsb.lol rate limit, backing off")
                time.sleep(20)
            return []
        except Exception as e:
            self.log.warning("adsb.lol %s: %s", path, e)
            return []

    def candidates(self) -> list:
        sc = self.cfg["spotlight"]
        found = []
        for sq in sc.get("squawks", []):
            for a in self._query(f"sqk/{sq}"):
                found.append((0, f"{SQUAWK_LABEL.get(sq, 'SQUAWK')} · {sq}", True, a))
        for cs in sc.get("watch_callsigns", []):
            for a in self._query(f"callsign/{cs}"):
                found.append((1, "AERONAVE DESTACADA", False, a))
        for ty in sc.get("watch_types", []):
            for a in self._query(f"type/{ty}"):
                found.append((2, "AERONAVE DESTACADA", False, a))
        return [f for f in found if "lat" in f[3] and f[3].get("alt_baro") != "ground"]

    def spotlight_once(self) -> None:
        sc = self.cfg["spotlight"]
        cands = sorted(self.candidates(), key=lambda c: c[0])
        now = time.time()
        for _, _, _, a in cands:  # remember positions -> flown trail on the map
            tr = self.trails.setdefault(a["hex"], [])
            if not tr or tr[-1] != (a["lat"], a["lon"]):
                tr.append((a["lat"], a["lon"]))
                del tr[:-60]
        for _, label, emergency, a in cands:
            if now - self.shown.get(a["hex"], 0) < sc.get("repeat_minutes", 120) * 60:
                continue
            self.shown[a["hex"]] = now
            self.show(label, emergency, a, sc.get("show_seconds", 60))
            return

    def show(self, label: str, emergency: bool, a: dict, secs: int) -> None:
        cs = callsign_of(a)
        o, d = self.routes.lookup(cs) if cs == (a.get("flight") or "").strip() else (None, None)
        trail = self.trails.get(a["hex"], [])[:-1]
        img, p, marks = self.map.render((a["lat"], a["lon"]), a.get("track"), o, d, trail)
        self.alert_id = self.alert_id % 65000 + 1
        aid = self.alert_id
        chunks = [img[i:i + CHUNK] for i in range(0, len(img), CHUNK)]
        # the ESP32 only queues ~6 UDP packets, so pace the 23 chunks; send twice
        for _ in range(2):
            for i, c in enumerate(chunks):
                self.send_raw(b"MP" + bytes((aid & 255, aid >> 8, i, len(chunks))) + c)
                time.sleep(0.03)
        alt = a.get("alt_baro") if isinstance(a.get("alt_baro"), (int, float)) else 0
        model = clean_model(a.get("desc") or "") or self.routes.model(a["hex"]) or a.get("t") or ""
        if a.get("r"):
            model = f"{model} · {a['r']}" if model else a["r"]
        msg = {"t": "alert", "id": aid, "lbl": label, "c": cs[:11], "m": model[:39],
               "ro": self.routes.label(o)[:39], "rd": self.routes.label(d)[:39],
               "em": emergency, "sec": int(secs),
               "dt": f"{thousands(int(alt * 0.3048))} m · {int((a.get('gs') or 0) * 1.852)} km/h",
               "p": list(p)}
        if "o" in marks:
            msg["o"] = [*marks["o"], o["icao"][:5]]
        if "d" in marks:
            msg["d"] = [*marks["d"], d["icao"][:5]]
        for _ in range(3):
            self.send(msg)
            time.sleep(0.3)
        self.log.info("spotlight: %s %s (%s)", label, cs, self.routes.text(o, d))
