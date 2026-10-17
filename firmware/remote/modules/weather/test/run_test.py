#!/usr/bin/env python3
"""
Builds and runs the Weather module's host test with a native gcc (any OS).
cJSON comes from ESP-IDF ($IDF_PATH, or --idf PATH), or from a cJSON
checkout (--cjson DIR, as in the GitHub workflow).

    python modules/weather/test/run_test.py [--idf PATH | --cjson DIR] [--gcc gcc]
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE = HERE.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--idf", default=os.environ.get("IDF_PATH"))
    ap.add_argument("--cjson", help="a cJSON source directory (instead of ESP-IDF's)")
    ap.add_argument("--gcc", default="gcc")
    args = ap.parse_args()
    if args.cjson:
        cjson = Path(args.cjson)
    elif args.idf:
        cjson = Path(args.idf) / "components" / "json" / "cJSON"
    else:
        sys.exit("set IDF_PATH, or pass --idf or --cjson (needed for cJSON)")
    exe = Path(tempfile.gettempdir()) / ("test_weather_data" + (".exe" if os.name == "nt" else ""))
    cmd = [args.gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
           "-I", str(MODULE), "-I", str(cjson),
           str(HERE / "test_weather_data.c"), str(MODULE / "weather_data.c"), str(cjson / "cJSON.c"),
           "-lm", "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(HERE)], check=True)


if __name__ == "__main__":
    main()
