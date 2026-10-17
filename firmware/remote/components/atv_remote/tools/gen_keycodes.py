"""Generate include/atv_keycodes.h from proto/remotemessage.proto.

The header maps the RemoteKeyCode enum of the Android TV Remote v2 protocol
(KEYCODE_X = n) to C constants (ATV_KEY_X = n), and adds the RemoteDirection
values. remotemessage.proto is vendored from tronikos/androidtvremote2 (see
proto/README.md for the pinned version).

Run it with any Python 3.8+ from any directory; it rewrites the header in place:

    python firmware/remote/components/atv_remote/tools/gen_keycodes.py

Needs only the standard library: the .proto is read as text, not compiled.
"""

import re
import sys
from pathlib import Path

COMPONENT = Path(__file__).resolve().parent.parent
PROTO_PATH = COMPONENT / "proto" / "remotemessage.proto"
OUT_PATH = COMPONENT / "include" / "atv_keycodes.h"


def enum_body(text: str, name: str) -> str:
    # Top-level enums in the .proto close with a "}" at the start of a line.
    m = re.search(r"enum\s+" + name + r"\s*\{(.*?)\n\}", text, re.S)
    if not m:
        sys.exit(f"enum {name} not found in {PROTO_PATH}")
    return m.group(1)


def main() -> None:
    text = PROTO_PATH.read_text(encoding="utf-8")

    # Entries look like "  KEYCODE_HOME = 3;" with an optional trailing comment.
    keys = re.findall(r"^\s*KEYCODE_(\w+)\s*=\s*(\d+)\s*;", enum_body(text, "RemoteKeyCode"), re.M)
    dirs = {n: int(v) for n, v in re.findall(r"^\s*(\w+)\s*=\s*(\d+)\s*;", enum_body(text, "RemoteDirection"), re.M)}

    # The direction enum is written out by hand below, with comments, so make
    # sure upstream has not changed it.
    expected_dirs = {"UNKNOWN_DIRECTION": 0, "START_LONG": 1, "END_LONG": 2, "SHORT": 3}
    if dirs != expected_dirs:
        sys.exit(f"RemoteDirection changed upstream: {dirs}")
    values = [int(v) for _, v in keys]
    if len(values) != len(set(values)):
        sys.exit("duplicate key code values in RemoteKeyCode")

    # Align the "=" signs on the longest name.
    width = max(len(n) for n, _ in keys) + len("ATV_KEY_")
    lines = [
        "#pragma once",
        "",
        "/*",
        " * GENERATED from remotemessage.proto (tronikos/androidtvremote2) by",
        " * tools/gen_keycodes.py. Do not edit by hand.",
        " *",
        " * RemoteKeyCode: KEYCODE_X = n  ->  ATV_KEY_X = n (Android KeyEvent codes).",
        " * RemoteDirection: values sent in remote_key_inject.direction.",
        " */",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "typedef enum {",
    ]
    for name, val in keys:
        lines.append(f"    {('ATV_KEY_' + name).ljust(width)} = {val},")
    lines += [
        "} atv_keycode_t;",
        "",
        "typedef enum {",
        "    ATV_DIR_UNKNOWN    = 0, /* UNKNOWN_DIRECTION */",
        "    ATV_DIR_START_LONG = 1, /* START_LONG: key down, held */",
        "    ATV_DIR_END_LONG   = 2, /* END_LONG: key up after START_LONG */",
        "    ATV_DIR_SHORT      = 3, /* SHORT: press and release */",
        "} atv_direction_t;",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
    ]
    with open(OUT_PATH, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"{len(keys)} key codes (max {max(values)}) -> {OUT_PATH}")


if __name__ == "__main__":
    main()
