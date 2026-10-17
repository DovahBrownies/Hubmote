# TVs over IR

The remote controls one TV over IR (infrared, the invisible light TV
remotes use), next to the Google TV Streamer. It handles the TV's power,
inputs, channels, its own menus and, if you want, its volume. This page
shows how to add your TV. Its second half, from
[Where the codes come from](#where-the-codes-come-from), is for
developers.

The TV gets its codes from a **built-in code set** for its brand, plus any
keys **learned** from the TV's own remote. Learned keys win over built-in
ones.

## Adding a TV

1. Open the switcher by tapping the remote tab in the top bar, or open
   **Settings > Remotes**.
2. Tap **Add a TV**, then pick your TV's brand.
3. The remote shows **code set 1 of N**. Point the remote at the TV and
   tap **Send**. It usually sends Mute.
4. If the TV reacts, tap **It worked**. If it doesn't, tap **Next set**
   and try again.

If no set works, or your brand isn't listed, tap **Learn from its
remote**. It's on the brand list, and it replaces **Next set** on the last
set. You get an empty TV and learn each key from the TV's own remote
([Missing keys](#missing-keys)). This needs a board with an IR receiver.

### The TV's panel

The panel has power, HDMI 1-4 and Source, channels, Back, Home, Menu,
Settings, Info and Exit, and volume. Power is On and Off where the TV has
separate codes, otherwise one Power key. While the panel is open, the
touchpad's arrows and OK go to the TV.

### Missing keys

Keys without a code are dimmed. On boards with an IR receiver, you can
learn them:

1. Tap the dimmed key.
2. Point the TV's own remote at the IR receiver and press that key three
   times.
3. Test the key, then save it.

**Keys**, at the top of the panel, lists every key, including the arrows,
OK and digits. Use it to learn or relearn any of them.

### Changing or removing the TV

**Change**, at the top of the panel, picks another brand or set, or
removes the TV.

> [!IMPORTANT]
> Choosing a new set, or **Learn from its remote**, forgets the keys you
> learned.

### TV volume

**Settings > Remotes > TV volume** chooses where volume keys go:

- **Google TV** (the default) sends them to the Streamer. The Streamer
  passes them to the TV over HDMI-CEC, which lets HDMI devices control
  each other. This needs the Streamer's volume control set to the TV
  ([getting started](getting-started.md#3-volume)).
- **TV's IR** sends them to the TV directly, for TVs without CEC.

## Where the codes come from

The built-in codes come only from sources whose licenses allow reuse:

| Source | License | TVs |
|---|---|---|
| [SmartIR](https://github.com/smartHomeHub/SmartIR) `codes/media_player` | MIT | About 25 |
| [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) `TVs/` | CC0, for files contributed after commit 2319685 | Two so far |

- **SmartIR's TVs** include Samsung, LG, Sony, Philips, TCL, Toshiba,
  Sharp, Insignia, Thomson, Xiaomi and others. They have power (separate
  On and Off in some), volume, mute, channels, inputs and digits. A few
  have arrows and menus.
- **Flipper-IRDB's TVs** are a Continental Edison and a Grundig.

These sources are not used:

- **Flipper-IRDB files from before its license**, which is most of it.
  The project only licensed contributions from August 2025 on.
- **Flipper's own `tv.ir`**, because it's under GPL-3.0.
- **probonopd/irdb**, because its license asks for notices and free units.
- **The LIRC database**, because it has no license.

The codes ship in `firmware/remote/main/tv_codes.c`, which names each
set's source file. The licenses, including SmartIR's MIT notice, are in
`main/tv_codes_licenses.txt`. The importer below generates both files.

## Updating the code sets

Run the importer in PowerShell, from the repository's root folder:

```powershell
cd firmware\remote
python tools\ir\import_tv_codes.py            # uses tools\ir\.cache
python tools\ir\import_tv_codes.py --refresh  # pulls both sources first
```

The importer:

1. Clones both sources into `tools/ir/.cache` (gitignored).
2. Keeps only the SmartIR files listed in `SMARTIR_TVS`: the TVs, not
   soundbars or receivers.
3. Keeps only the Flipper-IRDB TV files whose content arrived after the
   license commit. It checks the git history for this, renames included.
4. Maps button names to the remote's keys.
5. Decodes captures to NEC, SAMSUNG32, SIRC, RC5 or RC6 when re-encoding
   reproduces them, and keeps them as RAW otherwise.
6. Merges sets that duplicate each other, and skips sets without a Power
   key.
7. Writes `tv_codes.c` and `tv_codes_licenses.txt`, and prints a report.

Rebuild the firmware afterwards.

### Adding a TV for everyone

1. Contribute the TV's codes to SmartIR or Flipper-IRDB.
2. For SmartIR, add the file's number and brand to `SMARTIR_TVS` in
   `tools/ir/import_tv_codes.py`. The importer finds Flipper-IRDB TV files
   by itself.
3. Run the importer.

## Keys and protocols

The keys are `power`, `power_on`, `power_off`, `vol_up`, `vol_down`,
`mute`, `ch_up`, `ch_down`, `source`, `hdmi1`-`hdmi4`, `tv`, `up`, `down`,
`left`, `right`, `enter`, `return`, `home`, `menu`, `settings`, `info`,
`exit`, `play`, `pause` and `0`-`9`.

The remote sends these protocols (`components/ir/ir_codec.c`, with host
tests in `components/ir/host_test`):

- NEC, including NECext
- SAMSUNG32
- Sony SIRC: 12, 15 and 20 bit, always three frames
- Philips RC5 and RC5X
- RC6 mode 0
- RAW timings, for anything else

Built-in RC5 and RC6 codes flip their toggle bit on each press.

Learning decodes NEC and SAMSUNG32 only. So other learned keys (Sony, RC5,
RC6, Sharp, Panasonic) are stored as RAW and repeat exactly. The importer
keeps raw captures as RAW, but skips Flipper keys in protocols it can't
send (Kaseikyo, RCA, NEC42).

Codes are written as text:

```text
SAMSUNG32 E0E040BF
SIRC12 00000095
RC5 0000000C
RAW 38000 9000 4500 ...
```

## Storage

The TV's settings are in NVS namespace `ctrl`:

| Key | Holds |
|---|---|
| `irtv_set` | The set's id, for example `smartir-1060` |
| `irtv_name` | The TV's name |
| `irtv_vol` | 1 = volume over IR |

Learned keys are in the learned-remote store with the id `tv`
(`/storage/ir/tv.json`). It's hidden from the list of learned remotes.
