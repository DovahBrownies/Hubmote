#!/usr/bin/env python3
"""
Sends a firmware build to the remote over Wi-Fi (developer builds, port
8080): the same as scripts/hm-ota.ps1, for any OS. Standard library only.
Signs the upload with OTA_TOKEN from secrets.env (hm_auth.py); never
prints it.

    python tools/hm_ota.py                          # build/hubmote.bin to hubmote.local
    python tools/hm_ota.py --remote 192.168.1.50 --image build/hubmote.bin

Build first (idf.py build, or scripts/hm-idf.ps1 build on Windows). The
remote must already run firmware with updates; the first one goes on over
USB. A new build that doesn't reach Wi-Fi is rolled back on its next restart.
"""

import argparse
import json
import socket
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from hm_auth import PROJECT, read_token, signed_headers

WAIT_S = 90


def version(base, timeout):
    with urllib.request.urlopen(f"{base}/version", timeout=timeout) as r:
        return json.loads(r.read())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--remote", default="hubmote.local", help="name or address (Settings > About)")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--image", default=str(PROJECT / "build" / "hubmote.bin"))
    args = ap.parse_args()

    image = Path(args.image)
    if not image.is_file():
        sys.exit(f"no image at {image}; build first")
    token = read_token()

    # Look the name up once: after the remote restarts, some systems don't
    # resolve hubmote.local again for minutes, while the address stays.
    addr = args.remote
    try:
        addr = socket.getaddrinfo(args.remote, args.port, socket.AF_INET)[0][4][0]
    except OSError:
        pass
    base = f"http://{addr}:{args.port}"
    by_name = f"http://{args.remote}:{args.port}"
    try:
        before = version(base, 10)
    except (OSError, ValueError) as e:
        sys.exit(f"the remote doesn't answer at {base} ({e})")
    print(f"running: {before.get('built')} in {before.get('slot')}")

    data = image.read_bytes()
    print(f"sending {image} ({len(data)} bytes)...")
    try:
        headers = signed_headers(base, "POST", "/ota", token, data)
        headers["Content-Type"] = "application/octet-stream"
        req = urllib.request.Request(f"{base}/ota", data=data, method="POST", headers=headers)
        with urllib.request.urlopen(req, timeout=180) as r:
            print(f"remote: {r.read().decode(errors='replace').strip()}")
    except urllib.error.HTTPError as e:
        sys.exit(f"{e.code}: {e.read().decode(errors='replace').strip()}")
    except OSError as e:
        sys.exit(f"sending failed: {e}")

    # Wait for it to come back on the new build: by address first, by name in
    # case the address changed.
    deadline = time.monotonic() + WAIT_S
    time.sleep(5)
    while time.monotonic() < deadline:
        for url in (base, by_name):
            try:
                after = version(url, 5)
            except (OSError, ValueError):
                continue
            if after.get("slot") != before.get("slot"):
                print(f"updated: {after.get('built')} in {after.get('slot')}")
                return
        time.sleep(3)
    sys.exit(f"the remote didn't come back on the new build within {WAIT_S} s")


if __name__ == "__main__":
    main()
