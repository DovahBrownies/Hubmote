#!/usr/bin/env python3
"""
Installs build/companion.apk on the Streamer over ADB, turns on its
accessibility service and lets it update itself: the same as install.ps1,
for any OS. Standard library only. The Streamer needs Wireless debugging
(docs/companion.md).

    python install.py --device 192.168.1.60:35777     # host:port from Wireless debugging
    python install.py --device ... --apk other.apk

adb comes from ANDROID_HOME (or ANDROID_SDK_ROOT), the SDK that
setup-tools.ps1 installs on Windows, or the PATH.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PKG = "io.github.dovahbrownies.hubmote"
SVC = f"{PKG}/.CompanionService"


def find_adb():
    exe = "adb.exe" if os.name == "nt" else "adb"
    roots = [os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT")]
    if os.environ.get("LOCALAPPDATA"):
        roots.append(str(Path(os.environ["LOCALAPPDATA"]) / "Android" / "Sdk"))
    for root in filter(None, roots):
        p = Path(root) / "platform-tools" / exe
        if p.is_file():
            return str(p)
    found = shutil.which("adb")
    if not found:
        sys.exit("adb not found: set ANDROID_HOME or put adb on the PATH")
    return found


def run(adb, device, *args):
    cmd = [adb] + (["-s", device] if device else []) + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"adb {' '.join(args)} failed: {(r.stdout + r.stderr).strip()}")
    return r.stdout.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--device", required=True, help="host:port shown under Wireless debugging")
    ap.add_argument("--apk", default=str(HERE / "build" / "companion.apk"))
    args = ap.parse_args()
    if not Path(args.apk).is_file():
        sys.exit(f"no APK at {args.apk}; build it first (build.ps1)")
    adb = find_adb()

    print(run(adb, None, "connect", args.device))
    print(run(adb, args.device, "install", "-r", args.apk))

    # Add the service to the enabled list without dropping others (e.g. TalkBack).
    cur = run(adb, args.device, "shell", "settings", "get", "secure", "enabled_accessibility_services")
    services = [s for s in cur.split(":") if s and s != "null"]
    if SVC not in services:
        services.append(SVC)
    new = ":".join(services)
    run(adb, args.device, "shell", "settings", "put", "secure", "enabled_accessibility_services", new)
    run(adb, args.device, "shell", "settings", "put", "secure", "accessibility_enabled", "1")
    # "Install unknown apps" for the companion: where the TV lets apps install
    # at all, Android then lets it update itself without asking on the TV.
    run(adb, args.device, "shell", "appops", "set", PKG, "REQUEST_INSTALL_PACKAGES", "allow")
    # Notification access for its media listener, so it can tell the remote
    # what is playing (0.6+). It reads no notifications.
    run(adb, args.device, "shell", "cmd", "notification", "allow_listener", f"{PKG}/.MediaListener")
    print(f"installed; accessibility services now: {new}")


if __name__ == "__main__":
    main()
