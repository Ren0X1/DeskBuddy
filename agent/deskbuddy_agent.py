"""DeskBuddy agent - streams PC stats, now-playing info (with album art) and
audio levels to the DeskBuddy over Wi-Fi (UDP), and runs the media buttons
pressed on it.

Protocol (UDP, PC:4211 <-> DeskBuddy:4210):
  PC -> device   {"t":"stats",...}  {"t":"media",...}  {"t":"vu","l":..,"r":..}
                 b"AR" + id(u16 LE) + chunk + count + RGB565 LE pixels  (album art)
  device -> PC   {"t":"hello"}  {"t":"cmd","c":"play|next|prev|volup|voldn|mute"}
"""
import asyncio
import colorsys
import ctypes
import datetime as dt
import io
import json
import logging
import os
import queue
import socket
import struct
import subprocess
import threading
import time
import unicodedata
from pathlib import Path

import psutil
from PIL import Image

PORT_DEVICE = 4210
PORT_AGENT = 4211
ART_SIZE = 80          # must match firmware config.h
ART_CHUNK = 1280
HOSTNAME = socket.gethostname()
NO_WINDOW = 0x08000000

logging.basicConfig(
    filename=Path(__file__).with_name("agent.log"), level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("deskbuddy")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
try:
    sock.bind(("", PORT_AGENT))  # one socket for send + receive -> replies pass the firewall
except OSError:
    log.info("another agent is already running, exiting")
    raise SystemExit(0)

STARTUP_LNK = Path(os.environ.get("APPDATA", "")) / \
    "Microsoft/Windows/Start Menu/Programs/Startup/DeskBuddy Agent.lnk"
children: list[subprocess.Popen] = []

device_ip: str | None = None
gpu = {"gpu": 0.0, "gt": 0.0, "vram": 0.0}
media = {"has": False, "st": False, "ti": "", "ar": "", "app": "", "pos": 0.0, "dur": 0.0,
         "aid": 0, "col": 0}
audio = {"vol": 0, "mute": False}
art = {"id": 0, "data": b"", "dirty": False}
commands: "queue.Queue[str]" = queue.Queue()
media_cmds: "queue.Queue[str]" = queue.Queue()


def send(obj: dict) -> None:
    send_raw(json.dumps(obj, separators=(",", ":")).encode())


def send_raw(data: bytes) -> None:
    try:
        sock.sendto(data, (device_ip or "255.255.255.255", PORT_DEVICE))
    except OSError:
        pass


def latin1_text(s: str, limit: int) -> str:
    """The display fonts cover Latin-1 (á é ñ ü...); fold anything else to ASCII."""
    out = []
    for ch in s or "":
        if ord(ch) < 0x100:
            out.append(ch)
        else:
            out.append(unicodedata.normalize("NFKD", ch).encode("ascii", "ignore").decode())
    return "".join(out).strip()[:limit]


def idle_seconds() -> int:
    class LASTINPUTINFO(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_uint), ("dwTime", ctypes.c_uint)]
    lii = LASTINPUTINFO(ctypes.sizeof(LASTINPUTINFO), 0)
    if not ctypes.windll.user32.GetLastInputInfo(ctypes.byref(lii)):
        return 0
    return max(0, (ctypes.windll.kernel32.GetTickCount() - lii.dwTime) // 1000)


# ------------------------------------------------------------------ GPU (NVIDIA)
def gpu_reader() -> None:
    cmd = ["nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total",
           "--format=csv,noheader,nounits", "-lms", "1000"]
    while True:
        try:
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, creationflags=NO_WINDOW)
            children.append(p)
            for line in p.stdout:
                parts = [x.strip() for x in line.split(",")]
                if len(parts) == 4:
                    u, t, mu, mt = map(float, parts)
                    gpu.update(gpu=u, gt=t, vram=round(100.0 * mu / mt, 1) if mt else 0.0)
        except Exception as e:  # no NVIDIA GPU / driver hiccup
            log.warning("gpu: %s", e)
        time.sleep(30)


# ------------------------------------------------------------------ audio: VU + volume
def audio_worker() -> None:
    import comtypes
    from comtypes import CLSCTX_ALL, COMMETHOD, GUID, HRESULT, POINTER, IUnknown, cast
    from pycaw.pycaw import AudioUtilities

    class IAudioMeterInformation(IUnknown):  # full vtable (pycaw only maps GetPeakValue)
        _iid_ = GUID("{C02216F6-8C67-4B5B-9D00-D008E73E0064}")
        _methods_ = (
            COMMETHOD([], HRESULT, "GetPeakValue", (["out"], POINTER(ctypes.c_float), "peak")),
            COMMETHOD([], HRESULT, "GetMeteringChannelCount",
                      (["out"], POINTER(ctypes.c_uint), "count")),
            COMMETHOD([], HRESULT, "GetChannelsPeakValues", (["in"], ctypes.c_uint, "count"),
                      (["in"], POINTER(ctypes.c_float), "values")),
            COMMETHOD([], HRESULT, "QueryHardwareSupport",
                      (["out"], POINTER(ctypes.c_uint), "mask")),
        )

    comtypes.CoInitialize()
    meter = endpoint = None
    last_dev = 0.0
    while True:
        try:
            if meter is None or time.time() - last_dev > 10:  # follow default-device changes
                spk = AudioUtilities.GetSpeakers()
                meter = cast(spk._dev.Activate(IAudioMeterInformation._iid_, CLSCTX_ALL, None),
                             POINTER(IAudioMeterInformation))
                endpoint = spk.EndpointVolume
                last_dev = time.time()

            while not commands.empty():
                c = commands.get_nowait()
                v = endpoint.GetMasterVolumeLevelScalar()
                if c == "volup":
                    endpoint.SetMasterVolumeLevelScalar(min(1.0, v + 0.05), None)
                elif c == "voldn":
                    endpoint.SetMasterVolumeLevelScalar(max(0.0, v - 0.05), None)
                elif c == "mute":
                    endpoint.SetMute(not endpoint.GetMute(), None)

            audio["vol"] = round(endpoint.GetMasterVolumeLevelScalar() * 100)
            audio["mute"] = bool(endpoint.GetMute())
            n = meter.GetMeteringChannelCount()
            if n >= 2:
                arr = (ctypes.c_float * n)()
                meter.GetChannelsPeakValues(n, arr)
                l, r = arr[0], arr[1]
            else:
                l = r = meter.GetPeakValue()
            if l > 0.001 or r > 0.001:
                send({"t": "vu", "l": round(l ** 0.6, 3), "r": round(r ** 0.6, 3)})
        except Exception as e:
            log.warning("audio: %s", e)
            meter = None
            time.sleep(2)
        time.sleep(0.04)


# ------------------------------------------------------------------ album art
def pick_accent(im: Image.Image) -> int:
    """Most vivid common color of the cover, brightened to read well on black."""
    small = im.resize((24, 24))
    best, best_score = None, 0.0
    for count, (r, g, b) in small.getcolors(24 * 24) or []:
        h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        if v < 0.2:
            continue
        score = count * (s ** 1.5) * v
        if score > best_score:
            best, best_score = (h, s, v), score
    if best is None or best[1] < 0.18:
        return 0  # greyscale cover -> keep the default accent
    h, s, v = best
    r, g, b = colorsys.hsv_to_rgb(h, min(max(s, 0.45), 0.85), max(v, 0.9))
    return (int(r * 255) << 16) | (int(g * 255) << 8) | int(b * 255)


def encode_art(raw: bytes) -> tuple[bytes, int]:
    im = Image.open(io.BytesIO(raw)).convert("RGB")
    w, h = im.size
    s = min(w, h)
    im = im.crop(((w - s) // 2, (h - s) // 2, (w + s) // 2, (h + s) // 2))
    im = im.resize((ART_SIZE, ART_SIZE), Image.LANCZOS)
    px = bytearray()
    for r, g, b in im.getdata():
        px += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(px), pick_accent(im)


def send_art() -> None:
    data, aid = art["data"], art["id"]
    if not data:
        return
    chunks = [data[i:i + ART_CHUNK] for i in range(0, len(data), ART_CHUNK)]
    for i, c in enumerate(chunks):
        send_raw(b"AR" + struct.pack("<HBB", aid, i, len(chunks)) + c)
        time.sleep(0.003)


async def read_thumbnail(ref) -> bytes | None:
    from winrt.windows.storage.streams import Buffer, DataReader, InputStreamOptions
    stream = await ref.open_read_async()
    size = int(stream.size)
    if not size:
        return None
    buf = await stream.read_async(Buffer(size), size, InputStreamOptions.READ_AHEAD)
    try:
        return bytes(memoryview(buf))
    except TypeError:
        reader = DataReader.from_buffer(buf)
        out = bytearray(buf.length)
        reader.read_bytes(out)
        return bytes(out)


# ------------------------------------------------------------------ now playing (Windows SMTC)
VK = {"play": 0xB3, "next": 0xB0, "prev": 0xB1}


def press_media_key(vk: int) -> None:
    ctypes.windll.user32.keybd_event(vk, 0, 0, 0)
    ctypes.windll.user32.keybd_event(vk, 0, 2, 0)


def app_name(aumid: str) -> str:
    name = aumid.split("!")[-1].split("\\")[-1].removesuffix(".exe")
    known = {"spotify": "Spotify", "chrome": "Chrome", "msedge": "Edge", "firefox": "Firefox",
             "zunemusic": "Media Player", "microsoft.media.player": "Media Player"}
    low = aumid.lower()
    for k, v in known.items():
        if k in low:
            return v
    return name.split(".")[-1][:20]


async def media_loop() -> None:
    from winrt.windows.media.control import \
        GlobalSystemMediaTransportControlsSessionManager as Manager

    mgr = await Manager.request_async()
    track_key, art_tries = None, 0
    while True:
        try:
            s = mgr.get_current_session()
            while not media_cmds.empty():
                c = media_cmds.get_nowait()
                ok = False
                if s:
                    fn = {"play": s.try_toggle_play_pause_async, "next": s.try_skip_next_async,
                          "prev": s.try_skip_previous_async}[c]
                    ok = await fn()
                if not ok:
                    press_media_key(VK[c])
                await asyncio.sleep(0.25)
                s = mgr.get_current_session()

            if not s:
                media.update(has=False, st=False, ti="", ar="", app="", pos=0.0, dur=0.0, aid=0)
                track_key = None
                await asyncio.sleep(0.5)
                continue

            props = await s.try_get_media_properties_async()
            info = s.get_playback_info()
            tl = s.get_timeline_properties()
            playing = int(info.playback_status) == 4
            pos = tl.position.total_seconds()
            if playing and tl.last_updated_time.year > 2000:
                pos += (dt.datetime.now(dt.timezone.utc) - tl.last_updated_time).total_seconds()
            dur = tl.end_time.total_seconds()
            media.update(has=True, st=playing, ti=latin1_text(props.title, 90),
                         ar=latin1_text(props.artist, 60), app=app_name(s.source_app_user_model_id),
                         pos=round(max(0.0, min(pos, dur or pos)), 1), dur=round(dur, 1))

            key = (props.title, props.artist)
            if key != track_key:
                track_key, art_tries = key, 0
                media.update(aid=0, col=0)
            # covers often arrive a moment after the title, so retry a few times
            if media["aid"] == 0 and art_tries < 6 and props.thumbnail:
                art_tries += 1
                try:
                    raw = await read_thumbnail(props.thumbnail)
                    if raw:
                        px, col = encode_art(raw)
                        new_id = art["id"] % 65535 + 1
                        art.update(id=new_id, data=px, dirty=True)
                        media.update(aid=new_id, col=col)
                        log.info("art for %s - %s (#%06x)", props.artist, props.title, col)
                except Exception as e:
                    log.warning("art: %s", e)
        except Exception as e:
            log.warning("media: %s", e)
        await asyncio.sleep(0.5)


def media_thread() -> None:
    while True:
        try:
            asyncio.run(media_loop())
        except Exception as e:
            log.warning("media loop crashed: %s", e)
            time.sleep(5)


# ------------------------------------------------------------------ network
def receiver() -> None:
    global device_ip
    while True:
        try:
            data, addr = sock.recvfrom(512)
            msg = json.loads(data)
        except (OSError, ValueError):
            continue
        if msg.get("t") in ("hello", "cmd") and addr[0] != device_ip \
                and not addr[0].startswith("127."):
            device_ip = addr[0]
            log.info("DeskBuddy at %s", device_ip)
            art["dirty"] = True  # (re)connected -> resend the cover
        if msg.get("t") == "cmd":
            c = msg.get("c")
            log.info("button: %s", c)
            if c in VK:
                media_cmds.put(c)
            elif c in ("volup", "voldn", "mute"):
                commands.put(c)


def resolve_mdns() -> None:
    global device_ip
    while True:
        if device_ip is None:
            try:
                device_ip = socket.gethostbyname("deskbuddy.local")
                log.info("DeskBuddy resolved via mDNS: %s", device_ip)
            except OSError:
                pass
        time.sleep(10)


def sender() -> None:
    psutil.cpu_percent(None)
    tick = 0
    while True:
        time.sleep(0.5)
        tick += 1
        if art["dirty"] or (media["aid"] and tick % 10 == 0):  # resend every 5 s (UDP may drop)
            art["dirty"] = False
            send_art()
        send({"t": "media", **media, **audio})
        if tick % 2 == 0:
            send({"t": "stats", "host": HOSTNAME, "idle": idle_seconds(),
                  "cpu": round(psutil.cpu_percent(None), 1),
                  "ram": round(psutil.virtual_memory().percent, 1), **gpu})


# ------------------------------------------------------------------ tray icon
def tray_image(connected: bool) -> Image.Image:
    """A little round DeskBuddy: dark face, accent ring, two eyes."""
    from PIL import ImageDraw
    s = 64
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    ring = (90, 200, 250) if connected else (110, 116, 128)
    d.ellipse((2, 2, s - 3, s - 3), fill=(12, 14, 18), outline=ring, width=5)
    for x in (20, 36):
        d.rounded_rectangle((x, 21, x + 9, 41), radius=4, fill=ring)
    return im


def run_tray() -> None:
    import webbrowser
    import pystray

    def status_text(_item) -> str:
        return f"Conectado a {device_ip}" if device_ip else "Buscando DeskBuddy..."

    def open_web(_icon, _item) -> None:
        webbrowser.open(f"http://{device_ip or 'deskbuddy.local'}/")

    def open_log(_icon, _item) -> None:
        os.startfile(Path(__file__).with_name("agent.log"))

    def autostart_on(_item) -> bool:
        return STARTUP_LNK.exists()

    def toggle_autostart(_icon, _item) -> None:
        if STARTUP_LNK.exists():
            STARTUP_LNK.unlink()
        else:
            script = Path(__file__).with_name("install_autostart.ps1")
            subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-File", str(script),
                            "-NoStart"], creationflags=NO_WINDOW)

    def quit_app(icon, _item) -> None:
        log.info("quit from tray")
        for p in children:
            p.kill()
        icon.stop()

    icon = pystray.Icon("DeskBuddy", tray_image(False), "DeskBuddy", menu=pystray.Menu(
        pystray.MenuItem(status_text, None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Abrir panel web", open_web, default=True),
        pystray.MenuItem("Ver log", open_log),
        pystray.MenuItem("Iniciar con Windows", toggle_autostart, checked=autostart_on),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Salir", quit_app),
    ))

    def watch(icon) -> None:  # keep icon + tooltip in sync with the connection
        icon.visible = True
        shown = None
        while True:
            if device_ip != shown:
                shown = device_ip
                icon.icon = tray_image(bool(shown))
                icon.title = f"DeskBuddy - {status_text(None)}"
                icon.update_menu()
            time.sleep(2)

    icon.run(setup=lambda i: threading.Thread(target=watch, args=(i,), daemon=True).start())


def main() -> None:
    log.info("agent starting on %s", HOSTNAME)
    for fn in (gpu_reader, audio_worker, media_thread, receiver, resolve_mdns, sender):
        threading.Thread(target=fn, daemon=True, name=fn.__name__).start()
    try:
        run_tray()  # blocks until "Salir"
    except Exception as e:  # no tray available -> keep running headless
        log.warning("tray: %s", e)
        threading.Event().wait()


if __name__ == "__main__":
    main()
