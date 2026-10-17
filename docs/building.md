# Building from source

This page is for developers who want to change the firmware, or run a
developer build with updates, logs and screenshots over Wi-Fi. If you only
want to use the remote, the [installer](getting-started.md) is enough.

The commands are for Windows, in PowerShell 5.1. Run them from
`firmware\remote` in a clone of the repository, unless a step says
otherwise. On macOS and Linux, use ESP-IDF's `idf.py` with the same
options, and `tools/hm_ota.py` and `tools/hm_netlog.py` for updates and
logs over Wi-Fi.

On this page:

- [Set up the PC](#set-up-the-pc)
- [Secrets](#secrets)
- [Build and flash](#build-and-flash)
- [Updates over Wi-Fi](#updates-over-wi-fi)
- [Developer tools](#developer-tools)
- [Developer and release builds](#developer-and-release-builds)
- [Options](#options)
- [Rules](#rules)
- [Where to go next](#where-to-go-next)

## Set up the PC

### Windows

These setup commands work from any folder.

1. Install Git and Python 3.12.
2. Allow PowerShell to run scripts. PowerShell blocks them until you run
   this once:

   ```powershell
   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
   ```

3. Install ESP-IDF 5.4.4. Put Python 3.12 first on the PATH, then run:

   ```powershell
   git clone -b v5.4.4 --recursive https://github.com/espressif/esp-idf.git "$env:USERPROFILE\esp\v5.4.4\esp-idf"
   $env:IDF_TOOLS_PATH = "C:\Espressif"
   & "$env:USERPROFILE\esp\v5.4.4\esp-idf\install.ps1" esp32s3     # with Python 3.12 first on PATH
   ```

`scripts\hm-idf.ps1` finds ESP-IDF in that folder. It also finds the one
that Espressif's installer puts in `C:\Espressif\frameworks\esp-idf-v5.4.4`.
To point it somewhere else, set `HM_IDF_PATH`, `HM_IDF_TOOLS_PATH` and
`HM_IDF_PYTHON_ENV`. The script activates ESP-IDF for its own process
only.

> [!IMPORTANT]
> On Windows, don't add ESP-IDF to the PATH or run `idf.py` yourself. A
> build without the script's activation can write the wrong compiler into
> `build\CMakeCache.txt`, and that breaks later builds.

### macOS and Linux

Install ESP-IDF v5.4.4 with
[Espressif's guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32s3/get-started/linux-macos-setup.html),
for the target `esp32s3`. Then build with `idf.py` from `firmware/remote`.

### The companion app

The companion app has its own tools and build. See
[companion/README.md](../companion/README.md).

## Secrets

`secrets.env` holds the optional Wi-Fi details and the update token for
the PC tools. It is gitignored.

1. In `firmware\remote`, copy `secrets.env.example` to `secrets.env`.
2. Remove the `#` in front of the lines you need, and fill them in. A
   filled-in file looks like this:

   ```text
   # optional: Wi-Fi for a freshly flashed remote
   WIFI_SSID="Your 2.4 GHz network"
   WIFI_PASSWORD="your password"
   # once the remote runs: Settings > About > Update token
   OTA_TOKEN="the remote's token"
   ```

Put comments on lines of their own, because a value runs to the end of
its line.

- **The Wi-Fi lines** are optional. Without them, the remote asks for
  Wi-Fi on its first start. With them, a freshly flashed remote joins by
  itself. They go into `build/esp-idf/main/hm_secrets.h`, never into
  `sdkconfig`. A build with them is for your own remote only (see
  [What the remote keeps](security.md#what-the-remote-keeps) for why).
  After a build, you can check them without printing them:

  ```powershell
  python tools\check_secrets.py
  ```

- **`OTA_TOKEN`** protects updates and the developer tools over Wi-Fi. A
  developer build makes its own token the first time it joins Wi-Fi. Copy
  it from **Settings > About > Update token** by tapping **Show**. The row
  appears once the token exists. The tools sign each request with the
  token, so the token itself never crosses the network.

## Build and flash

Build the firmware, then flash it over USB:

```powershell
.\scripts\hm-idf.ps1 build
.\scripts\hm-idf.ps1 -p COM5 flash       # your board's port; check the MAC it prints
```

Replace `COM5` with your board's port. Check the MAC address in the flash
output to make sure you flashed the board you meant.

- Use the board's native USB port.
- If several Espressif boards are plugged in, Device Manager shows which
  COM port is which, under **Ports**. Confirm the port before flashing.
- If flashing fails with "Port is busy", a monitor still has the port
  open.

### Another board

The 7B is the default. To build for another board, give the build the
board's id and a build folder of its own:
[Building for a board](porting.md#building-for-a-board) has the commands.
Use the same folder for updates over Wi-Fi, from `firmware/remote/`:

```powershell
.\scripts\hm-ota.ps1 -Image build-<id>\hubmote.bin
```

Replace `build-<id>` with that board's build folder.

> [!WARNING]
> The remote doesn't check which board an image was built for. Send each
> board its own build.

## Updates over Wi-Fi

Developer builds take updates from a PC on the same network, while the
remote is awake. Build, then send the image:

```powershell
.\scripts\hm-idf.ps1 build
.\scripts\hm-ota.ps1                       # or -Remote <ip>, -Image build-4_3\hubmote.bin
```

By default, `hm-ota.ps1` sends `build\hubmote.bin` to `hubmote.local`.
`-Remote <ip>` sends it to the remote's IP address instead, and `-Image`
sends another build, such as another board's.

What happens next:

- The remote writes the image to its spare slot and restarts into it.
  This takes about 20 s.
- If the new build never reaches Wi-Fi, the remote rolls it back on its
  next restart.
- Pairing, settings and learned remotes stay.
- The chip at the top of Settings shows the running version and build
  time.

If the update fails with `403`, the `OTA_TOKEN` in `secrets.env` doesn't
match the remote's token.

## Developer tools

Developer builds also serve their log over Wi-Fi (see
[troubleshooting](troubleshooting.md#logs)). They take screenshots and
simulated touches too:

```powershell
python tools\devtools.py shot screen.png      # PNG with Pillow, else BMP
python tools\devtools.py tap 275 27           # x y in screen pixels: the Home tab on the 7B
python tools\devtools.py swipe 700 300 300 300
```

`swipe` takes the start point's x and y, then the end point's.

The UI simulator draws every screen at every board's size on the PC,
without a board. See [sim/README.md](../firmware/remote/sim/README.md).

## Developer and release builds

A normal build is a developer build. The release build is the one the
installer flashes. It differs in two ways:

- **It adds `sdkconfig.release`.** That turns off updates from a PC, the
  log over Wi-Fi and screenshots.
- **It is built with `-D HM_NO_SECRETS=1`.** Nothing from `secrets.env`
  goes in.

> [!NOTE]
> A remote on a release build can't take `hm-ota.ps1` updates. It needs a
> developer build over USB first.

To build the release firmware for every board and serve the installer
page at http://localhost:8000, run:

```powershell
.\scripts\hm-web.ps1 -Serve
```

To build one board only, add `-Board <id>`.

## Options

The firmware's defaults are in menuconfig. Open it, then go to
**Hubmote**:

```powershell
.\scripts\hm-idf.ps1 menuconfig
```

- **Board.** Switch boards with `HM_BOARD` instead (see
  [Another board](#another-board)).
- **Google TV.** The Streamer's address, and the name the TV shows while
  pairing. Leave the address blank to find the Streamer by name.
- **Display.** Rotate 180° on the first start, and the brightness
  defaults.
- **Power.** The default dim, screen-off and turn-off times.
- **IR.** The pins, and a start-up self-check for the wiring.
- **Updates.** Where releases are published, and the daily check.
- **Modules.** Turns each module on or off.
- **Developer build.** The update server, screenshots, the Wi-Fi log, and
  the start-up protocol self-test.

Settings on the remote override these defaults. Each option is described
in [firmware/remote/README.md](../firmware/remote/README.md).

## Rules

- **Wrapper scripts only.** Use `hm-idf.ps1` for builds and flashing, and
  `hm-monitor.ps1` or `hm-capture.ps1` for the USB log.
- **No redirection in PowerShell 5.1.** Don't pipe the wrapper through
  `2>&1` or `*>`. To capture its output, use `Start-Process ...
  -RedirectStandardOutput -RedirectStandardError`.
- **`.ps1` files stay ASCII.**
- **`sdkconfig` is generated** from `sdkconfig.defaults`. After changing
  the defaults, move `sdkconfig` aside, rebuild, and diff the two.
- **Never move `nvs` or `storage`** in `partitions.csv` or
  `partitions_8mb.csv`. If you do, remotes lose their pairing or learned
  remotes.

## Where to go next

- **The code's layout and internals:**
  [firmware/remote/README.md](../firmware/remote/README.md).
- **Modules:** [modules.md](modules.md). To start one, run
  `python tools/new_module.py <id> "<Name>"` from `firmware/remote`.
- **Themes:** [themes.md](themes.md).
- **IR code sets:** [ir-tvs.md](ir-tvs.md).
- **Boards:** [porting.md](porting.md).
- **Fonts and icons:** `tools/fonts/build_fonts.py`. Its docstring has the
  setup.
- **Releases:** [releasing.md](releasing.md).
