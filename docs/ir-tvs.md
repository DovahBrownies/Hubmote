# TVs over IR

The remote controls one TV over infrared, next to the Google TV Streamer:
power, inputs, channels, the TV's own menus and, if you want, its volume.
The TV gets its codes from a **built-in code set** for its brand, plus any
keys **learned** from the TV's own remote. Learned keys win.

## Adding a TV

Open the remote tab's switcher (or Settings > Remotes) and tap **Add a
TV**:

1. Pick the brand.
2. The remote shows **code set 1 of N**. Point the remote at the TV and
   tap **Send** (usually Mute). If the TV reacts, tap **It worked**;
   otherwise **Next set**.
3. No set works (the button appears after the last one), or the brand
   isn't listed? Tap **Learn from its remote** (boards with an IR
   receiver): you get an empty TV and learn each key from the TV's own
   remote.

The TV's panel then has power (On and Off where the TV has separate codes,
otherwise one Power key), HDMI 1-4 and Source, channels, Back, Home, Menu,
Settings, Info and Exit, and volume. The touchpad's arrows and OK go to the
TV while its panel is open.

**Missing keys** are dimmed. On boards with an IR receiver, tap one to
learn it: press that key three
times on the TV's own remote, pointed at the IR receiver, then test and
save. **Keys** (top of the panel) lists every key, including the arrows,
OK and digits, so any of them can be learned or relearned. **Change**
picks another brand or set, or removes the TV; choosing a new set, or
Learn from its remote, forgets learned keys.

**Volume:** Settings > Remotes > TV volume. **Google TV** (the default)
sends volume keys to the Streamer, which passes them to the TV over
HDMI-CEC; this needs the Streamer's volume control set to the TV
([getting started](getting-started.md#3-volume)). **TV's IR** sends them to the TV directly, for TVs
without CEC.

## Where the codes come from

Only sources whose licenses allow reuse:

| Source | License | What it gives |
|---|---|---|
| [SmartIR](https://github.com/smartHomeHub/SmartIR) `codes/media_player` | MIT | About 25 TVs: Samsung, LG, Sony, Philips, TCL, Toshiba, Sharp, Insignia, Thomson, Xiaomi and others. Power (separate On and Off in some), volume, mute, channels, inputs, digits; a few have arrows and menus |
| [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) `TVs/` | CC0, for files contributed after commit 2319685 | Two TVs so far (Continental Edison, Grundig) |

Not used: Flipper-IRDB files from before its license (most of it: the
project only licensed contributions from August 2025 on), Flipper's own
`tv.ir` (GPL-3.0), probonopd/irdb (its license asks for notices and free
units), the LIRC database (no license).

The codes ship in `firmware/remote/main/tv_codes.c`, which names each
set's source file; the licenses, including SmartIR's MIT notice, are in
`main/tv_codes_licenses.txt`. Both are generated.

## Updating the code sets

```powershell
cd firmware\remote
python tools\ir\import_tv_codes.py            # uses tools\ir\.cache
python tools\ir\import_tv_codes.py --refresh  # pulls both sources first
```

The importer clones both sources into `tools/ir/.cache` (gitignored),
keeps only the SmartIR files listed in `SMARTIR_TVS` (the TVs, not
soundbars or receivers) and the Flipper-IRDB TV files whose content
arrived after the license commit (it checks git history, renames
included), maps button names to the remote's keys, decodes captures to
NEC, SAMSUNG32, SIRC, RC5 or RC6 when re-encoding reproduces them (RAW
otherwise), merges sets that duplicate each other, and writes
`tv_codes.c` and `tv_codes_licenses.txt`, and prints a report. Rebuild afterwards.

To add a TV for everyone, contribute its codes to SmartIR or
Flipper-IRDB. For SmartIR, add the file's number and brand to
`SMARTIR_TVS`; Flipper-IRDB TV files are found by themselves. Then run the
importer (it skips sets without a Power key).

## Keys and protocols

Keys: `power`, `power_on`, `power_off`, `vol_up`, `vol_down`, `mute`,
`ch_up`, `ch_down`, `source`, `hdmi1`-`hdmi4`, `tv`, `up`, `down`, `left`,
`right`, `enter`, `return`, `home`, `menu`, `settings`, `info`, `exit`,
`play`, `pause`, `0`-`9`.

Protocols the remote sends (`components/ir/ir_codec.c`, host tests in
`components/ir/host_test`): NEC (including NECext), SAMSUNG32, Sony SIRC
(12, 15 and 20 bit, always three frames), Philips RC5 and RC5X, RC6 mode 0,
and RAW timings for anything else. Built-in RC5 and RC6 codes flip their
toggle bit on each press. Learning decodes NEC and SAMSUNG32 only, so other
learned keys (Sony, RC5, RC6, Sharp, Panasonic) are stored as RAW and
repeat exactly. The importer keeps raw captures as RAW, but skips Flipper
keys in protocols it can't send (Kaseikyo, RCA, NEC42). Codes are
written as text: `SAMSUNG32 E0E040BF`, `SIRC12 00000095`, `RC5 0000000C`,
`RAW 38000 9000 4500 ...`.

## Storage

- NVS `ctrl`: `irtv_set` (the set's id, e.g. `smartir-1060`), `irtv_name`,
  `irtv_vol` (1 = volume over IR).
- Learned keys: the learned-remote store with id `tv`
  (`/storage/ir/tv.json`), hidden from the learned remotes list.
