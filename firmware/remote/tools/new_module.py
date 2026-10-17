#!/usr/bin/env python3
"""
Creates a new module from tools/module_template (docs/modules.md).

    python tools/new_module.py transit "Transit" --description "Next departures from my stop."

Makes modules/<id>/ with <id>.c, CMakeLists.txt, Kconfig.module and
README.md, ready to build: reconfigure once (`scripts/hm-idf.ps1
reconfigure` on Windows, `idf.py reconfigure` elsewhere) so menuconfig and
the build see it, then build.
"""

import argparse
import re
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
TEMPLATE = PROJECT / "tools" / "module_template"
MODULES = PROJECT / "modules"
# HUB_MODULE_ID_MAX: NVS namespaces are "m_<id>", 15 characters at most.
ID_MAX = int(re.search(r"#define HUB_MODULE_ID_MAX (\d+)",
                       (PROJECT / "components" / "hub" / "include" / "hub_module.h").read_text(encoding="utf-8")).group(1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("id", help=f"lowercase letters, digits and _; {ID_MAX} characters at most")
    ap.add_argument("name", help="shown on the Home tab and in the switcher")
    ap.add_argument("--description", default="", help="one or two sentences for menuconfig and the README")
    ap.add_argument("--author", default="", help="name or GitHub handle for the README")
    args = ap.parse_args()

    if not re.fullmatch(r"[a-z][a-z0-9_]*", args.id) or len(args.id) > ID_MAX:
        sys.exit(f"id must start with a letter, use a-z 0-9 _ and be {ID_MAX} characters at most")
    dest = MODULES / args.id
    if dest.exists():
        sys.exit(f"{dest} already exists")

    values = {
        "__ID_UPPER__": args.id.upper(),
        "__ID__": args.id,
        "__NAME__": args.name,
        "__DESCRIPTION__": args.description or f"The {args.name} module.",
        "__AUTHOR__": args.author or "(your name)",
    }
    dest.mkdir(parents=True)
    for src in TEMPLATE.iterdir():
        name = f"{args.id}.c" if src.name == "module.c" else src.name
        text = src.read_text(encoding="utf-8")
        for key, value in values.items():  # __ID_UPPER__ before __ID__
            text = text.replace(key, value)
        (dest / name).write_text(text, encoding="utf-8", newline="\n")
        print(f"  {dest.relative_to(PROJECT) / name}")
    print(f"Created modules/{args.id}. Next: reconfigure (scripts/hm-idf.ps1 reconfigure), then build.")


if __name__ == "__main__":
    main()
