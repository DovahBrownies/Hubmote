#!/usr/bin/env python3
"""
Assembles the web installer site from finished release builds, one per
board: for each, a folder named by the board's id (CONFIG_HM_BOARD_ID) with
the four images ESP Web Tools writes and their manifest.json; then
index.html (the board picker and install button), the vendored ESP Web
Tools, and update.json, which lists the boards for the page and their
images for remotes updating themselves over Wi-Fi (main/update.c).
scripts/hm-web.ps1 runs it on a PC, and the GitHub workflow
(.github/workflows/build.yml) runs it for GitHub Pages.

    python tools/web/make_site.py [--build build-web ...] [--out build-web/site]
    python tools/web/make_site.py --companion ../../companion/build [--no-firmware]

Each build's board comes from its configuration (config/sdkconfig.json);
its name, and whether it is untested, from its header
(components/board/boards/<id>.h: BOARD_NAME, BOARD_UNTESTED). The builds
must be of the same version.

--companion adds the companion app (companion.apk and companion.json from
companion/build.ps1): companion.apk to download by hand, a copy named by
its version, and its entry in update.json, from which remotes offer it to
companions 0.5+ (Settings > Remotes). Only an APK signed with the project's
key (HM_KEYSTORE) goes into update.json, as Android refuses an update signed
with another key; --any-key lifts that, for testing with the debug key.
--no-firmware only adds the companion to a site already assembled (CI).

--sign KEY signs each board's image in update.json with the project's
private key (a PEM file; CI has it from the UPDATE_SIGNING_KEY secret), as
main/update.c checks: an ECDSA P-256 signature over
"hubmote-firmware\n<board>\n<version>\n<sha256>\n", base64, as "sig". It
refuses a key whose public half isn't main/update_key.pem (or --public,
for a test build's key), and checks each signature with it before
writing. With --no-firmware and no
--companion it only signs. Needs the openssl command.

Paths are relative to firmware/remote. The version comes from the image
(or version.txt), the build time from the build's stamp
(esp-idf/main/build_stamp.h).

It refuses an app image that contains a value from secrets.env (a Wi-Fi
password, the update token): a site is for others to download. Build with
HM_NO_SECRETS, as scripts/hm-web.ps1 does. Only the key's name is printed.
It also refuses a developer build (HM_DEVELOPER: an update server, devtools,
http:// updates), unless --developer says it's for a test site, and an image
whose built-in update key (main/update_key.pem, or a test build's
HM_UPDATE_KEY) isn't --public's: remotes would refuse the next release.
"""

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent.parent

# What ESP Web Tools writes, and where: the bootloader, the partition table,
# the OTA data that boots ota_0, and the app (partitions.csv).
PARTS = [
    ("bootloader/bootloader.bin", "bootloader.bin", 0x0),
    ("partition_table/partition-table.bin", "partition-table.bin", 0x8000),
    ("ota_data_initial.bin", "ota_data_initial.bin", 0x1A000),
    ("hubmote.bin", "hubmote.bin", 0x20000),
]


def image_version(image: Path) -> str:
    """The version ESP-IDF stamped into the app image (esp_app_desc_t right
    after the image and first segment headers), or "" if it isn't there."""
    data = image.read_bytes()[:256]
    if len(data) < 80 or int.from_bytes(data[32:36], "little") != 0xABCD5432:
        return ""
    return data[48:80].split(b"\0", 1)[0].decode("ascii", "replace")


def secrets_in(image: bytes) -> list:
    """The names of secrets.env values found in the image (never the values)."""
    env = PROJECT / "secrets.env"
    if not env.is_file():
        return []
    found = []
    for line in env.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] == '"':
            value = value[1:-1]
        # Short values (an SSID like "Home") also turn up in the image's own
        # text; Wi-Fi passwords and tokens are at least 8 characters.
        if len(value) >= 8 and value.encode() in image:
            found.append(key.strip())
    return found


def build_time(build: Path) -> str:
    stamp = build / "esp-idf" / "main" / "build_stamp.h"
    m = re.search(r'HM_BUILD_DATE "([^"]+)"[\s\S]*HM_BUILD_TIME "([^"]+)"', stamp.read_text(encoding="utf-8"))
    return f"{m.group(1)} {m.group(2)}" if m else ""


# The remote reads update.json into a buffer of this size (JSON_MAX in
# main/update.c): a bigger file would fail to parse on every remote.
UPDATE_JSON_MAX = 8192


def write_update(path: Path, update: dict):
    """Writes update.json compact, refusing one the remotes couldn't read."""
    text = json.dumps(update, separators=(",", ":")) + "\n"
    if len(text.encode("utf-8")) > UPDATE_JSON_MAX:
        sys.exit(f"update.json would be {len(text)} bytes, over the remote's {UPDATE_JSON_MAX} "
                 "(JSON_MAX in main/update.c): raise both")
    path.write_text(text, encoding="utf-8", newline="\n")


def config_of(build: Path) -> dict:
    config = build / "config" / "sdkconfig.json"
    return json.loads(config.read_text(encoding="utf-8")) if config.is_file() else {}


def pem_body(path: Path) -> bytes:
    """A PEM file's base64 lines, as the image embeds them (line ends aside)."""
    lines = path.read_text(encoding="utf-8").replace("\r", "").split("\n")
    return "\n".join(l for l in lines if l and not l.startswith("-----")).encode()


def board_of(build: Path) -> dict:
    """The board a build is for: its id from the build's configuration, its
    name and whether it is untested from its header."""
    config = build / "config" / "sdkconfig.json"
    board_id = config_of(build).get("HM_BOARD_ID")
    if not board_id:
        sys.exit(f"no HM_BOARD_ID in {config} (build first)")
    header = (PROJECT / "components" / "board" / "boards" / f"{board_id}.h").read_text(encoding="utf-8")
    name = re.search(r'#define BOARD_NAME\s+"([^"]+)"', header)
    return {
        "id": board_id,
        "name": name.group(1) if name else board_id,
        "untested": re.search(r"#define BOARD_UNTESTED\s+1\b", header) is not None,
    }


def add_board(build: Path, out: Path, developer: bool, public: Path) -> dict:
    """Copies one build's images into out/<id>/ with their manifest;
    returns the board's id, version, build time and update.json entry."""
    missing = [src for src, _, _ in PARTS if not (build / src).is_file()]
    if missing:
        sys.exit(f"not in {build}: {', '.join(missing)} (build first)")
    image = (build / "hubmote.bin").read_bytes()
    leaked = secrets_in(image)
    if leaked:
        sys.exit(f"{build / 'hubmote.bin'} contains {', '.join(leaked)} from secrets.env: "
                 "build it with -D HM_NO_SECRETS=1 (scripts/hm-web.ps1 does)")
    if config_of(build).get("HM_DEVELOPER") and not developer:
        sys.exit(f"{build} is a developer build (update server, devtools): build the release "
                 "(scripts/hm-web.ps1), or pass --developer for a test site")
    if public.is_file() and pem_body(public).split(b"\n")[0] not in image.replace(b"\r", b""):
        sys.exit(f"{build / 'hubmote.bin'} doesn't carry {public.name} as its update key "
                 "(a stale HM_UPDATE_KEY in its CMakeCache?): remotes would refuse the next release")
    board = board_of(build)
    folder = out / board["id"]
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for src, name, _ in PARTS:
        shutil.copyfile(build / src, folder / name)

    # The image's own version, so update.json matches what the remote will
    # run (version.txt is the same in normal builds).
    version = image_version(folder / "hubmote.bin") or (PROJECT / "version.txt").read_text(encoding="utf-8").strip()
    built = build_time(build)
    manifest = {
        "name": f"Hubmote for {board['name']}",
        "version": f"{version} ({built})" if built else version,
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": "ESP32-S3",
            "parts": [{"path": name, "offset": offset} for _, name, offset in PARTS],
        }],
    }
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    app = (folder / "hubmote.bin").read_bytes()
    print(f"{board['id']}: firmware {manifest['version']}" + (", untested board" if board["untested"] else ""))
    return {
        "id": board["id"],
        "version": version,
        "built": built,
        "entry": {
            "name": board["name"],
            "untested": board["untested"],
            "image": f"{board['id']}/hubmote.bin",
            "size": len(app),
            "sha256": hashlib.sha256(app).hexdigest(),
        },
    }


def add_companion(out: Path, folder: Path, any_key: bool):
    """Adds the companion APK from companion/build.ps1's output folder."""
    apk = folder / "companion.apk"
    info = json.loads((folder / "companion.json").read_text(encoding="utf-8-sig"))
    data = apk.read_bytes()
    for old in out.glob("companion*.apk"):
        old.unlink()
    shutil.copyfile(apk, out / "companion.apk")
    if info.get("key") != "release" and not any_key:
        print(f"companion {info['version']}: not signed with the project's key, so not offered to remotes")
        return
    name = f"companion-{info['version']}.apk"
    shutil.copyfile(apk, out / name)
    update_path = out / "update.json"
    update = json.loads(update_path.read_text(encoding="utf-8"))
    update["companion"] = {
        "version": info["version"],
        "code": int(info["code"]),
        "apk": name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    write_update(update_path, update)
    print(f"companion {info['version']} (code {info['code']}, {info['key']} key): {name}")


def openssl_path() -> str:
    """openssl on the PATH, or Git for Windows' own."""
    found = shutil.which("openssl")
    git = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Git" / "usr" / "bin" / "openssl.exe"
    return found or (str(git) if git.is_file() else "openssl")


def openssl(*args, data: bytes = b"") -> bytes:
    try:
        return subprocess.run([openssl_path(), *args], input=data, capture_output=True, check=True).stdout
    except FileNotFoundError:
        sys.exit("--sign needs the openssl command")
    except subprocess.CalledProcessError as e:
        sys.exit(f"openssl {args[0]}: {e.stderr.decode(errors='replace').strip()}")


def sign_update(out: Path, key: Path, public: Path):
    """Signs every board's image in out/update.json (see --sign)."""
    for pem in (key, public):
        if not pem.is_file():
            sys.exit(f"no {pem} (tools/web/make_update_key.py makes the key)")
    der = lambda pem_args: openssl("pkey", *pem_args, "-pubout", "-outform", "DER")
    if der(["-in", str(key)]) != der(["-pubin", "-in", str(public)]):
        sys.exit(f"{key.name} isn't the key whose public half is {public.name}")
    path = out / "update.json"
    update = json.loads(path.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as tmp:
        sig_file = Path(tmp) / "sig"
        for board, entry in update["boards"].items():
            message = f"hubmote-firmware\n{board}\n{update['version']}\n{entry['sha256']}\n".encode()
            sig = openssl("dgst", "-sha256", "-sign", str(key), data=message)
            sig_file.write_bytes(sig)
            openssl("dgst", "-sha256", "-verify", str(public), "-signature", str(sig_file), data=message)
            entry["sig"] = base64.b64encode(sig).decode()
    write_update(path, update)
    print(f"signed {len(update['boards'])} image{'s' if len(update['boards']) > 1 else ''} in {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", action="append",
                    help="a release build directory, one per board (repeatable); default build-web")
    ap.add_argument("--out", default="build-web/site", help="where the site goes")
    ap.add_argument("--companion", help="companion/build.ps1's output folder (companion.apk, companion.json)")
    ap.add_argument("--any-key", action="store_true", help="offer the companion even if not signed with the project's key")
    ap.add_argument("--no-firmware", action="store_true", help="only add the companion to an assembled site")
    ap.add_argument("--sign", metavar="KEY", help="sign the images in update.json with this private key (PEM)")
    ap.add_argument("--public", metavar="PEM", default="main/update_key.pem",
                    help="its public half, as the firmware has it (a test build's HM_UPDATE_KEY)")
    ap.add_argument("--developer", action="store_true", help="allow developer builds (a test site)")
    args = ap.parse_args()
    builds = [(PROJECT / b).resolve() for b in args.build or ["build-web"]]
    out = (PROJECT / args.out).resolve()
    companion = (PROJECT / args.companion).resolve() if args.companion else None

    sign = (PROJECT / args.sign).resolve() if args.sign else None
    if args.no_firmware:
        if not (companion or sign) or not (out / "update.json").is_file():
            sys.exit("--no-firmware needs --companion or --sign, and a site with update.json in --out")
        if companion:
            add_companion(out, companion, args.any_key)
        if sign:
            sign_update(out, sign, (PROJECT / args.public).resolve())
        return

    out.mkdir(parents=True, exist_ok=True)
    for old in [*out.glob("*.bin"), out / "manifest.json"]:  # an older site's, from before the board folders
        old.unlink(missing_ok=True)
    for old in out.glob("*/manifest.json"):  # boards of an earlier build
        shutil.rmtree(old.parent)
    shutil.copyfile(PROJECT / "web" / "index.html", out / "index.html")
    ewt = out / "esp-web-tools"
    if ewt.exists():
        shutil.rmtree(ewt)
    shutil.copytree(PROJECT / "web" / "esp-web-tools", ewt)

    # update.json: the page lists the boards from it, and a remote already
    # running compares the version with its own (Settings > About) and
    # installs its board's image.
    # Tested boards first, then by name: the page lists them in this order.
    public = (PROJECT / args.public).resolve()
    boards = sorted((add_board(build, out, args.developer, public) for build in builds),
                    key=lambda b: (b["entry"]["untested"], b["entry"]["name"]))
    versions = sorted({b["version"] for b in boards})
    if len(versions) > 1:
        sys.exit(f"the builds are of different versions: {', '.join(versions)}")
    ids = [b["id"] for b in boards]
    if len(set(ids)) < len(ids):
        sys.exit("two builds are for the same board")
    update = {
        "version": versions[0],
        "built": max(b["built"] for b in boards),
        "boards": {b["id"]: b["entry"] for b in boards},
    }
    write_update(out / "update.json", update)
    print(f"web installer: {out} ({len(boards)} board{'s' if len(boards) > 1 else ''})")
    if companion:
        add_companion(out, companion, args.any_key)
    if sign:
        sign_update(out, sign, (PROJECT / args.public).resolve())


if __name__ == "__main__":
    main()
