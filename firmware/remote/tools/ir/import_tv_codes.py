#!/usr/bin/env python3
"""
Builds the remote's built-in TV code sets, main/tv_codes.c, from sources
whose licenses allow it (docs/ir-tvs.md):

  - SmartIR (MIT): codes/media_player/<id>.json, the TVs in SMARTIR_TVS.
  - Flipper-IRDB (CC0): TV files contributed after commit 2319685, the
    commit that added its license. Older files aren't covered and are
    left out, as are files whose content existed before it.

    python tools/ir/import_tv_codes.py            # use the cached clones
    python tools/ir/import_tv_codes.py --refresh  # pull both first

The clones live in tools/ir/.cache (gitignored). Raw captures are decoded
to NEC, SAMSUNG32, SIRC, RC5 or RC6 when re-encoding the decoded value
reproduces the capture; anything else is kept as RAW timings. Prints a
report of every set it writes.
"""

import argparse
import base64
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent
CACHE = HERE / ".cache"
OUT = PROJECT / "main" / "tv_codes.c"
NOTICE = PROJECT / "main" / "tv_codes_licenses.txt"

FLIPPER_URL = "https://github.com/Lucaslhm/Flipper-IRDB.git"
FLIPPER_LICENSE_COMMIT = "2319685"
SMARTIR_URL = "https://github.com/smartHomeHub/SmartIR.git"

# SmartIR media_player files that are TVs (the rest are soundbars,
# receivers, set-top boxes and projectors), with the brand to show.
SMARTIR_TVS = {
    1000: "Philips", 1001: "Philips", 1002: "Philips",
    1020: "Sony", 7020: "Sony",
    1040: "LG", 1042: "LG", 1043: "LG",
    1060: "Samsung", 1061: "Samsung", 1062: "Samsung", 1063: "Samsung", 1064: "Samsung", 1065: "Samsung",
    7060: "Samsung",
    1080: "Insignia", 1100: "Toshiba", 1180: "TCL", 1181: "TCL", 1240: "TurboX", 1260: "Thomson",
    1300: "OKI", 1340: "Bauhn", 1380: "Xiaomi", 1440: "Andersson", 1600: "Kivi", 1620: "Sharp",
    1640: "Noblex",
}

# Flipper-IRDB TV folders that aren't a TV brand.
FLIPPER_SKIP_DIRS = {"Hotels"}
FLIPPER_BRAND_NAMES = {"ContinentalEdison": "Continental Edison", "SEG": "SEG"}

# --- Keys --------------------------------------------------------------------------

KEYS = ["power", "power_on", "power_off", "vol_up", "vol_down", "mute", "ch_up", "ch_down", "source",
        "hdmi1", "hdmi2", "hdmi3", "hdmi4", "tv", "up", "down", "left", "right", "enter", "return",
        "home", "menu", "settings", "info", "exit", "play", "pause",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"]

ALIASES = {
    "power": ["power", "pwr", "powertoggle", "standby", "onoff"],
    "power_on": ["on", "poweron", "pon"],
    "power_off": ["off", "poweroff", "poff", "poweroffonly"],
    "vol_up": ["volup", "volumeup", "vol+", "volplus", "volumeplus"],
    "vol_down": ["voldn", "voldown", "volumedown", "vol-", "volminus", "volumeminus"],
    "mute": ["mute", "volumemute"],
    "ch_up": ["chnext", "chup", "channelup", "nextchannel", "ch+", "chplus", "programup", "pup"],
    "ch_down": ["chprev", "chdown", "chdn", "channeldown", "previouschannel", "ch-", "chminus", "programdown",
                "pdown"],
    "source": ["input", "source", "inputs", "sources", "inputsource", "sourceinput", "tvav"],
    "hdmi1": ["hdmi1"], "hdmi2": ["hdmi2"], "hdmi3": ["hdmi3"], "hdmi4": ["hdmi4"],
    "tv": ["tv", "dtv", "antenna", "atv"],
    "up": ["up", "uparrow", "arrowup", "cursorup", "navu"],
    "down": ["down", "downarrow", "arrowdown", "cursordown", "navd"],
    "left": ["left", "leftarrow", "arrowleft", "cursorleft", "navl"],
    "right": ["right", "rightarrow", "arrowright", "cursorright", "navr"],
    "enter": ["ok", "enter", "select", "confirm"],
    "return": ["back", "return"],
    "home": ["home", "smarthub", "homemenu", "smart"],
    "menu": ["menu"],
    "settings": ["settings", "setup"],
    "info": ["info", "information", "display"],
    "exit": ["exit"],
    "play": ["play", "playpause"],
    "pause": ["pause"],
}
for d in range(10):
    ALIASES[str(d)] = [str(d), f"num{d}", f"channel{d}", f"digit{d}", f"key{d}"]
ALIAS_TO_KEY = {alias: key for key, aliases in ALIASES.items() for alias in aliases}


def key_for(name):
    simple = re.sub(r"[\s_\.]+", "", name.strip().lower())
    return ALIAS_TO_KEY.get(simple)


# --- Protocols (the same frames as components/ir/ir_codec.c) -------------------------

def rev8(b):
    return int(f"{b & 0xFF:08b}"[::-1], 2)


def proto32_frame(lead_mark, lead_space, value):
    t = [lead_mark, lead_space]
    for bit in range(31, -1, -1):
        t += [560, 1690 if (value >> bit) & 1 else 560]
    return t + [560]


class Halves:
    """Builds mark/space timings from half-bit levels, like ir_codec.c."""

    def __init__(self):
        self.t, self.level = [], -1

    def put(self, level, us):
        if self.level < 0 and level == 0:
            return
        if self.t and self.level == level:
            self.t[-1] += us
        else:
            self.t.append(us)
            self.level = level

    def end(self):
        if self.t and self.level == 0:
            self.t.pop()
        return self.t


SIRC_BITS = {"SIRC12": 12, "SIRC15": 15, "SIRC20": 20}


def encode(proto, value, toggle=False):
    if proto == "NEC":
        return proto32_frame(9000, 4500, value)
    if proto == "SAMSUNG32":
        return proto32_frame(4500, 4500, value)
    if proto in SIRC_BITS:
        bits = SIRC_BITS[proto]
        t = [2400, 600]
        for i in range(bits):
            t.append(1200 if (value >> i) & 1 else 600)
            if i < bits - 1:
                t.append(600)
        return t
    if proto == "RC5":
        cmd, addr = value & 0x7F, (value >> 7) & 0x1F
        bits = 1 << 13 | (1 if cmd < 64 else 0) << 12 | int(toggle) << 11 | addr << 6 | (cmd & 0x3F)
        h = Halves()
        for i in range(13, -1, -1):
            one = (bits >> i) & 1
            h.put(0 if one else 1, 889)
            h.put(1 if one else 0, 889)
        return h.end()
    if proto == "RC6":
        h = Halves()
        h.put(1, 2666)
        h.put(0, 889)

        def bit(one, half):
            h.put(1 if one else 0, half)
            h.put(0 if one else 1, half)
        bit(1, 444)
        for _ in range(3):
            bit(0, 444)
        bit(int(toggle), 889)
        for i in range(15, -1, -1):
            bit((value >> i) & 1, 444)
        return h.end()
    raise ValueError(proto)


def near(v, nominal, pct=0.3, min_us=150):
    return abs(v - nominal) <= max(nominal * pct, min_us)


def frames_match(a, b):
    return len(a) == len(b) and all(near(x, y, 0.3, 200) for x, y in zip(a, b))


def decode_pulse_distance(t):
    if len(t) < 67:
        return None
    if near(t[0], 9000, 0.2) and near(t[1], 4500, 0.2):
        proto = "NEC"
    elif near(t[0], 4500, 0.2) and near(t[1], 4500, 0.2):
        proto = "SAMSUNG32"
    else:
        return None
    value = 0
    for i in range(32):
        value = value << 1 | (1 if t[3 + 2 * i] > 1125 else 0)
    return proto, value, False


def decode_sirc(t):
    if len(t) < 3 or not (near(t[0], 2400, 0.25) and near(t[1], 600, 0.35)):
        return None
    marks = t[2::2]
    proto = {12: "SIRC12", 15: "SIRC15", 20: "SIRC20"}.get(len(marks))
    if not proto:
        return None
    value = sum((1 if m > 900 else 0) << i for i, m in enumerate(marks))
    return proto, value, False


def units(t, unit):
    """Expands timings into a list of unit-length levels, starting with a mark."""
    out, level = [], 1
    for v in t:
        k = round(v / unit)
        if k < 1 or not near(v, k * unit, 0.3, 160):
            return None
        out += [level] * k
        level ^= 1
    return out


def decode_rc5(t):
    u = units(t, 889)
    if u is None:
        return None
    u = [0] + u  # S1's idle half
    if len(u) % 2:
        u.append(0)
    if len(u) != 28:
        return None
    bits = []
    for i in range(0, 28, 2):
        pair = (u[i], u[i + 1])
        if pair == (0, 1):
            bits.append(1)
        elif pair == (1, 0):
            bits.append(0)
        else:
            return None
    if bits[0] != 1:
        return None
    field, toggle = bits[1], bits[2]
    addr = int("".join(map(str, bits[3:8])), 2)
    cmd = int("".join(map(str, bits[8:14])), 2) | (0 if field else 64)
    return "RC5", addr << 7 | cmd, bool(toggle)


def decode_rc6(t):
    if len(t) < 3 or not (near(t[0], 2666, 0.2) and near(t[1], 889, 0.3)):
        return None
    u = units(t[2:], 444)
    if u is None:
        return None
    need = 2 + 3 * 2 + 4 + 16 * 2
    u += [0] * (need - len(u)) if len(u) < need else []
    if len(u) != need:
        return None
    if u[0:2] != [1, 0] or u[2:8] != [0, 1, 0, 1, 0, 1]:
        return None  # start bit 1, mode 000 only
    if u[8:12] == [1, 1, 0, 0]:
        toggle = True
    elif u[8:12] == [0, 0, 1, 1]:
        toggle = False
    else:
        return None
    value = 0
    for i in range(16):
        pair = u[12 + 2 * i:14 + 2 * i]
        if pair == [1, 0]:
            value = value << 1 | 1
        elif pair == [0, 1]:
            value = value << 1
        else:
            return None
    return "RC6", value, toggle


def decode(t):
    for dec in (decode_pulse_distance, decode_sirc, decode_rc6, decode_rc5):
        r = dec(t)
        if r and frames_match(encode(r[0], r[1], r[2]), t):
            return r[0], r[1]
    return None


def split_frames(t, gap_us=10000):
    frames, cur = [], []
    for i, v in enumerate(t):
        if i % 2 == 1 and v >= gap_us:
            if cur:
                frames.append(cur)
            cur = []
            continue
        if i % 2 == 0 or cur:
            cur.append(v)
    if cur:
        frames.append(cur)
    return [f for f in frames if len(f) >= 6]


def code_text(timings, carrier=38000):
    """Our text form for one capture: decoded if possible, else RAW (first frame)."""
    frames = split_frames(timings)
    if not frames:
        return None
    first = frames[0]
    if len(first) % 2 == 0:
        first = first[:-1]  # end on a mark
    d = decode(first)
    if d:
        return f"{d[0]} {d[1]:08X}"
    if len(first) > 512:
        return None
    return "RAW %d %s" % (carrier, " ".join(str(max(1, min(65535, round(v)))) for v in first))


# --- Sources -------------------------------------------------------------------------

def broadlink_timings(b64):
    try:
        data = base64.b64decode(b64.strip() + "=" * (-len(b64.strip()) % 4))
    except ValueError:
        return None
    if len(data) < 4 or data[0] != 0x26:
        return None
    length = data[2] | data[3] << 8
    body = data[4:4 + length]
    out, i = [], 0
    while i < len(body):
        v = body[i]
        i += 1
        if v == 0:
            if i + 1 >= len(body):
                break
            v = body[i] << 8 | body[i + 1]
            i += 2
        out.append(v * 269 / 8.192)  # Broadlink ticks are 269/8192 ms
    return out


def esphome_timings(raw):
    if isinstance(raw, str):  # some files keep the array as a string
        try:
            raw = json.loads(raw)
        except ValueError:
            return None
    return [abs(v) for v in raw] if isinstance(raw, list) else None


def capture_text(value, encoding):
    if encoding == "Base64":
        t = broadlink_timings(value)
    elif encoding == "Raw":
        t = esphome_timings(value)
    else:
        return None
    return code_text(t) if t else None


def lenient_json(path):
    return json.loads(re.sub(r",(\s*[}\]])", r"\1", path.read_text(encoding="utf-8")))


SMARTIR_MAIN = {"off": "power_off", "on": "power_on", "power": "power", "previousChannel": "ch_down",
                "nextChannel": "ch_up", "volumeDown": "vol_down", "volumeUp": "vol_up", "mute": "mute"}


def smartir_sets(root, commit):
    sets = []
    for num, brand in sorted(SMARTIR_TVS.items()):
        path = root / "codes" / "media_player" / f"{num}.json"
        d = lenient_json(path)
        enc = d.get("commandsEncoding", "")
        cmds = d.get("commands", {})
        keys = {}
        for name, value in cmds.items():
            if name == "sources" and isinstance(value, dict):
                for sname, svalue in value.items():
                    k = key_for(sname)
                    if k and k not in keys and isinstance(svalue, (str, list)):
                        text = capture_text(svalue, enc)
                        if text:
                            keys[k] = text
                continue
            k = SMARTIR_MAIN.get(name) or key_for(name)
            if k and k not in keys and isinstance(value, (str, list)):
                text = capture_text(value, enc)
                if text:
                    keys[k] = text
        if keys.get("power_on") and keys.get("power_on") == keys.get("power_off"):
            keys["power"] = keys.pop("power_on")
            keys.pop("power_off")
        models = ", ".join(d.get("supportedModels", []))
        sets.append({"id": f"smartir-{num}", "brand": brand, "label": models,
                     "source": f"SmartIR {num}.json @{commit} (MIT)", "keys": keys})
    return sets


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, encoding="utf-8",
                          errors="replace", check=True).stdout


def flipper_eligible(repo):
    """TV files whose content arrived after the license commit."""
    old_paths = set(git(repo, "ls-tree", "-r", "--name-only", FLIPPER_LICENSE_COMMIT).splitlines())
    old_blobs = {line.split()[2] for line in git(repo, "ls-tree", "-r", FLIPPER_LICENSE_COMMIT).splitlines()}
    out = []
    for line in git(repo, "ls-tree", "-r", "HEAD", "TVs").splitlines():
        meta, path = line.split("\t", 1)
        if not path.endswith(".ir") or path in old_paths or meta.split()[2] in old_blobs:
            continue
        # Moved here after the license commit? Then it's older content.
        added = git(repo, "log", "--follow", "--diff-filter=A", "--format=%H", "--", path).split()
        if added and subprocess.run(["git", "-C", str(repo), "merge-base", "--is-ancestor",
                                     FLIPPER_LICENSE_COMMIT, added[-1]]).returncode == 0:
            out.append(path)
    return sorted(out)


def parse_hex_bytes(s):
    return [int(b, 16) for b in s.split()]


def flipper_code(sig):
    t = sig.get("type")
    if t == "raw":
        data = [int(v) for v in sig.get("data", "").split()]
        return code_text(data, int(float(sig.get("frequency", 38000))))
    proto = sig.get("protocol", "")
    a = parse_hex_bytes(sig.get("address", "00 00 00 00"))
    c = parse_hex_bytes(sig.get("command", "00 00 00 00"))
    if proto == "NEC":
        v = rev8(a[0]) << 24 | rev8(~a[0]) << 16 | rev8(c[0]) << 8 | rev8(~c[0])
        return f"NEC {v:08X}"
    if proto == "NECext":
        v = rev8(a[0]) << 24 | rev8(a[1]) << 16 | rev8(c[0]) << 8 | rev8(c[1])
        return f"NEC {v:08X}"
    if proto == "Samsung32":
        v = rev8(a[0]) << 24 | rev8(a[0]) << 16 | rev8(c[0]) << 8 | rev8(~c[0])
        return f"SAMSUNG32 {v:08X}"
    if proto == "RC5":
        return f"RC5 {(a[0] & 0x1F) << 7 | (c[0] & 0x3F):08X}"
    if proto == "RC5X":
        return f"RC5 {(a[0] & 0x1F) << 7 | ((c[0] & 0x3F) | 0x40):08X}"
    if proto == "RC6":
        return f"RC6 {a[0] << 8 | c[0]:08X}"
    if proto in ("SIRC", "SIRC15", "SIRC20"):
        name = {"SIRC": "SIRC12", "SIRC15": "SIRC15", "SIRC20": "SIRC20"}[proto]
        addr = a[0] | a[1] << 8
        return f"{name} {(c[0] & 0x7F) | addr << 7:08X}"
    return None  # Kaseikyo, RCA, NEC42 ...: no encoder here


def flipper_sets(repo, commit):
    sets = []
    for path in flipper_eligible(repo):
        parts = path.split("/")
        if parts[1] in FLIPPER_SKIP_DIRS:
            continue
        brand = FLIPPER_BRAND_NAMES.get(parts[1], parts[1].replace("_", " "))
        text = (repo / path).read_text(encoding="utf-8", errors="replace")
        keys, sig = {}, None
        for line in text.splitlines() + ["#"]:
            line = line.strip()
            if line.startswith("name:"):
                sig = {"name": line[5:].strip()}
            elif sig is not None and ":" in line and not line.startswith("#"):
                k, v = line.split(":", 1)
                sig[k.strip()] = v.strip()
            elif line.startswith("#") and sig:
                key = key_for(sig["name"])
                if key and key not in keys:
                    code = flipper_code(sig)
                    if code:
                        keys[key] = code
                sig = None
        stem = Path(path).stem
        label = re.sub(r"^" + re.escape(parts[1]) + r"[\s_-]*", "", stem, flags=re.I).replace("_", " ").strip()
        slug = re.sub(r"[^a-z0-9]+", "-", f"{parts[1]}-{stem}".lower()).strip("-")
        sets.append({"id": f"flipper-{slug}"[:40], "brand": brand, "label": label or stem,
                     "source": f"Flipper-IRDB {path} @{commit} (CC0)", "keys": keys})
    return sets


# --- Output --------------------------------------------------------------------------

def merge_sets(sets):
    """Drops sets whose codes another set of the same brand already has."""
    keep = []
    for s in sorted(sets, key=lambda s: -len(s["keys"])):
        dup = None
        for k in keep:
            if k["brand"] == s["brand"] and all(k["keys"].get(key) == code for key, code in s["keys"].items()):
                dup = k
        if dup:
            if s["label"] and s["label"] not in dup["label"]:
                dup["label"] = (dup["label"] + ", " + s["label"]).strip(", ")
            continue
        keep.append(s)
    return keep


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_c(sets, commits):
    lines = [
        "/*",
        " * Built-in TV code sets. Generated by tools/ir/import_tv_codes.py on " +
        datetime.date.today().isoformat() + " from:",
        f" *   SmartIR {commits['smartir']} (MIT), https://github.com/smartHomeHub/SmartIR",
        f" *   Flipper-IRDB {commits['flipper']} (CC0; files contributed after {FLIPPER_LICENSE_COMMIT}),",
        " *   https://github.com/Lucaslhm/Flipper-IRDB",
        " * Every set names its source file; the licenses are in tv_codes_licenses.txt.",
        " * Don't edit by hand: change the importer and run it again (docs/ir-tvs.md).",
        " */",
        '#include "tv_codes.h"',
        "",
    ]
    for i, s in enumerate(sets):
        lines.append(f"static const tv_key_t k_set{i}[] = {{")
        for key in KEYS:
            if key in s["keys"]:
                lines.append(f"    {{{c_str(key)}, {c_str(s['keys'][key])}}},")
        lines.append("};")
    lines.append("")
    lines.append("const tv_set_t tv_sets[] = {")
    for i, s in enumerate(sets):
        label = s["label"] if len(s["label"]) <= 60 else s["label"][:57] + "..."
        lines.append(f"    {{{c_str(s['id'])}, {c_str(s['brand'])}, {c_str(label)},")
        lines.append(f"     {c_str(s['source'])}, k_set{i}, {len(s['keys'])}}},")
    lines.append("};")
    lines.append("")
    lines.append("const size_t tv_set_count = sizeof(tv_sets) / sizeof(tv_sets[0]);")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def write_notice(sets, smartir, commits):
    files = sorted({s["source"] for s in sets})
    text = [
        "Licenses of the built-in TV codes (main/tv_codes.c), written by",
        "tools/ir/import_tv_codes.py. See docs/ir-tvs.md.",
        "",
        "Code sets taken from:",
        *("  " + f for f in files),
        "",
        f"Flipper-IRDB ({commits['flipper']}): only files contributed after commit {FLIPPER_LICENSE_COMMIT},",
        "which the project releases under CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/).",
        "",
        f"SmartIR ({commits['smartir']}), https://github.com/smartHomeHub/SmartIR, under this license:",
        "",
        (smartir / "LICENSE").read_text(encoding="utf-8").strip(),
        "",
    ]
    NOTICE.write_text("\n".join(text), encoding="utf-8", newline="\n")


def ensure_clone(name, url, full, refresh):
    path = CACHE / name
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        args = ["git", "clone", "-q"] + ([] if full else ["--depth", "1"]) + [url, str(path)]
        subprocess.run(args, check=True)
    elif refresh:
        subprocess.run(["git", "-C", str(path), "pull", "-q"], check=True)
    return path, git(path, "rev-parse", "--short", "HEAD").strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--refresh", action="store_true", help="pull both sources before importing")
    args = ap.parse_args()

    flipper, flipper_commit = ensure_clone("Flipper-IRDB", FLIPPER_URL, True, args.refresh)
    smartir, smartir_commit = ensure_clone("SmartIR", SMARTIR_URL, False, args.refresh)
    sets = smartir_sets(smartir, smartir_commit) + flipper_sets(flipper, flipper_commit)
    sets = [s for s in sets if len(s["keys"]) >= 2 and any(k in s["keys"] for k in ("power", "power_on"))]
    sets = merge_sets(sets)
    sets.sort(key=lambda s: (s["brand"].lower(), -len(s["keys"]), s["id"]))
    commits = {"smartir": smartir_commit, "flipper": flipper_commit}
    write_c(sets, commits)
    write_notice(sets, smartir, commits)

    raw_total = 0
    for s in sets:
        protos = sorted({code.split()[0] for code in s["keys"].values()})
        raw = sum(1 for code in s["keys"].values() if code.startswith("RAW"))
        raw_total += raw
        print(f"{s['brand']:18s} {len(s['keys']):2d} keys  {','.join(protos):22s} {s['label'][:44]}")
    brands = sorted({s["brand"] for s in sets})
    print(f"\n{len(sets)} sets, {len(brands)} brands, {raw_total} raw codes -> {OUT.relative_to(PROJECT)}")


if __name__ == "__main__":
    sys.exit(main())
