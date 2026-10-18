#!/usr/bin/env python3
r"""
Makes the project's update signing key, once (docs/releasing.md): an ECDSA
P-256 key pair. The private key goes to the file you name, outside the
repository; keep it secret, and put it in the repository's
UPDATE_SIGNING_KEY secret, from which CI signs releases
(tools/web/make_site.py --sign). The public key goes to main/update_key.pem,
which the firmware checks release images with (main/update.c): commit it.

    python tools/web/make_update_key.py C:\Users\me\hubmote-update-key.pem
    gh secret set UPDATE_SIGNING_KEY < C:\Users\me\hubmote-update-key.pem

It refuses to overwrite either file. A new key means remotes built with the
old public key refuse later releases until they get a firmware with the new
one over USB or Wi-Fi from a PC, so make it once and keep it safe. Needs the
openssl command (Git for Windows has one).
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent.parent
PUBLIC = PROJECT / "main" / "update_key.pem"


def find_openssl() -> str:
    """openssl on the PATH, or Git for Windows' own."""
    found = shutil.which("openssl")
    git = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Git" / "usr" / "bin" / "openssl.exe"
    if found:
        return found
    if git.is_file():
        return str(git)
    sys.exit("needs the openssl command (Git for Windows has one)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("private", help="where the private key goes (outside the repository)")
    args = ap.parse_args()
    private = Path(args.private).resolve()
    if private.exists() or PUBLIC.exists():
        sys.exit(f"{private if private.exists() else PUBLIC} exists: not overwriting a key")
    if PROJECT.parent.parent in private.parents:
        sys.exit("the private key must not be inside the repository")
    openssl = find_openssl()
    subprocess.run([openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(private)], check=True)
    subprocess.run([openssl, "ec", "-in", str(private), "-pubout", "-out", str(PUBLIC)], check=True,
                   capture_output=True)
    print(f"private key: {private} (secret: add it as UPDATE_SIGNING_KEY, keep a copy safe)")
    print(f"public key:  {PUBLIC} (commit it)")


if __name__ == "__main__":
    main()
