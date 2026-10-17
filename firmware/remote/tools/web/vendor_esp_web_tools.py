#!/usr/bin/env python3
"""
Copies ESP Web Tools (the web installer's flashing component) into
web/esp-web-tools, pinned to VERSION, with one change: when an install
fails, its dialog offers Close instead of Back (Back went to the start and
reconnected, which is rarely what you want after a failure).

    python tools/web/vendor_esp_web_tools.py

Needs npm (to fetch the package). The vendored files are committed, so the
installer page works offline and can't change under us; run this only to
move to another version. ESP Web Tools is Apache-2.0 (its LICENSE is kept).
"""

import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

VERSION = "10.4.0"
PROJECT = Path(__file__).resolve().parent.parent.parent
DEST = PROJECT / "web" / "esp-web-tools"

# The failed-install state's only button: its handler, then its label.
FAILED_BACK = re.compile(r'@click=\$\{async\(\)=>\{this\._initialize\(\),this\._state="DASHBOARD"\}\}(\s*>\s*)Back')
FAILED_CLOSE = r"@click=${this._closeDialog}\1Close"


def main():
    with tempfile.TemporaryDirectory() as tmp:
        npm = shutil.which("npm") or shutil.which("npm.cmd")
        if not npm:
            sys.exit("npm not found")
        subprocess.run([npm, "pack", f"esp-web-tools@{VERSION}", "--silent"], cwd=tmp, check=True)
        with tarfile.open(Path(tmp) / f"esp-web-tools-{VERSION}.tgz") as tar:
            tar.extractall(tmp, filter="data")
        src = Path(tmp) / "package"
        if DEST.exists():
            shutil.rmtree(DEST)
        shutil.copytree(src / "dist" / "web", DEST)
        shutil.copy(src / "LICENSE", DEST / "LICENSE")

    dialogs = list(DEST.glob("install-dialog-*.js"))
    if len(dialogs) != 1:
        sys.exit(f"expected one install-dialog-*.js, found {len(dialogs)}")
    text = dialogs[0].read_text(encoding="utf-8")
    patched, n = FAILED_BACK.subn(FAILED_CLOSE, text)
    if n != 1:
        sys.exit("the failed-install button changed in this version: update FAILED_BACK")
    dialogs[0].write_text(patched, encoding="utf-8", newline="\n")
    if "_closeDialog(" not in text:
        sys.exit("_closeDialog isn't in this version")
    (DEST / "VERSION").write_text(
        f"esp-web-tools {VERSION} (npm), dist/web, Apache-2.0.\n"
        "Changed: a failed install offers Close instead of Back (tools/web/vendor_esp_web_tools.py).\n",
        encoding="utf-8", newline="\n")
    print(f"esp-web-tools {VERSION} -> {DEST.relative_to(PROJECT)} ({len(list(DEST.iterdir()))} files)")


if __name__ == "__main__":
    main()
