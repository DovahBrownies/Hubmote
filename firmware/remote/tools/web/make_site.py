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

Paths are relative to firmware/remote. The version comes from the image
(or version.txt), the build time from the build's stamp
(esp-idf/main/build_stamp.h).

It refuses an app image that contains a value from secrets.env (a Wi-Fi
password, the update token): a site is for others to download. Build with
HM_NO_SECRETS, as scripts/hm-web.ps1 does. Only the key's name is printed.
"""

import argparse
import hashlib
import json
import re
import shutil
import sys
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


def board_of(build: Path) -> dict:
    """The board a build is for: its id from the build's configuration, its
    name and whether it is untested from its header."""
    config = build / "config" / "sdkconfig.json"
    board_id = json.loads(config.read_text(encoding="utf-8")).get("HM_BOARD_ID") if config.is_file() else None
    if not board_id:
        sys.exit(f"no HM_BOARD_ID in {config} (build first)")
    header = (PROJECT / "components" / "board" / "boards" / f"{board_id}.h").read_text(encoding="utf-8")
    name = re.search(r'#define BOARD_NAME\s+"([^"]+)"', header)
    return {
        "id": board_id,
        "name": name.group(1) if name else board_id,
        "untested": re.search(r"#define BOARD_UNTESTED\s+1\b", header) is not None,
    }


def add_board(build: Path, out: Path) -> dict:
    """Copies one build's images into out/<id>/ with their manifest;
    returns the board's id, version, build time and update.json entry."""
    missing = [src for src, _, _ in PARTS if not (build / src).is_file()]
    if missing:
        sys.exit(f"not in {build}: {', '.join(missing)} (build first)")
    leaked = secrets_in((build / "hubmote.bin").read_bytes())
    if leaked:
        sys.exit(f"{build / 'hubmote.bin'} contains {', '.join(leaked)} from secrets.env: "
                 "build it with -D HM_NO_SECRETS=1 (scripts/hm-web.ps1 does)")
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", action="append",
                    help="a release build directory, one per board (repeatable); default build-web")
    ap.add_argument("--out", default="build-web/site", help="where the site goes")
    ap.add_argument("--companion", help="companion/build.ps1's output folder (companion.apk, companion.json)")
    ap.add_argument("--any-key", action="store_true", help="offer the companion even if not signed with the project's key")
    ap.add_argument("--no-firmware", action="store_true", help="only add the companion to an assembled site")
    args = ap.parse_args()
    builds = [(PROJECT / b).resolve() for b in args.build or ["build-web"]]
    out = (PROJECT / args.out).resolve()
    companion = (PROJECT / args.companion).resolve() if args.companion else None

    if args.no_firmware:
        if not companion or not (out / "update.json").is_file():
            sys.exit("--no-firmware needs --companion and a site with update.json in --out")
        add_companion(out, companion, args.any_key)
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
    boards = sorted((add_board(build, out) for build in builds),
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


if __name__ == "__main__":
    main()
