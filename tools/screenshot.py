"""Grab a PNG of the DeskBuddy screen over USB serial.

usage: python tools/screenshot.py [--port COM3] [--tile N ...] [--out DIR]
"""
import argparse
import time
from pathlib import Path

import serial
from PIL import Image, ImageDraw


def read_line(s: serial.Serial) -> bytes:
    line = s.readline()
    if not line:
        raise TimeoutError("no answer from DeskBuddy")
    return line.strip()


def grab(s: serial.Serial) -> Image.Image:
    s.reset_input_buffer()
    s.write(b"shot\n")
    while not read_line(s).startswith(b"SHOT"):
        pass
    img = Image.new("RGB", (240, 240))
    px = img.load()
    while True:
        line = read_line(s)
        if line == b"END":
            break
        if not line.startswith(b"RECT"):
            continue
        try:
            x0, y0, w, h = map(int, line.split()[1:5])
        except ValueError:
            continue
        raw = s.read(w * h * 2)
        raw += bytes(w * h * 2 - len(raw))  # tolerate a short read
        for i in range(w * h):
            if not (0 <= x0 + i % w < 240 and 0 <= y0 + i // w < 240):
                continue
            v = raw[2 * i] | raw[2 * i + 1] << 8  # little-endian RGB565
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            px[x0 + i % w, y0 + i // w] = (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
    # mask to the round panel
    mask = Image.new("L", (240, 240), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, 239, 239), fill=255)
    out = Image.new("RGB", (240, 240), (40, 40, 40))
    out.paste(img, mask=mask)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--tile", type=int, nargs="*", default=[None])
    ap.add_argument("--out", default="screenshots")
    a = ap.parse_args()
    Path(a.out).mkdir(exist_ok=True)
    with serial.Serial(a.port, 115200, timeout=3) as s:
        for t in a.tile:
            if t is not None:
                s.write(f"tile {t}\n".encode())
                time.sleep(0.6)
            path = Path(a.out) / f"tile{'' if t is None else t}.png"
            grab(s).save(path)
            print("saved", path)


if __name__ == "__main__":
    main()
