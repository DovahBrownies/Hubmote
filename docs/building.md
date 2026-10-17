# Building from source

For changing the firmware, or running a developer build with updates, logs
and screenshots over Wi-Fi. To just use the remote, the
[installer](getting-started.md) is enough.

The commands are for Windows (PowerShell 5.1, from `firmware\remote` in
a clone of the repository). PowerShell blocks scripts until you allow
them once: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`. On macOS
and Linux, use ESP-IDF's `idf.py` with the same options, and
`tools/hm_ota.py` and `tools/hm_netlog.py` for updates and logs over
Wi-Fi.

## Set up the PC

**macOS and Linux:** install ESP-IDF v5.4.4 with
[Espressif's guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32s3/get-started/linux-macos-setup.html)
(target `esp32s3`), then build with `idf.py` from `firmware/remote`.

**Windows:** install Git and Python 3.12, then ESP-IDF 5.4.4:

```powershell
git clone -b v5.4.4 --recursive https://github.com/espressif/esp-idf.git "$env:USERPROFILE\esp\v5.4.4\esp-idf"
$env:IDF_TOOLS_PATH = "C:\Espressif"
& "$env:USERPROFILE\esp\v5.4.4\esp-idf\install.ps1" esp32s3     # with Python 3.12 first on PATH
```

`scripts\hm-idf.ps1` finds ESP-IDF there (or in the Espressif installer's
`C:\Espressif\frameworks\esp-idf-v5.4.4`; `HM_IDF_PATH`,
`HM_IDF_TOOLS_PATH` and `HM_IDF_PYTHON_ENV` point elsewhere) and activates
it for its own process only. Don't add ESP-IDF to the PATH or run `idf.py`
yourself on Windows: a build without that activation can write the wrong
compiler into `build\CMakeCache.txt` and break later builds.

The companion app has its own tools and build: [companion/README.md](../companion/README.md).

## Secrets

Copy `secrets.env.example` to `secrets.env` (gitignored) and fill in the
lines you need:

```
# optional: Wi-Fi for a freshly flashed remote
WIFI_SSID="Your 2.4 GHz network"
WIFI_PASSWORD="your password"
# once the remote runs: Settings > About > Update token
OTA_TOKEN="the remote's token"
```

Comments go on lines of their own: a value runs to the end of its line.

- **Wi-Fi** lines are optional: without them the remote asks on its first
  start. With them a freshly flashed remote joins by itself. They go into
  `build/esp-idf/main/hm_secrets.h`, never `sdkconfig`; a build with them is
  for your own remote only (`make_site.py` refuses it).
  `python tools\check_secrets.py` checks them without printing them.
- **`OTA_TOKEN`** protects updates and the developer tools over Wi-Fi.
  A developer build makes its own the first time it joins Wi-Fi: copy it
  from **Settings > About > Update token** (Show; the row appears once it
  exists). The tools sign each request with it, so the token never crosses
  the network.

## Build and flash

```powershell
.\scripts\hm-idf.ps1 build
.\scripts\hm-idf.ps1 -p COM5 flash       # your board's port; check the MAC it prints
```

Use the board's native USB port. Several Espressif boards plugged in?
Device Manager (Ports) shows which COM port is which: confirm it before
flashing. "Port is busy" means a monitor still has it open.

**Another board:** give its id ([the boards](porting.md#the-boards)) and a
build folder of its own, for building, flashing and updates alike:

```powershell
.\scripts\hm-idf.ps1 -B build-4_3 -D HM_BOARD=waveshare_s3_touch_lcd_4_3 -D SDKCONFIG=build-4_3/sdkconfig build
.\scripts\hm-idf.ps1 -B build-4_3 -D HM_BOARD=waveshare_s3_touch_lcd_4_3 -D SDKCONFIG=build-4_3/sdkconfig -p COM5 flash
.\scripts\hm-ota.ps1 -Image build-4_3\hubmote.bin
```

The remote doesn't check which board an image was built for, so send
each board its own build.

## Updates over Wi-Fi

Developer builds take updates from a PC on the same network, while the
remote is awake:

```powershell
.\scripts\hm-idf.ps1 build
.\scripts\hm-ota.ps1                       # or -Remote <ip>, -Image build-4_3\hubmote.bin
```

The remote writes the image to its spare slot and restarts into it (about
20 s); a build that never reaches Wi-Fi is rolled back on its next
restart. Pairing, settings and learned remotes stay. `403` means
`OTA_TOKEN` doesn't match the remote's. Settings shows the running version
and build time in the chip at the top.

## Developer tools

Developer builds also serve their log over Wi-Fi
([troubleshooting](troubleshooting.md#logs)) and take screenshots and
simulated touches:

```powershell
python tools\devtools.py shot screen.png      # PNG with Pillow, else BMP
python tools\devtools.py tap 275 27           # x y in screen pixels: the Home tab on the 7B
python tools\devtools.py swipe 700 300 300 300
```

The UI simulator draws every screen at every board's size on the PC,
without a board: [sim/README.md](../firmware/remote/sim/README.md).

## Developer and release builds

A normal build is a developer build. The release build (the installer's)
adds `sdkconfig.release` (no updates from a PC, no log over Wi-Fi, no
screenshots) and is built with `-D HM_NO_SECRETS=1`, so nothing from
`secrets.env` goes in. A remote on a release build can't take
`hm-ota.ps1` updates until it gets a developer build over USB.

`.\scripts\hm-web.ps1 -Serve` builds the release firmware for every board
(`-Board <id>` for one) and serves the installer page at
http://localhost:8000.

## Options

`.\scripts\hm-idf.ps1 menuconfig` > *Hubmote*:

- **Board** (switch boards with `HM_BOARD` instead, above);
- **Google TV:** the Streamer's address (blank: found by name) and the
  name the TV shows while pairing;
- **Display:** Rotate 180° on the first start, brightness defaults;
- **Power:** the default dim, screen-off and turn-off times;
- **IR:** the pins, and a start-up self-check for the wiring;
- **Updates:** where releases are published, and the daily check;
- **Modules:** each one on or off;
- **Developer build:** the update server, screenshots, the Wi-Fi log, the
  start-up protocol self-test.

Settings on the remote override the defaults. Details per option:
[firmware/remote/README.md](../firmware/remote/README.md).

## Rules

- **Wrapper scripts only:** `hm-idf.ps1` for builds and flashing,
  `hm-monitor.ps1` and `hm-capture.ps1` for the USB log.
- **No redirection in PowerShell 5.1:** don't pipe the wrapper through
  `2>&1` or `*>`; capture with `Start-Process ... -RedirectStandardOutput
  -RedirectStandardError`.
- **`.ps1` files stay ASCII.**
- **`sdkconfig` is generated** from `sdkconfig.defaults`. After changing the
  defaults, move `sdkconfig` aside, rebuild, and diff the two.
- **Never move `nvs` or `storage`** in `partitions.csv` or
  `partitions_8mb.csv`: remotes would lose their pairing or learned
  remotes.

## Where to go next

- The code's layout and internals: [firmware/remote/README.md](../firmware/remote/README.md).
- Modules: [modules.md](modules.md) (`python tools/new_module.py <id> "<Name>"`
  starts one). Themes: [themes.md](themes.md). IR code sets:
  [ir-tvs.md](ir-tvs.md). Boards: [porting.md](porting.md).
- Fonts and icons: `tools/fonts/build_fonts.py` (its docstring has the
  setup).
- Releases: [releasing.md](releasing.md).
