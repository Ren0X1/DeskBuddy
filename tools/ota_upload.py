"""Upload a firmware to DeskBuddy over Wi-Fi, retrying on the occasional dropped
connection (the single-core ESP32-C3 can stall while busy right after a boot).

usage: python tools/ota_upload.py <host> <password> <firmware.bin> [tries]
"""
import subprocess
import sys
import time


def main() -> int:
    host, password, firmware = sys.argv[1:4]
    tries = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    for attempt in range(1, tries + 1):
        print(f"OTA upload to {host} (attempt {attempt}/{tries})...", flush=True)
        r = subprocess.run(["curl", "--fail", "-sS", "--connect-timeout", "10",
                            "-u", f"deskbuddy:{password}", "-F", f"firmware=@{firmware}",
                            f"http://{host}/update"], capture_output=True, text=True)
        if r.returncode == 0 and "Success" in r.stdout:
            print("Update Success! DeskBuddy is rebooting.")
            return 0
        print(f"  failed: {(r.stderr or r.stdout).strip()[:120]}", flush=True)
        if attempt < tries:
            time.sleep(15)
    return 1


if __name__ == "__main__":
    sys.exit(main())
