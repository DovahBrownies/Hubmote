#!/usr/bin/env python3
"""
Screenshots and simulated touches over Wi-Fi (firmware built with
CONFIG_HM_DEV_TOOLS). Signs each request with OTA_TOKEN from secrets.env
(hm_auth.py); never prints it.

    python tools/devtools.py shot [out.png]          # PNG if Pillow is installed, else BMP
    python tools/devtools.py tap X Y [--ms 80]
    python tools/devtools.py swipe X1 Y1 X2 Y2 [--ms 300]

Options: --host (default hubmote.local), --port (default 8080).
Coordinates are screen pixels (0..1023 by 0..599 on the 7B), as the picture is drawn.
"""

import argparse
import socket
import sys
import urllib.error
import urllib.request
from pathlib import Path

from hm_auth import read_token, signed_headers


def request(args, method, path):
    # Resolve the name once: on Windows each mDNS lookup of hubmote.local
    # takes a second or more, and a signed request makes two connections.
    addr = args.host
    try:
        addr = socket.getaddrinfo(args.host, args.port, socket.AF_INET)[0][4][0]
    except OSError:
        pass
    base = f"http://{addr}:{args.port}"
    url = base + path
    try:
        req = urllib.request.Request(url, method=method, headers=signed_headers(base, method, path, read_token()))
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        sys.exit(f"{e.code}: {e.read().decode(errors='replace').strip()}")
    except urllib.error.URLError as e:
        sys.exit(f"can't reach {url}: {e.reason}")


def shot(args):
    data = request(args, "GET", "/screenshot")
    out = Path(args.out)
    try:
        from io import BytesIO
        from PIL import Image
        img = Image.open(BytesIO(data))
        if out.suffix.lower() != ".png":
            out = out.with_suffix(".png")
        img.save(out)
    except ImportError:
        out = out.with_suffix(".bmp")
        out.write_bytes(data)
    print(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="hubmote.local")
    ap.add_argument("--port", type=int, default=8080)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("shot")
    s.add_argument("out", nargs="?", default="screenshot.png")
    t = sub.add_parser("tap")
    t.add_argument("x", type=int)
    t.add_argument("y", type=int)
    t.add_argument("--ms", type=int, default=80)
    w = sub.add_parser("swipe")
    for n in ("x1", "y1", "x2", "y2"):
        w.add_argument(n, type=int)
    w.add_argument("--ms", type=int, default=300)
    args = ap.parse_args()

    if args.cmd == "shot":
        shot(args)
    elif args.cmd == "tap":
        request(args, "POST", f"/tap?x={args.x}&y={args.y}&ms={args.ms}")
    else:
        request(args, "POST", f"/swipe?x1={args.x1}&y1={args.y1}&x2={args.x2}&y2={args.y2}&ms={args.ms}")


if __name__ == "__main__":
    main()
