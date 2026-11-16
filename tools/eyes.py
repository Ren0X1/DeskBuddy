"""Single source of truth for DeskBuddy's eye animations.

    python tools/eyes.py        -> firmware/src/eye_anims.h  (+ tools/eyes.json for the web preview)

Each animation is a list of (milliseconds, patch) keyframes; a patch is applied
on top of BASE (so {} means "back to rest") and the device eases between them.
Patch keys: "E" (both eyes), "L"/"R" (one eye) with x y w h r lt la lb s, plus
dx dy rot for the whole face.  lt = top-lid cover 0..1, la = lid slant degrees
(+ angry, - sad), lb = happy cheek 0..1, s = scale.  `ov` names an overlay.
"""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EYE_FIELDS = ["x", "y", "w", "h", "r", "lt", "la", "lb", "s"]
GLOBAL_FIELDS = ["dx", "dy", "rot"]
BASE = {"L": {"x": -42, "y": 0, "w": 54, "h": 74, "r": 20, "lt": 0, "la": 0, "lb": 0, "s": 1},
        "R": {"x": 42, "y": 0, "w": 54, "h": 74, "r": 20, "lt": 0, "la": 0, "lb": 0, "s": 1},
        "dx": 0, "dy": 0, "rot": 0}

OVERLAYS = ["none", "scan", "haha", "rage", "exclam", "question", "dots", "yawn", "zzz", "hearts",
            "stars", "spiral", "xx", "tear", "sweat", "notes", "sing", "lock", "sweep", "grid",
            "boot", "glitch", "loading", "blush", "lockR", "confetti", "retina", "nv", "lost",
            "alert", "plane", "note1", "laser", "sonar", "hypno", "countdown", "matrix",
            "telemetry", "flash", "aim", "wave"]

ANIMS: list[dict] = []


def add(name, steps, ov="none"):
    assert ov in OVERLAYS, ov
    ANIMS.append({"name": name, "ov": ov, "steps": [[int(ms), p] for ms, p in steps]})


def W(p, **extra):
    q = copy.deepcopy(p)
    q.update(extra)
    return q


def hold(p, ms):
    return [[220, p], [ms, p], [260, {}]]


def shake(p, n, amp, ms=60):
    return [[ms, W(p, dx=p.get("dx", 0) + (amp if i % 2 else -amp))] for i in range(n)]


# ---------------------------------------------------------------- the catalogue
add("Parpadeo", [[80, {"E": {"h": 6}}], [120, {}], [700, {}]])
add("Doble parpadeo", [[70, {"E": {"h": 6}}], [90, {}], [70, {"E": {"h": 6}}], [110, {}], [600, {}]])
add("Parpadeo lento", [[500, {"E": {"h": 6}}], [300, {"E": {"h": 6}}], [500, {}], [300, {}]])
add("Guiño izquierdo", [[120, {"L": {"h": 6}, "R": {"lb": .15}}], [700, {"L": {"h": 6}, "R": {"lb": .15}}], [200, {}], [300, {}]])
add("Guiño derecho", [[120, {"R": {"h": 6}, "L": {"lb": .15}}], [700, {"R": {"h": 6}, "L": {"lb": .15}}], [200, {}], [300, {}]])
add("Mirar a la izquierda", hold({"dx": -32}, 900))
add("Mirar a la derecha", hold({"dx": 32}, 900))
add("Mirar arriba", hold({"dy": -22, "E": {"h": 66}}, 900))
add("Mirar abajo", hold({"dy": 24, "E": {"lt": .25}}, 900))
add("Escanear", [[500, {"dx": -34}], [900, {"dx": 34}], [900, {"dx": -34}], [500, {}]], "scan")
add("Vistazos rápidos", [[90, {"dx": -28, "dy": -8}], [320, {"dx": -28, "dy": -8}], [90, {"dx": 26, "dy": 6}],
                         [320, {"dx": 26, "dy": 6}], [90, {"dx": -10, "dy": 14}], [320, {"dx": -10, "dy": 14}], [160, {}]])
add("Poner los ojos en blanco", [[180, {"dy": -22}], [180, {"dx": 24, "dy": -14}], [180, {"dx": 30, "dy": 4}],
                                 [180, {"dx": 16, "dy": 18}], [180, {"dx": -16, "dy": 18}], [180, {"dx": -30, "dy": 4}],
                                 [180, {"dx": -24, "dy": -14}], [180, {"dy": -22}], [300, {}]])
add("Bizco", hold({"L": {"x": -26}, "R": {"x": 26}, "E": {"w": 42, "h": 58}}, 1100))
add("Feliz", hold({"E": {"lb": .55}}, 1400))
add("Risa", [[150, {"E": {"lb": .6}, "dy": -8}], [150, {"E": {"lb": .6}}], [150, {"E": {"lb": .6}, "dy": -8}],
             [150, {"E": {"lb": .6}}], [150, {"E": {"lb": .6}, "dy": -8}], [350, {"E": {"lb": .6}}], [250, {}]], "haha")
add("Triste", [[400, {"dy": 16, "E": {"lt": .35, "la": -18, "h": 66}}], [1400, {"dy": 16, "E": {"lt": .35, "la": -18, "h": 66}}], [400, {}]])
add("Enfadado", hold({"E": {"lt": .3, "la": 22}}, 1400))
_f = {"E": {"lt": .38, "la": 26}}
add("Furia", [[150, _f], *shake(_f, 10, 5), [300, _f], [300, {}]], "rage")
_b = {"E": {"w": 62, "h": 86, "r": 30}}
add("Sorpresa", [[120, _b], [1200, _b], [300, {}]], "exclam")
_s = {"E": {"w": 64, "h": 88, "r": 32}}
add("Susto", [[80, W(_s, dy=-14)], [200, _s], *shake(_s, 6, 3, 50), [400, {}]])
_q = {"E": {"lt": .45, "lb": .2}}
add("Sospecha", [[300, W(_q, dx=-22)], [1200, W(_q, dx=-22)], [300, W(_q, dx=22)], [800, W(_q, dx=22)], [300, {}]])
add("Mirada de reojo", hold({"dx": 30, "L": {"lt": .5}, "R": {"lt": .2}}, 1500))
add("Curioso", hold({"rot": -12, "L": {"s": .85}, "R": {"s": 1.15}}, 1400))
add("Confuso", hold({"L": {"y": -8, "lt": .3, "la": -15}, "R": {"y": 6, "s": 1.1}}, 1600), "question")
add("Pensando", hold({"dx": 22, "dy": -20, "E": {"h": 66}}, 1700), "dots")
add("Somnoliento", [[600, {"dy": 10, "E": {"lt": .5}}], [300, {"dy": 10, "E": {"lt": .62}}], [300, {"dy": 10, "E": {"lt": .45}}],
                    [700, {"dy": 12, "E": {"lt": .72}}], [400, {}]])
add("Bostezo", [[300, {"E": {"lt": .55, "lb": .3}}], [1100, {"E": {"lt": .6, "lb": .3}}], [300, {"E": {"h": 8}}], [400, {}]], "yawn")
add("Dormir", [[600, {"dy": 14, "E": {"h": 6}}], [2600, {"dy": 14, "E": {"h": 6}}], [500, {}]], "zzz")
add("Despertar", [[1, {"dy": 14, "E": {"h": 6}}], [500, {"dy": 14, "E": {"h": 6}}], [150, {"E": {"h": 40}}], [100, {"E": {"h": 6}}],
                  [220, {"E": {"w": 60, "h": 82}}], [600, {"E": {"w": 60, "h": 82}}], [300, {}]])
_hide = {"E": {"s": .001}}
add("Enamorado", hold(_hide, 1800), "hearts")
add("Ojos de estrella", hold(_hide, 1800), "stars")
add("Mareado", [[200, _hide], [500, W(_hide, rot=-10)], [500, W(_hide, rot=10)], [500, W(_hide, rot=-10)], [300, {}]], "spiral")
add("K.O.", hold(_hide, 1700), "xx")
add("Llorar", hold({"dy": 12, "E": {"lt": .35, "la": -18}}, 1900), "tear")
add("Sudor frío", hold({"dx": -10, "E": {"lb": .2}}, 1700), "sweat")
add("Asentir", [[150, {"dy": 14}], [150, {"dy": -6}], [150, {"dy": 14}], [150, {"dy": -6}], [250, {}]])
add("Negar", [[120, {"dx": -20}], [120, {"dx": 20}], [120, {"dx": -20}], [120, {"dx": 20}], [250, {}]])
add("Rebote", [[200, {"dy": -18, "E": {"h": 80}}], [200, {"dy": 6, "E": {"h": 62, "w": 60}}], [150, {"dy": -6}], [250, {}]])
add("Ritmo musical", [*[[220, {"rot": 8 if i % 2 else -8, "dx": 8 if i % 2 else -8, "E": {"lb": .3}}] for i in range(8)], [250, {}]], "notes")
add("Cantar", [*[[280, {"dy": -4 if i % 2 else 2, "E": {"lb": .55}}] for i in range(6)], [250, {}]], "sing")
add("Fijar blanco", hold({"E": {"w": 44, "h": 44, "r": 22}}, 1700), "lock")
add("Barrido radar", [[2200, {}]], "sweep")
add("Calibración", [[300, {"E": {"w": 30, "h": 30, "r": 3}}], [300, {"E": {"w": 70, "h": 40, "r": 3}}],
                    [300, {"E": {"w": 40, "h": 80, "r": 3}}], [300, {"E": {"r": 3}}], [400, {}]], "grid")
add("Arranque", [[1, {"E": {"w": 2, "h": 2}}], [300, {"E": {"w": 2, "h": 2}}], [300, {"E": {"w": 54, "h": 2}}], [400, {}], [500, {}]], "boot")
add("Apagado", [[250, {"E": {"h": 2}}], [250, {"E": {"w": 2, "h": 2}}], [600, {"E": {"w": 2, "h": 2}}], [200, _hide], [500, _hide], [350, {}]])
add("Glitch", [*[[90, {"dx": (-6, 9, -3, 0, 12, -10, 4, 0)[i % 8], "E": {"w": 54 + (0, 8, -6, 0, 4, 0, -8, 0)[i % 8]}}] for i in range(16)], [300, {}]], "glitch")
add("Cargando", hold(_hide, 1900), "loading")
add("Morse SOS", [*[s for ch in "...---..." for s in ([110 if ch == "." else 360, {"E": {"h": 6}}], [140, {}])], [400, {}]])
add("Tiritar", [*shake({"E": {"h": 70}}, 16, 3, 45), [250, {}]])
add("Estirarse", [[400, {"E": {"h": 96, "w": 44}}], [300, {"E": {"h": 50, "w": 70}}], [300, {}], [300, {}]])
add("Asomarse", [[1, {"dy": 150}], [600, {"dy": 40}], [800, {"dy": 40}], [150, {"dy": 40, "E": {"h": 6}}], [150, {"dy": 40}], [500, {}]])
add("Esconderse", [[400, {"dy": 160}], [900, {"dy": 160}], [500, {}]])
add("Enfocar", hold({"E": {"s": .6}}, 1200))
add("Ojos como platos", hold({"E": {"w": 70, "h": 96, "r": 35}}, 1200))
add("Latido", [[150, {"E": {"s": 1.12}}], [150, {}], [150, {"E": {"s": 1.12}}], [700, {}]])
add("Alerta", hold({"E": {"w": 60, "h": 84}}, 1600), "alert")
add("Pícaro", hold({"L": {"h": 6}, "R": {"lb": .45}, "rot": -6}, 1400))
add("Desconfianza", hold({"E": {"lt": .42, "lb": .25}}, 1600))
add("Tímido", hold({"dy": 18, "dx": -14, "E": {"lt": .2, "lb": .3}}, 1600), "blush")
add("Ojo de halcón", hold({"L": {"s": .7}, "R": {"s": 1.3}}, 1400), "lockR")
add("Celebración", [[150, {"dy": -16, "E": {"lb": .6}}], [150, {"E": {"lb": .6}}], [150, {"dy": -16, "E": {"lb": .6}}],
                    [700, {"E": {"lb": .6}}], [250, {}]], "confetti")
add("Escáner de retina", hold({"E": {"w": 54, "h": 54, "r": 27}}, 1800), "retina")
add("Visión nocturna", [[2000, {}]], "nv")
add("Objetivo perdido", [[200, {"dx": -30}], [200, {"dx": 30}], [200, {"dx": -20, "dy": 10}], [200, {"dx": 24, "dy": -10}],
                         [500, {"E": {"lt": .2, "la": -10}}], [300, {}]], "lost")
add("Avión a la vista", [[300, {"dx": 26, "dy": -16}], [1600, {"dx": 26, "dy": -16, "E": {"w": 60, "h": 80}}], [300, {}]], "plane")
add("Nueva canción", hold({"E": {"lb": .4}}, 1500), "note1")
# ---- second batch
add("Saludo", [[150, {"L": {"h": 6}, "R": {"lb": .3}, "dy": -10}], [250, {"L": {"h": 6}, "R": {"lb": .3}}],
               [150, {"dy": -10, "E": {"lb": .3}}], [600, {"E": {"lb": .3}}], [250, {}]])
add("Ola", [*[[160, {"L": {"y": -14 if i % 2 else 10}, "R": {"y": 10 if i % 2 else -14}}] for i in range(8)], [250, {}]])
add("Parpadeo alterno", [*[s for i in range(4) for s in ([90, {"L" if i % 2 else "R": {"h": 6}}], [110, {}])], [400, {}]])
add("Aburrido", [[600, {"dx": -24, "E": {"lt": .5}}], [1400, {"dx": 24, "E": {"lt": .5}}], [1000, {"dx": 24, "E": {"lt": .55}}], [500, {}]])
add("Orgulloso", hold({"dy": -12, "E": {"lb": .5, "lt": .1}}, 1500))
add("Determinado", hold({"E": {"lt": .28, "la": 12, "lb": .12, "w": 58}}, 1500))
add("Ojos cuadrados", hold({"E": {"r": 2, "w": 58, "h": 58}}, 1400))
add("Ojos de gato", hold({"E": {"w": 18, "h": 80, "r": 9}}, 1500))
add("Láser", hold({"E": {"lt": .3, "la": 10}}, 1700), "laser")
add("Sonar", [[2400, {}]], "sonar")
add("Hipnosis", hold(_hide, 2200), "hypno")
add("Cuenta atrás", hold({"E": {"w": 40, "h": 40, "r": 20}}, 3000), "countdown")
add("Lluvia de código", [[2400, {"E": {"lt": .1}}]], "matrix")
add("Telemetría", hold({"dx": -20, "E": {"s": .8}}, 2200), "telemetry")
add("Destello", [[80, {"E": {"w": 70, "h": 96}}], [200, {"E": {"h": 6}}], [400, {"E": {"h": 6}}], [300, {}]], "flash")
add("Puntería", [[400, {"dx": -24, "dy": -10}], [400, {"dx": 20, "dy": 8}], [400, {"dx": 6, "dy": -14}], [900, {"dx": 6, "dy": -14}], [300, {}]], "aim")
add("Escáner vertical", [[500, {"dy": -24, "E": {"h": 10}}], [800, {"dy": 24, "E": {"h": 10}}], [800, {"dy": -24, "E": {"h": 10}}], [400, {}]])
add("Olas de sonido", hold({"E": {"lb": .25}}, 2000), "wave")


# ---------------------------------------------------------------- generators
def flatten(patch):
    """patch -> [(field_name, value)] with names like 'h', 'Lh', 'Rlb', 'dx'."""
    out = []
    for k, v in patch.items():
        if k in GLOBAL_FIELDS:
            out.append((k, v))
        elif k == "E":
            out += [(f, val) for f, val in v.items()]
        elif k in ("L", "R"):
            out += [(k + f, val) for f, val in v.items()]
        else:
            raise ValueError(k)
    return out


def field_ids():
    names = GLOBAL_FIELDS + EYE_FIELDS + ["L" + f for f in EYE_FIELDS] + ["R" + f for f in EYE_FIELDS]
    return {n: i for i, n in enumerate(names)}


def c_escape(s):
    return "".join(c if ord(c) < 128 else "".join(f"\\x{b:02X}\"\"" for b in c.encode()) for c in s)


def gen_c():
    ids = field_ids()
    lines = ["// Generated by tools/eyes.py - do not edit", "#pragma once", "#include <stdint.h>", ""]
    lines.append("enum EyeField : uint8_t {" + ", ".join(f"EF_{n.upper()}" for n in ids) + ", EF_COUNT};")
    lines.append("enum EyeOverlay : uint8_t {" + ", ".join(f"OV_{o.upper()}" for o in OVERLAYS) + ", OV_COUNT};")
    lines += ["struct EyeKv { uint8_t f; float v; };",
              "struct EyeStep { uint16_t ms; uint8_t n; uint16_t kv; };  // kv = index into EYE_KV",
              "struct EyeAnim { const char* name; uint8_t ov; uint8_t n; uint16_t step; };", ""]
    kvs, steps, anims = [], [], []
    for a in ANIMS:
        first = len(steps)
        for ms, p in a["steps"]:
            fl = flatten(p)
            steps.append((ms, len(fl), len(kvs)))
            kvs += [(ids[f], v) for f, v in fl]
        anims.append((a["name"], OVERLAYS.index(a["ov"]), len(a["steps"]), first))
    lines.append("static const EyeKv EYE_KV[] = {" + ", ".join(f"{{{f},{float(v)!r}f}}" for f, v in kvs) + "};")
    lines.append("static const EyeStep EYE_STEPS[] = {" + ", ".join(f"{{{a},{b},{c}}}" for a, b, c in steps) + "};")
    lines.append("static const EyeAnim EYE_ANIMS[] = {")
    lines += [f'  {{"{c_escape(n)}", {o}, {k}, {s}}},' for n, o, k, s in anims]
    lines.append("};")
    lines.append(f"#define EYE_ANIM_COUNT {len(anims)}")
    (ROOT / "firmware" / "src" / "eye_anims.h").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    gen_c()
    (ROOT / "tools" / "eyes.json").write_text(
        json.dumps({"base": BASE, "overlays": OVERLAYS, "anims": ANIMS}, ensure_ascii=False), encoding="utf-8")
    print(f"{len(ANIMS)} animations, {len(OVERLAYS) - 1} overlays")


if __name__ == "__main__":
    main()
