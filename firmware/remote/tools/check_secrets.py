"""Compare secrets.env with the generated hm_secrets.h without printing the values.

Usage (from firmware/remote, after a build):  python tools/check_secrets.py
Prints lengths and flags only, so the output is safe to share.
"""
import re
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
env_path = sys.argv[1] if len(sys.argv) > 1 else PROJECT / "secrets.env"
hdr_path = sys.argv[2] if len(sys.argv) > 2 else PROJECT / "build" / "esp-idf" / "main" / "hm_secrets.h"
env = open(env_path, encoding="utf-8").read()
hdr = open(hdr_path, encoding="utf-8").read()
BS = chr(92)
QUOTE = chr(34)

for key, macro in (("WIFI_SSID", "HM_WIFI_SSID"), ("WIFI_PASSWORD", "HM_WIFI_PASSWORD")):
    m = re.search(r"^[ \t]*" + key + r"[ \t]*=([^\r\n]*)", env, re.M)
    v = (m.group(1) if m else "").strip()
    quoted = len(v) >= 2 and v[0] == v[-1] == QUOTE
    if quoted:
        v = v[1:-1]
    hm = re.search("#define " + macro + " " + QUOTE + "(.*)" + QUOTE + "\n", hdr)
    h = hm.group(1).replace(BS + QUOTE, QUOTE).replace(BS + BS, BS) if hm else None
    print(
        f"{key}: chars={len(v)} quoted_in_file={quoted} header_matches_file={h == v} "
        f"edge_spaces={v != v.strip()} non_ascii={any(ord(c) > 127 for c in v)} "
        f"placeholder={v in ('My Router SSID', 'MyRouterPassword')}"
    )
