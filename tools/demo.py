"""Show off the radar on a real DeskBuddy without waiting for real traffic.

  1. a plane flies right over your home (plus a helicopter nearby)
  2. a spotlight alert: Air Force One over a real world map with its route

Stop the PC agent first (tray icon -> Salir), otherwise its live data wins.
usage: python tools/demo.py [--host deskbuddy.local] [--shots DIR --port COM3]
"""
import argparse
import json
import logging
import math
import socket
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "agent"))
sys.path.insert(0, str(ROOT / "tools"))
import radar as R  # noqa: E402


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="deskbuddy.local")
    ap.add_argument("--shots", help="save screenshots here (needs USB)")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--flyover", type=int, default=40, help="seconds")
    ap.add_argument("--alert", type=int, default=60, help="seconds")
    a = ap.parse_args()

    dev = (socket.gethostbyname(a.host), 4210)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send = lambda m: sock.sendto(json.dumps(m, separators=(",", ":")).encode(), dev)  # noqa: E731
    send_raw = lambda b: sock.sendto(b, dev)  # noqa: E731
    ser = None
    if a.shots:
        import serial
        from screenshot import grab
        Path(a.shots).mkdir(parents=True, exist_ok=True)
        ser = serial.Serial(a.port, 115200, timeout=6)

    def shot(name: str) -> None:
        if ser:
            grab(ser).save(Path(a.shots) / f"{name}.png")
            print("  📸", name)

    stop = threading.Event()

    def heartbeat() -> None:  # keep "PC online" so screens stay live
        while not stop.is_set():
            send({"t": "stats", "host": socket.gethostname(), "cpu": 9, "ram": 48, "gpu": 3,
                  "gt": 41, "idle": 0})
            time.sleep(1)

    threading.Thread(target=heartbeat, daemon=True).start()
    if ser:
        ser.write(b"tile 0\n")  # start on the clock: the radar should jump in by itself
    time.sleep(2)

    # ---- 1. flyover: SW -> NE straight over home at 10 km, 820 km/h (time-lapsed)
    print("✈️  flyover")
    t0, last_shot = time.time(), 0
    while (el := time.time() - t0) < a.flyover:
        f = el / a.flyover
        d = -17 + 34 * f                       # km along the track
        x, y = d * math.sin(math.radians(45)), d * math.cos(math.radians(45))
        hx, hy = 6.5 * math.cos(el / 9), -4 + 3.5 * math.sin(el / 9)  # helicopter loiter
        send({"t": "radar", "on": 1, "r": 15, "auto": True, "ac": sorted([
            {"c": "RYR8143", "ty": "B738", "x": round(x, 2), "y": round(y, 2), "al": 10363,
             "s": 820, "h": 45, "rt": "SVQ → STN"},
            {"c": "HLE45", "ty": "EC35", "x": round(hx, 2), "y": round(hy, 2), "al": 457,
             "s": 210, "h": int(math.degrees(math.atan2(-math.sin(el / 9), math.cos(el / 9))) + 90) % 360,
             "rt": ""},
        ], key=lambda p: math.hypot(p["x"], p["y"]))})
        if el - last_shot > a.flyover / 4 and el > 4:
            last_shot = el
            shot(f"flyover_{int(f * 100):02d}")
        time.sleep(0.25)
    send({"t": "radar", "on": 1, "r": 15, "auto": False, "ac": []})

    # ---- 2. spotlight: Air Force One, Andrews -> Elmendorf, somewhere over Canada
    print("🚨 spotlight")
    rd = R.Radar(send, send_raw, logging.getLogger("demo"))
    o, d = rd.routes.airport("KADW"), rd.routes.airport("PAED")
    rd.routes.routes["AF1"] = (o, d, time.time())
    rd.trails["adfdf8"] = [(41.5, -80.5), (44.0, -88.0), (47.0, -96.0), (49.8, -104.0)]
    af1 = {"hex": "adfdf8", "flight": "AF1     ", "r": "28000", "t": "B742",
           "desc": "Boeing VC-25A", "lat": 52.3, "lon": -112.0, "alt_baro": 37000,
           "gs": 505, "track": 305}
    rd.show("AERONAVE DESTACADA", False, af1, a.alert)
    time.sleep(3)
    shot("spotlight")
    time.sleep(max(0, a.alert - 3))
    stop.set()
    print("done ✅")


if __name__ == "__main__":
    main()
