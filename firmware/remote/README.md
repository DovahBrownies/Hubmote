# Hubmote firmware

This page is for developers working on the remote's firmware. It covers
the configuration, what the firmware stores, how its parts work and where
they are in the code. The firmware uses ESP-IDF 5.4.4 and LVGL 9, on
Waveshare's ESP32-S3 touch boards.

- To set up a remote, see [getting started](../../docs/getting-started.md).
  To use it, see [using the remote](../../docs/using.md).
- To build, flash and update your own, see [building](../../docs/building.md).
- Open problems are in the [GitHub issues](https://github.com/DovahBrownies/Hubmote/issues).

On this page:

- [How builds and updates work](#how-builds-and-updates-work)
- [Menuconfig options](#menuconfig-options)
- [What's stored](#whats-stored)
- [Companion app (optional)](#companion-app-optional)
- [IR](#ir)
- [Layout of the code](#layout-of-the-code)
- [Other internals](#other-internals)
- [Tested on hardware](#tested-on-hardware)
- [What's next](#whats-next)

## How builds and updates work

The commands are in [docs/building.md](../../docs/building.md). This
section explains how the pieces work.

### Updates over Wi-Fi

`main/ota.c` takes updates from a PC.

- `hm-ota.ps1` posts `build\hubmote.bin` to `/ota` on port 8080. It signs
  the request with `OTA_TOKEN` from `secrets.env`
  ([below](#secrets-and-the-update-token)).
- The remote checks the signature and the image's SHA-256. It then writes
  the image to the spare app slot and restarts into it.
- Rollback is on. If an image doesn't reach Wi-Fi, the previous one
  replaces it on the next restart. Reaching Wi-Fi calls `ota_mark_good()`,
  which logs `ota: new firmware confirmed`.
- `GET /version` shows the running build and slot. Settings shows the
  version and build time in the top bar's chip.

### Version

`version.txt` sets the version. Between releases it holds the coming
release with `-dev`, such as `0.1.1-dev` after 0.1.0
([releasing](../../docs/releasing.md#a-release)). ESP-IDF reads it at
configure time, so run `.\scripts\hm-idf.ps1 reconfigure` after you change
it.

### Flash layout

`partitions.csv` sets the flash layout.

- `ota_0` and `ota_1` are 6 MB each, and `otadata` picks the one that
  boots. A USB flash always boots `ota_0`.
- Boards with 8 MB of flash use `partitions_8mb.csv`. It has `nvs` at the
  same offset, 3 MB slots, and a smaller `storage` at 0x320000.

> [!WARNING]
> Never move `nvs` (at 0x9000) or `storage` (at 0x620000, or 0x320000 in
> `partitions_8mb.csv`). They stay put so that updates and USB flashes keep
> the pairing, the settings and the learned remotes.

### Console

The console is on the native USB connector (USB-Serial/JTAG), so GPIO43
and GPIO44 (UART0) are free for IR. The UART connector only shows the ROM
banner.

### Secrets and the update token

`secrets.env` holds the secrets and is gitignored. It has `OTA_TOKEN` and,
optionally, `WIFI_SSID` and `WIFI_PASSWORD`.

- **The update token.** Developer builds make it on the remote when the
  update server first starts, once Wi-Fi is up. It is 20 letters and
  digits from the radio's random source. The remote keeps it in NVS
  (namespace `ota`, `main/ota.c`), and it is never built into an image.
  **Settings > About** shows it behind **Show**. Copy it into `secrets.env`
  as `OTA_TOKEN`, where `hm-ota.ps1`, `hm_ota.py` and `devtools.py` read it.
- **Signed requests.** The tools sign each request instead of sending the
  token (`ota_authorized()` in `main/ota.h`, `tools/hm_auth.py`). A tool
  first fetches a one-time nonce from `GET /nonce`. It then sends
  `X-OTA-Nonce` and `X-OTA-Auth`, an HMAC-SHA256 of the nonce, the method,
  the path and the body's SHA-256 (`X-OTA-Body`). A device posing as
  `hubmote.local`, or anyone reading the traffic, learns nothing it could
  use for another request. Each wrong signature costs a second.
- **Wi-Fi.** You normally set Wi-Fi on the remote, on its first start or in
  **Settings > Wi-Fi**. The remote keeps it in NVS (namespace `wifi`). It
  uses the `WIFI_*` values from `secrets.env` only while NVS has no
  network, and saves them there once they connect (`main/net.c`). A network
  chosen on the remote is also saved only after it connects.
- **How Wi-Fi values get into the build.** `main/CMakeLists.txt` turns the
  `WIFI_*` lines (never `OTA_TOKEN`) into
  `build/esp-idf/main/hm_secrets.h`, so nothing lands in `sdkconfig`. The
  next build picks up any edits.
- **Checking them.** `python tools/check_secrets.py` checks the Wi-Fi values
  against what was compiled, without printing them.

### sdkconfig

`sdkconfig` is generated from `sdkconfig.defaults`. After you change the
defaults, move `sdkconfig` aside as a backup and rebuild, then diff the
two. Kconfig changes in `main/Kconfig.projbuild` need
`.\scripts\hm-idf.ps1 reconfigure`.

### Developer and release builds

A normal build (`hm-idf.ps1 build`) is a developer build. The release build
is `sdkconfig.defaults` plus `sdkconfig.release`, which turns
`HM_DEVELOPER` off. So a release build has no update server, no log over
Wi-Fi, no screenshots or touches and no self-test.

`hm-web.ps1` makes the release build for the web installer. It builds in
`build-web/<board>/`, from a fresh `sdkconfig` each time, and with
`HM_NO_SECRETS`, so nothing from `secrets.env` goes in.

A remote running a release build can't take `hm-ota.ps1` updates. Flash a
developer build over USB first.

## Menuconfig options

Open menuconfig with `.\scripts\hm-idf.ps1 menuconfig`. The `Hubmote` menu
has the options below. The last item, the Android TV Remote client's
menu, is outside it.

- **Board** (`HM_BOARD`) picks which board in `components/board/boards/`
  is built, with the board-specific defaults (screen size, IR pins).
  - The default is the Waveshare ESP32-S3-Touch-LCD-7B. Waveshare's 7, 5B,
    5, 4.3, 4.3B and 4.3C are there too, untested.
  - Build those with `-D HM_BOARD=<id>` from a new sdkconfig. That also
    brings in their `<id>.defaults`, such as the 7's flash size.
  - [docs/porting.md](../../docs/porting.md) has the table, the commands
    and how to add a board.
- **Google TV** sets the Streamer's address and the name the TV shows
  while pairing. The address is optional. If it is blank, the remote finds
  the Streamer with mDNS.
- **Display** has the rotation and brightness.
  - **Turn the screen and touch 180 degrees on first start**
    (`HM_DISPLAY_ROTATE_180`) is off by default, which is right for a bare
    board. The enclosure needs it on. It is only a default. The remote
    saves it on its first start (NVS `ctrl`/`turn`), and after that
    **Settings > Screen > Rotate 180°** changes it at once, without a
    restart.
  - **How the turn works.** `main/display.c` turns the picture while
    sending it to the panel. The driver keeps no framebuffer and asks for each
    10-line bounce buffer. The copy reads LVGL's framebuffer backwards,
    because a 180-degree turn is the pixel stream reversed. LVGL still
    draws double-buffered in direct mode, so nothing tears and drawing
    costs the same. `board_touch_init()` mirrors touch, and the touch
    driver's mirror flags do it when the setting changes.
  - **Brightness.** The normal level is only a default, and
    **Settings > Screen** saves it in NVS. The dimmed level
    (`HM_BRIGHTNESS_DIM`) is fixed, and capped at the normal level.
- **Power** sets the dim, screen-off and turn-off (deep sleep) times.
  These are only defaults. **Settings > Screen** changes them and keeps
  them in NVS.
  - Dim and screen-off have a second pair for USB power:
    `HM_DIM_AFTER_CHARGING_S` (5 min) and `HM_SCREEN_OFF_AFTER_CHARGING_S`
    (30 min), where 0 means never. They apply while `battery_update()`
    reports USB or no cell.
  - The remote goes into deep sleep after 12 idle hours by default
    (`HM_SLEEP_AFTER_H`, 0 = never), on any power.
- **IR** sets the pins and the start-up self-check.
  - The 7B's default pins are GPIO44 for the transmitter and GPIO6 for the
    receiver. An optional pin can power the receiver.
  - **The start-up self-check** (`HM_IR_BOOT_CHECK`, off by default) is
    for checking the wiring. About 1 s after start-up, the transmitter
    sends a burst that matches no protocol while the receiver listens. The
    log line `ir_ctl: IR check ...` gives the number of frames caught.
    Frames that differ mean a weak or garbled signal, or that the two are
    too close together.
- **Updates** sets where releases are published (`HM_UPDATE_URL`) and
  whether the remote checks by itself (`HM_UPDATE_AUTO`). Release builds
  have these options too, because this is how they update.
  - `HM_UPDATE_URL` defaults to the GitHub Pages site. It takes https
    only, but developer builds also take http://, for a site served from a
    PC.
  - `HM_UPDATE_AUTO` checks a minute after Wi-Fi comes up, then daily.
- **Modules** has one option per module (`modules/*/Kconfig.module`). A
  module that is off isn't compiled at all.
- **Developer build** (`HM_DEVELOPER`) is on by default, and off in release
  builds. Its options:
  - **Updates over Wi-Fi from a PC** (`HM_LOCAL_OTA`): the update server on
    port 8080 (`HM_OTA_PORT`) that `hm-ota.ps1` sends to, with the update
    token.
  - **Screenshot and simulated touches** (`HM_DEV_TOOLS`, needs the update
    server): `GET /screenshot` (a 24-bit BMP of the screen),
    `POST /tap?x=&y=&ms=` and `POST /swipe?x1=&y1=&x2=&y2=&ms=`, with the
    same token. `python tools/devtools.py shot|tap|swipe` wraps them. PNG
    output needs Pillow.
  - **Log over Wi-Fi** (`HM_NETLOG`): TCP port 2323 (`HM_NETLOG_PORT`),
    holding the last 32 KB (`main/netlog.c`). The Wi-Fi driver is limited
    to warnings, so the network name stays out of the log.
  - **Protocol self-test at start-up** (`HM_ATV_SELFTEST`): checks the
    Android TV Remote encoder and parsers against golden vectors.
  - **Battery preview** (`HM_BATTERY_PREVIEW`, off, for test builds only).
    On battery, the shown level counts down from 35% to 10%, 1% every 2 s.
    This checks the red level (below 30%) and the low-battery card (below
    20%) without draining the cell.
- **Android TV Remote (atv_remote)** is outside the Hubmote menu, under
  Component config (from `components/atv_remote/Kconfig`). It sets the
  client task's stack (`ATV_REMOTE_TASK_STACK_SIZE`, 8192, always in
  internal RAM), its priority (5) and its core (-1, any).

## What's stored

The first start is described in
[getting started](../../docs/getting-started.md). In short, the remote:

1. Makes its pairing key and certificate (RSA-2048). This takes up to a
   minute, once.
2. Asks for Wi-Fi.
3. Finds the TVs with mDNS. It skips this if an address is configured or
   remembered.
4. Pairs.

When the TV's address changes, the remote finds it again by its mDNS name.

### What's saved in NVS

| Namespace | Keys | What they hold |
|---|---|---|
| `atv` | `key`, `cert` | The pairing key and certificate |
| `atv` | `host` | The TV's address |
| `atv` | `tv_key` | The hash of the TV's public key, pinned at pairing |
| `ctrl` | `tv_mdns` | The TV's mDNS name |
| `ctrl` | `app_order`, `hidden_apps` | Home's app order and hidden apps |
| `ctrl` | `media_on`, `media_off` | Apps given playback keys (they played something, or the user chose) and apps the user took them from |
| `ctrl` | `bright`, `dim_s`, `off_s`, `sleep_h` | **Settings > Screen** |
| `ctrl` | `dim_c`, `off_c` | **Settings > Screen**, for charging |
| `ctrl` | `turn` | **Rotate 180°** |
| `ctrl` | `theme` | The theme |
| `ctrl` | `irtv_set`, `irtv_name`, `irtv_vol` | The IR TV |
| `ctrl` | `home` | The Home tab's module cards: their order and sizes (**Arrange Home**) |
| `wifi` | `ssid`, `pass` | The network's name and password |
| `ota` | `token` | The update token |
| `companion` | `token` | The token from pairing with companion 0.4+ |
| `hub` | `tz` | The time zone (none: Automatic) |
| `hub` | `utc_off` | The last UTC offset Weather reported, so local time is right from start-up while the zone is Automatic |
| `hub_off` | The module's id | Modules switched off in Settings |
| `m_<module>` | Set by the module | Each module's own settings, and the values of its declared settings (Settings > Modules) |

## Companion app (optional)

`companion/` in the repo is a small Android TV app for the Streamer.
[docs/companion.md](../../docs/companion.md) explains how to install it,
and its [README](../../companion/README.md) covers the API and how to build
it. The firmware talks to it through `components/companion`, over HTTP on
port 6470 of the TV. When the companion is reachable:

- **Home** lists every app installed on the TV with its own icon, and opens
  them through the companion. Without the companion, Home shows the five
  built-in apps.
- **Arranging apps** uses Home's **Edit** button. A tap hides or shows an
  app; press and hold to drag it. The order and the hidden apps are saved
  in NVS ([above](#whats-saved-in-nvs)), as package names between
  newlines. They apply to the built-in apps too. Apps that were never
  placed come after the placed ones, in alphabetical order.
- **Icons** take about 43 KB per app in PSRAM. They are also cached in
  `/storage/icons` (`companion_set_cache_dir()`):
  - one file per app, holding both sizes deflated with the ROM's miniz
    (about 4 KB per app);
  - checked against the app's `updated` value;
  - at most 640 KB, and only for apps still on the TV.

  So after a restart or waking up, the icons load in under a second, and
  only new or updated apps are downloaded. The deflate runs on a
  short-lived task with a 10 KB stack (it uses about 7 KB), so the
  companion task's stack stays at 6 KB.
- **Pairing** (companion 0.4+). Until the remote pairs, the companion
  answers 401, and the client reports "pairing needed".
  **Settings > Remotes** then shows **Pair companion**. It asks the
  companion to show a code on the TV (`/pair/start`) and opens the setup
  screen's code entry, with **Cancel**. The typed code goes to `/pair`. The
  token that comes back is saved in NVS and sent as
  `Authorization: Bearer` on every request.
- **Its version and updates** (companion 0.5+). On each connection, the
  client reads `/status`: the version code, the name, and whether the
  companion can update itself. `update.json` may carry the release's
  companion, with its version, code, APK name, size and SHA-256
  (`main/update.c`).
  - When the release's code is higher, **Settings > Remotes** shows both
    versions. If the companion can update itself, it also shows
    **Update companion**.
  - **Update companion** makes the controller send the APK's address (next
    to `update.json`) to `/update`. The controller follows the companion's
    `{"update":...}` events. It counts the update as done when the
    companion comes back with the new version.
- **The keyboard** types through the companion whenever the companion
  reports a focused field (`/text`, `/enter`). This works in apps that
  never tell the remote protocol about their fields, such as browsers.
  - While typing that way, the remote ignores the TV's late echoes of the
    field.
  - Number fields get the number pad alone, and passwords show as `*`
    until **Show** is tapped. So does the Wi-Fi password
    (`ui_kb_open_password()`).
  - The text clears when a different field gets focus.
  - While the remote's keyboard is open, the Streamer's on-screen keyboard
    is hidden (`/keyboard`).
  - Some apps, such as Plex, draw their own keyboard. The companion reports
    it as a field and presses its keys. See "On-screen keyboards" in the
    companion README's [API section](../../companion/README.md#api).
- **The keyboard's modes** (`ui_keyboard.c`). The mode is picked when the
  keyboard opens. While it is open, `ui_kb_follow()` switches the mode when
  the field or the app changes.
  - `UI_KB_FIELD` is for the TV's or the companion's field, and sends the
    whole text.
  - `UI_KB_SEARCH` is for Google TV's Home, and sends the text to Google TV
    search (`/search`).
  - `UI_KB_KEYS` is for anything else. It sends characters as protocol text
    (`UI_ACT_TYPE`), which Android hands to apps as virtual-keyboard keys.
    Backspace and Enter go as key presses.
  - `UI_KB_LOCAL` is for text for the remote itself.
- **Typing on Home** (`UI_KB_SEARCH`). When Google TV's launcher is in
  front and no field has focus, the keyboard sends its text to Google TV's
  search through the companion (`/search`). It sends the text 1.5 s after
  typing pauses, once there are 3 or more characters, or at once on Enter.
  The values are `SEARCH_PAUSE_US` and `SEARCH_MIN_CHARS` in
  `main/ctrl_companion.c`.
- **Messages on the TV** (companion 0.7+). `hub_notify()` with a module's
  **Show on the TV** setting on sends the text to `/notify`, which shows it
  at the top right of the TV for a few seconds.
- **The speed test** (companion 0.7+). `hub_tv_speedtest()` starts it with
  `/speedtest`; the progress and the result come back as `{"speed":...}`
  events. The test itself runs on the TV (the companion README has the
  details).

## IR

Wiring, and the MOSFET stage for room range, are in
[docs/hardware.md](../../docs/hardware.md). The pins are in menuconfig
(`Hubmote` > IR). A pin set to `-1` turns sending or learning off, and the
UI then leaves out what can't work (`ui_set_ir_caps()`).

- **The TV** has built-in codes in `main/tv_codes.c`, generated by
  `tools/ir/import_tv_codes.py` ([docs/ir-tvs.md](../../docs/ir-tvs.md)).
  Keys learned from the TV's remote are stored as the remote `tv`.
- **HDMI switch inputs** are four inputs, each with a name and a learned
  code. They are stored as the learned remote "HDMI switch", in button
  slots 0-3 (`main/ir_ctl.c`, `components/ui/ui_inputs.c`).
- **Learned remotes** are stored on LittleFS as `/storage/ir/<id>.json`.
- **Learned remotes and the learn flow** hide the rail and the touchpad,
  so nothing goes to the Streamer by mistake.

## Layout of the code

### The application (`main/`)

- **`main/main.c`** is the bring-up: NVS, cJSON's PSRAM allocator, the
  board, LVGL, the display and touch.
- **`main/display.c`** is the LVGL display. Two PSRAM framebuffers are
  swapped between frames and copied into the panel's bounce buffers,
  turned 180 degrees if set.
- **`main/battery.c`** works out the battery level and USB power from the
  cell voltage. Steady readings mean USB; ripple means battery.
- **`main/controller.c`** is the controller task. One queue takes UI
  actions and TV, Wi-Fi, companion and update events. It handles the
  Streamer's keys and IR actions, what starts when Wi-Fi comes up, Home's
  layout in NVS, and the heap and key logs. Its parts share
  `main/ctrl_internal.h`, and each part handles its own UI actions:
  - **`main/ctrl_setup.c`** runs the first-run flow: identity, Wi-Fi (the
    network picker), finding the TV (mDNS or the menuconfig address),
    pairing, and finding the TV again by name when its address changes.
  - **`main/ctrl_companion.c`** handles the companion: the app list on
    Home, pairing, its version and updates, and typing (companion field,
    Google TV search on Home, protocol text, Enter). Its requests to the
    companion run in order on a worker task of their own (`comp_req`), so
    the controller never waits for the network.
  - **`main/ctrl_screen.c`** handles **Settings > Screen** and
    **Appearance** in NVS, dimming and screen off (longer on USB), turning off after idle
    hours, the theme restart and the battery readout.
- **`main/net.c`** is the Wi-Fi station: the network from NVS (or from
  `secrets.env` the first time), scanning and joining another network. It
  also sets up the `hubmote.local` mDNS name.
- **`main/netlog.c`** is the log over Wi-Fi: a ring buffer plus a TCP
  server on port 2323, for a client that answers its challenge with the
  update token (an HMAC; the protocol is at the top of the file).
- **`main/ota.c`** takes updates over Wi-Fi from a PC, in developer builds:
  `/ota`, `/version` and `/nonce` on port 8080, with requests signed with
  the update token. It also does the rollback confirmation.
- **`main/update.c`** takes updates from the releases.
  - It reads `update.json` at `HM_UPDATE_URL`, with the release's
    companion if there is one.
  - It compares versions. A release beats its own pre-releases.
  - It offers a release only if its board's entry carries a valid
    signature (`sig`, ECDSA P-256 over the board, version and SHA-256),
    checked with the public key in `main/update_key.pem`. Test builds can
    point `HM_UPDATE_KEY` at another public key.
  - It downloads its board's image (`HM_BOARD_ID`) into the spare slot,
    with size and SHA-256 checks.
  - It checks a minute after Wi-Fi comes up, then daily, and on request
    (**Settings > About**).
- **`main/build_info.c`** and **`main/build_stamp.cmake`** stamp the build
  time on every build, because ESP-IDF's own date rarely changes. Settings
  and `/version` show it.
- **`main/devtools.c`** adds the developer tools to the update server:
  `/screenshot`, `/tap` and `/swipe` (`HM_DEV_TOOLS`).
- **`main/tv_ir.c`** and **`main/tv_codes.c`** are the IR TV. The first
  holds its code set (NVS `irtv_*`) and learned keys
  (`/storage/ir/tv.json`). The second holds the built-in sets, generated,
  with `tv_codes_licenses.txt`.
- **`main/storage.c`** mounts LittleFS at `/storage`. That is the
  `storage` partition, which holds the learned remotes, the modules' files
  and the icon cache. A partition it can't mount is formatted (a new or
  erased board), except right after an update: then the remote rolls back
  to the firmware before, which could read it.
- **`main/ir_ctl.c`** routes keys to IR and runs Add a TV, the
  learn-and-save flow, the Inputs page and the start-up IR check.

### Components and modules

- **`components/board`** is the board support.
  - `board.h` is what the firmware needs from a board.
  - Each board has `boards/<id>.h` (its panel, expander pins and what it
    can do) and `<id>.defaults`. You choose one with `-D HM_BOARD=<id>`,
    and menuconfig > Hubmote > Board shows it.
  - Waveshare's boards share `waveshare_s3_rgb.c` (I2C, GT911, the RGB
    panel, deep sleep). They add `waveshare_ch32v003.c` (the 7B and 4.3C:
    backlight PWM, battery ADC) or `waveshare_ch422g.c` (on/off
    backlight).
  - The 7B's panel is an ST7262 at 14 MHz, with bench-proven timings.
  - [docs/porting.md](../../docs/porting.md) explains how to add a board.
- **`components/atv_remote`** is the Android TV Remote v2 client: identity,
  mDNS discovery, pairing (port 6467) and the control session (port 6466).
  Its `tools` and `proto` folders hold the generators and the vendored
  androidtvremote2 protos they work from:
  - `gen_keycodes.py` writes `atv_keycodes.h`, with the standard library
    only.
  - `gen_golden.py` writes the self-test's `atv_golden.h`. It needs
    protobuf and grpcio-tools in a venv; see its docstring.
- **`components/companion`** is the client for the companion app: pairing
  and its token, its version, the event stream (field type and id, update
  progress), the app list and icons, launch, set text, Enter, Google TV
  search (`/search`), keyboard hiding, asking it to update, and what is
  playing (`companion_media()`, 0.6+).
- **`components/ui`** holds the LVGL 9 screens. The main files are below.
- **`components/hub`** is the module platform: the registry
  (`HUB_MODULE()`), events, glances, one HTTP worker, per-module NVS and
  files, SNTP time, the time zone, and actions on the TV for modules
  ([docs/modules.md](../../docs/modules.md)).
- **`components/kv`** reads and writes one NVS value per call (`kv.h`).
  Settings in `main/`, the companion's token and the hub's module storage
  use it; `atv_remote` keeps its own NVS code.
- **`components/ir`** sends and learns IR on the RMT peripheral. It
  encodes NEC, Samsung32, SIRC, RC5 and RC6, and decodes NEC and
  Samsung32. It also keeps the learned remotes on LittleFS. Its host tests
  are in `host_test/` (`run_host_tests.ps1`).
- **`modules/`** has one component per module, each with a
  `Kconfig.module` and a README. There are `weather/` (Open-Meteo), with
  a host test in `test/`, `sleep_timer/`, `screen_time/`, `guest_wifi/`
  and `speed_test/`.

### The UI (`components/ui`)

| File | What it is |
|---|---|
| `ui.c` | View switching |
| `ui_topbar.c` | The tabs and glance |
| `ui_switcher.c` | The switcher sheet |
| `ui_dash.c` | The Home tab and module host |
| `ui_tv_home.c` | Google TV's Home: the app grid and Edit |
| `ui_app.c` | The app context panel |
| `ui_rail.c` | The Streamer's keys |
| `ui_pad.c` | The D-pad and touchpad |
| `ui_keyboard.c` | The keyboard |
| `ui_setup.c` | First run and code entry |
| `ui_settings.c` | Settings |
| `ui_ir.c` | The TV and learned remotes |
| `ui_tv_setup.c` | Add a TV |
| `ui_learn.c` | The learn flow |
| `ui_inputs.c` | The Inputs page |
| `ui_wifi.c` | The Wi-Fi network picker |
| `ui_status.c` | Loading, error and empty states |
| `include/ui_kit.h`, `ui_kit.c` | The part modules use |
| `ui_themes.c` | The themes |
| `include/ui_size.h` | Scales pixel sizes to the board's screen (`UI_S()`) |
| `ui_mem.c` | Puts LVGL's heap in PSRAM |
| `fonts/h<screen height>/` | The fonts for each screen height |

There are more files besides these.

### Tools, scripts and the simulator

- **`sim/`** is the UI simulator. It draws the real UI on a PC at every
  board's screen size and saves PNGs ([sim/README.md](sim/README.md)).
- **`tools/fonts/build_fonts.py`** generates the fonts for every screen
  height, plus `ui_fonts.h` and `ui_icons.h`. Its docstring has the setup:
  a venv with fontTools, then `npm install`. **`preview_fonts.py`** next to
  it draws a preview of them.
- **`tools/web/make_site.py`** assembles the installer site and
  `update.json` from release builds, one folder per board, optionally with
  the companion. CI and `hm-web.ps1` use it. With `--sign` and `--public`
  it signs each board's image for updates; it refuses developer builds
  unless given `--developer`.
- **`tools/web/make_update_key.py`** makes the update signing key pair
  once ([docs/releasing.md](../../docs/releasing.md)).
- **`tools/time/gen_zones.py`** builds `components/hub/hub_tz.c`, the
  time zones and their rules, from the IANA tz database (needs the
  `tzdata` Python package).
- **`tools/ir/import_tv_codes.py`** builds `main/tv_codes.c` from SmartIR
  (MIT) and CC0 Flipper-IRDB files ([docs/ir-tvs.md](../../docs/ir-tvs.md)).
- **`tools/check_themes.py`** checks every theme's contrast
  ([docs/themes.md](../../docs/themes.md)).
- **`tools/check_secrets.py`** checks `secrets.env` against the compiled
  header without printing it.
- **`tools/new_module.py`** and **`tools/module_template/`** make
  `modules/<id>/` for a new module. The UI simulator compiles a module made
  from the template on every run (`--into`), so CI notices when a change
  to the module API leaves it behind.
- **`tools/devtools.py`** takes screenshots and sends taps and swipes over
  Wi-Fi. It signs them with the token from `secrets.env` and never prints
  the token.
- **`tools/hm_auth.py`** reads the update token from `secrets.env` and
  signs requests. `hm_ota.py` and `devtools.py` share it.
- **`tools/hm_ota.py`** and **`tools/hm_netlog.py`** do the same updates and
  log over Wi-Fi from any OS, in standard-library Python. On macOS and
  Linux, `idf.py build flash monitor` covers the rest.
- **`scripts/`** holds the Windows PowerShell scripts:
  - `hm-idf.ps1` builds and flashes over USB;
  - `hm-ota.ps1` updates over Wi-Fi;
  - `hm-netlog.ps1` shows the log over Wi-Fi;
  - `hm-monitor.ps1` and `hm-capture.ps1` show the log over USB.
- **`web/`** and **`scripts/hm-web.ps1`** make the web installer.
  - `web/` has `index.html` and ESP Web Tools 10.4.0 in
    `web/esp-web-tools`. That copy is vendored by
    `tools/web/vendor_esp_web_tools.py`, with one change: a failed install
    offers **Close**.
  - `hm-web.ps1` builds every board's release firmware
    ([above](#developer-and-release-builds)) and the site
    (`build-web/site`). Add `-Serve` to serve it on localhost.

## Other internals

- **Volume** goes through the Streamer, over the remote protocol, on every
  screen. The exception is when **Settings > Remotes > TV volume** is set
  to the TV's IR. A held volume button is a real key hold: down, then up on
  release. The Streamer passes it on over HDMI-CEC.
- **System screens.** The TV reports the screensaver (`...tv.dreamx`), the
  Assistant (`katniss`), system panels and keyboards as the foreground
  app. `ui_app_is_system_screen()` in `components/ui/ui_apps.c` lists
  them. The controller ignores those reports, so the remote stays on the
  app underneath. The log shows `app: ... (system screen, ignored)`. If
  another one shows up as an app, add its package there.
- **Built-in apps.** The tiles and deep links are in
  `components/ui/ui_apps.c`. They show as letter tiles, because no logos
  are shipped; the companion's list brings the real icons. To add an app,
  add its package, name and deep link.
- **Memory.** The logs print `ctrl: heap ...` lines at start-up and on
  connect. Internal RAM should stay well above 50 KB. It is typically
  about 116 KB after start-up and 89 KB once connected to the TV, with a
  low of 78 KB. **Settings > About** shows the current figure. Large or
  numerous buffers belong in PSRAM.

## Tested on hardware

These were tested on a 7B with a Google TV Streamer and a TV over
HDMI-CEC:

- the display, touch, backlight, and Rotate 180° (both ways, kept after a
  restart);
- Wi-Fi, discovery, pairing and reconnecting;
- volume through HDMI-CEC, including holding;
- app detection, and launching from the Google TV app grid;
- the keyboard in Google TV fields, a browser, an app's own on-screen
  keyboard (Plex), YouTube and Google TV search; the number pad; hiding
  the Streamer's keyboard;
- the companion: the app list, pairing (0.4) and its version in Settings;
- updates and logs over Wi-Fi from a PC, updates from a release site
  (served from a PC), and the web installer;
- IR transmit, receive and learning, and the Inputs page;
- the battery level, charging indicator, deep sleep and touch wake;
- the top bar, switcher and Home tab; Weather (location search, forecast,
  card, glance); Settings and themes.

Not checked on hardware yet:

1. **The built-in TV code sets** at a real TV.
2. **The release build** in use (it was flashed from the web installer),
   and the first-run Wi-Fi picker with no saved network.
3. **Companion updates** where Android lets apps install: the dialog, and
   the update without one.
4. **Deep links:** each built-in app tile, without the companion.
5. **Weather over days:** the refresh timing, the cache after a restart,
   and offline behavior.

## What's next

Planned work and open problems are in the
[GitHub issues](https://github.com/DovahBrownies/Hubmote/issues). On the hardware side, these are next:

- A MOSFET transmit stage for IR range ([docs/hardware.md](../../docs/hardware.md)).
- Wake on pickup, with an accelerometer such as a BMA400. But GPIO6, the
  only free wake-capable pin, holds the IR receiver.
- The power-test measurements ([firmware/power-test](../power-test/README.md)).
