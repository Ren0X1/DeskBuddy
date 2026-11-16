"""Build DeskBuddy.exe (single file, no console, lives in the system tray).

    pip install -r agent/requirements.txt pyinstaller
    python agent/build_exe.py            -> agent/dist/DeskBuddy.exe
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE / "build"


def make_icon() -> Path:
    sys.path.insert(0, str(HERE))
    from PIL import Image
    import importlib.util
    # tray_image() lives in the agent; load it without starting the agent
    src = (HERE / "deskbuddy_agent.py").read_text(encoding="utf-8")
    start = src.index("def tray_image(")
    end = src.index("\n\n\n", start)
    ns: dict = {"Image": Image}
    exec(src[start:end], ns)
    BUILD.mkdir(exist_ok=True)
    ico = BUILD / "deskbuddy.ico"
    big = ns["tray_image"](True).resize((256, 256), Image.LANCZOS)
    big.save(ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    return ico


def main() -> None:
    ico = make_icon()
    cmd = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile", "--windowed",
           "--name", "DeskBuddy", "--icon", str(ico),
           "--distpath", str(HERE / "dist"), "--workpath", str(BUILD / "work"), "--specpath", str(BUILD),
           "--collect-all", "winrt", "--hidden-import", "pystray._win32",
           "--collect-submodules", "comtypes", "--collect-submodules", "pycaw",
           str(HERE / "deskbuddy_agent.py")]
    subprocess.run(cmd, check=True, cwd=HERE)
    print("built", HERE / "dist" / "DeskBuddy.exe")


if __name__ == "__main__":
    main()
