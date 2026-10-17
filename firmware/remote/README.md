# Hubmote firmware

The remote's firmware: ESP-IDF 5.4.4 and LVGL 9 on Waveshare's ESP32-S3
touch boards. This file is about the firmware itself: configuration, what
it stores, how its parts work and where they are.

- Setting up a remote: [getting started](../../docs/getting-started.md);
  using it: [using the remote](../../docs/using.md).
- Building, flashing and updating your own: [building](../../docs/building.md).
- Open problems: the [GitHub issues](https://github.com/DovahBrownies/Hubmote/issues).

## How builds and updates work

The commands are in [docs/building.md](../../docs/building.md). How the
pieces work:

- **Updates over Wi-Fi** (`main/ota.c`).
  - `hm-ota.ps1` posts `build\hubmote.bin` to `/ota` on port 8080,
    signed with `OTA_TOKEN` from `secrets.env` (below). The remote checks
    the signature and the image's SHA-256, writes it to the spare app slot
    and restarts into it.
  - Rollback is on: an image that doesn't reach Wi-Fi is replaced by the
    previous one on its next restart. Reaching Wi-Fi calls
    `ota_mark_good()`, logged as `ota: new firmware confirmed`.
  - `GET /version` shows the running build and slot; Settings shows the
    version and build time in the top bar's chip.
- **Version** comes from `version.txt` (`0.1.0-dev` until the first
  release). ESP-IDF reads it at configure time, so run
  `.\scripts\hm-idf.ps1 reconfigure` after changing it.
- **Flash layout** (`partitions.csv`):
  - `nvs` at 0x9000 and `storage` at 0x620000 must never move, so updates
    and USB flashes keep the pairing, settings and learned remotes.
  - `ota_0` and `ota_1` are 6 MB each; `otadata` picks the one that boots. A
    USB flash always boots `ota_0`.
  - Boards with 8 MB of flash use `partitions_8mb.csv`: `nvs` at the same
    offset, 3 MB slots, a smaller `storage` at 0x320000 (which mustn't move
    either).
- **Console** on the native USB connector (USB-Serial/JTAG), so GPIO43/44
  (UART0) are free for IR; the UART connector only shows the ROM banner.
- **Secrets** live in `secrets.env`, which is gitignored: `OTA_TOKEN` and,
  optionally, `WIFI_SSID` and `WIFI_PASSWORD`.
  - The update token is made up on the remote when its update server first
    starts, once Wi-Fi is up (developer builds; 20 letters and digits, from
    the radio's random source), and kept in NVS
    (namespace `ota`, `main/ota.c`). Settings > About shows it (Show): copy
    it into `secrets.env` as `OTA_TOKEN`, where `hm-ota.ps1`, `hm_ota.py`
    and `devtools.py` read it. It is never built into an image.
  - Requests are signed, not sent with the token (`ota_authorized()` in
    `main/ota.h`, `tools/hm_auth.py`): the tool fetches a one-time nonce
    from `GET /nonce` and sends `X-OTA-Nonce` and `X-OTA-Auth`, an
    HMAC-SHA256 of the nonce, method, path and the body's SHA-256
    (`X-OTA-Body`). A device posing as `hubmote.local`, or anyone reading
    the traffic, learns nothing it could use for another request. A wrong
    signature costs a second.
  - Wi-Fi is normally set on the remote (first start, or Settings > Wi-Fi)
    and kept in NVS (namespace `wifi`). `WIFI_*` from `secrets.env` are used
    only while NVS has no network, and saved there once they connect
    (`main/net.c`). A network chosen on the remote is saved only after it
    connects.
  - `main/CMakeLists.txt` turns the `WIFI_*` lines (never `OTA_TOKEN`) into
    `build/esp-idf/main/hm_secrets.h`, so nothing lands in `sdkconfig`. Edits are picked up by the next build.
  - `python tools/check_secrets.py` checks the Wi-Fi values against what was
    compiled, without printing them.
- **`sdkconfig`** is generated from `sdkconfig.defaults`. After changing the
  defaults, move `sdkconfig` aside (as a backup) and rebuild, then diff the
  two. Kconfig changes in `main/Kconfig.projbuild` need
  `.\scripts\hm-idf.ps1 reconfigure`.

The `Hubmote` menu in menuconfig (`.\scripts\hm-idf.ps1 menuconfig`) has
these submenus (and one more menu outside it):

- **Board** (`HM_BOARD`): which board in `components/board/boards/` is
  built, and the board-specific defaults (screen size, IR pins). The
  Waveshare ESP32-S3-Touch-LCD-7B by default; Waveshare's 7, 5B, 5, 4.3,
  4.3B and 4.3C are there too, untested. Build those with `-D
  HM_BOARD=<id>` from a new sdkconfig, which also brings their
  `<id>.defaults` (the 7's flash size);
  [docs/porting.md](../../docs/porting.md) has the table, the commands and
  how to add a board.
- **Google TV:** the Streamer's address (optional: blank finds it with
  mDNS) and the name the TV shows while pairing.
- **Display:**
  - **Turn the screen and touch 180 degrees on first start**
    (`HM_DISPLAY_ROTATE_180`, off by default: right for a bare board). Only
    a default: the remote saves it on its first start (NVS `ctrl`/`turn`),
    and Settings > Screen > Rotate 180° changes it at once, without a
    restart (the enclosure needs it on). `main/display.c` turns the picture while
    sending it to the panel: the driver keeps no framebuffer, asks for each
    10-line bounce buffer, and the copy reads LVGL's framebuffer backwards
    (a 180-degree turn is the pixel stream reversed). LVGL still draws
    double-buffered in direct mode, so nothing tears and drawing costs the
    same. Touch is mirrored in `board_touch_init()`, and by the touch
    driver's mirror flags when the setting changes.
  - Brightness: the normal level is only a default (Settings > Screen saves
    it in NVS); the dimmed level (`HM_BRIGHTNESS_DIM`) is fixed, capped at
    the normal level.
- **Power:** the dim, screen-off and turn-off (deep sleep) times: defaults
  only, as Settings > Screen changes them and keeps them in NVS. Dim and
  screen-off have a second pair for USB power (`HM_DIM_AFTER_CHARGING_S`,
  5 min, and `HM_SCREEN_OFF_AFTER_CHARGING_S`, 30 min; 0 = never), used
  while `battery_update()` reports USB or no cell. Deep sleep after 12 idle
  hours by default (`HM_SLEEP_AFTER_H`, 0 = never), on any power.
- **IR:** the pins (the 7B's defaults: transmitter GPIO44, receiver
  GPIO6; optional receiver power pin) and the **start-up self-check** (`HM_IR_BOOT_CHECK`, off by
  default; for checking the wiring). About 1 s after start-up the
  transmitter sends a burst that matches no protocol while the receiver
  listens, and the log says `ir_ctl: IR check ...` with the number of
  frames caught (frames that differ mean a weak or garbled signal, or the
  two too close together).
- **Updates:** where releases are published (`HM_UPDATE_URL`, default the
  GitHub Pages site; https only, developer builds also take http:// for a
  site served from a PC) and whether the remote checks by itself
  (`HM_UPDATE_AUTO`, a minute after Wi-Fi, then daily). Release builds have
  this too: it is how they update.
- **Modules:** one option per module (`modules/*/Kconfig.module`); a module
  that is off isn't compiled at all.
- **Android TV Remote (atv_remote)**, outside the Hubmote menu (Component
  config, from `components/atv_remote/Kconfig`): the client
  task's stack (`ATV_REMOTE_TASK_STACK_SIZE`, 8192, always internal RAM),
  priority (5) and core (-1, any).
- **Developer build** (`HM_DEVELOPER`, on by default; off in release
  builds). Its options:
  - **Updates over Wi-Fi from a PC** (`HM_LOCAL_OTA`): the update server on
    port 8080 (`HM_OTA_PORT`) that `hm-ota.ps1` sends to, with the update
    token.
  - **Screenshot and simulated touches** (`HM_DEV_TOOLS`, needs the update
    server): `GET /screenshot` (24-bit BMP of the screen),
    `POST /tap?x=&y=&ms=` and `POST /swipe?x1=&y1=&x2=&y2=&ms=`, with the
    same token. `python tools/devtools.py shot|tap|swipe` wraps them (PNG
    output needs Pillow).
  - **Log over Wi-Fi** (`HM_NETLOG`): TCP port 2323 (`HM_NETLOG_PORT`),
    holding the last 32 KB (`main/netlog.c`). The Wi-Fi driver is limited
    to warnings, so the network name stays out of the log.
  - **Protocol self-test at start-up** (`HM_ATV_SELFTEST`): the Android TV
    Remote encoder and parsers against golden vectors.
  - **Battery preview** (`HM_BATTERY_PREVIEW`, off; test builds only). On
    battery the shown level counts down from 35% to 10%, 1% every 2 s, to
    check the red level (below 30%) and the low-battery card (below 20%)
    without draining the cell.

**Developer and release builds.** A normal build (`hm-idf.ps1 build`) is a
developer build. The release build is `sdkconfig.defaults` plus
`sdkconfig.release`, which turns `HM_DEVELOPER` off: no update server, no
log over Wi-Fi, no screenshots or touches, no self-test. `hm-web.ps1`
builds it (in `build-web/<board>/`, from a fresh `sdkconfig` each time, and
with `HM_NO_SECRETS`, so nothing from `secrets.env` goes in) for the web
installer. A remote running a release build
can't take `hm-ota.ps1` updates: flash a developer build over USB first.

Volume goes through the Streamer (the remote protocol) on every screen,
unless Settings > Remotes > TV volume is set to the TV's IR. A held volume
button is a real key hold (down, then up on release), which the Streamer
passes on over HDMI-CEC.

## What's stored

The first start is described in
[getting started](../../docs/getting-started.md): the remote makes its
pairing key and certificate (RSA-2048, up to a minute, once), asks for
Wi-Fi, finds the TVs with mDNS (skipped if an address is configured or
remembered) and pairs. It finds the TV again by its mDNS name when its
address changes.

**What's saved in NVS:**
- namespace `atv`: the pairing key and certificate (`key`, `cert`), the
  TV's address (`host`) and the hash of the TV's public key pinned at
  pairing (`tv_key`);
- namespace `ctrl`: the TV's mDNS name (`tv_mdns`), Home's app order
  (`app_order`) and hidden apps (`hidden_apps`), Settings > Screen
  (`bright`, `dim_s`, `off_s`, `dim_c`, `off_c` for charging, `sleep_h`,
  `turn` for Rotate 180°), the theme (`theme`), and the IR TV (`irtv_set`,
  `irtv_name`, `irtv_vol`);
- namespace `wifi`: the network's name and password (`ssid`, `pass`);
- namespace `ota`: the update token (`token`);
- namespace `companion`: the token from pairing with companion 0.4+
  (`token`);
- namespace `hub_off`: modules switched off in Settings (key = module id);
- namespaces `m_<module>`: each module's own settings.

## Companion app (optional)

`companion/` in the repo is a small Android TV app for the Streamer:
[docs/companion.md](../../docs/companion.md) installs it, its
[README](../../companion/README.md) covers the API and building it. When it is
reachable (`components/companion`, HTTP on port 6470 of the TV):

- **Home** lists every app installed on the TV with its own icon, and opens
  them through the companion. Without the companion it shows the five
  built-in apps.
- **Arranging apps:** Home's **Edit** button. A tap hides or shows an app;
  press and hold drags it. The order and the hidden apps are saved in NVS
  (see above), as package names between newlines. They apply to the built-in
  apps too. Apps never placed follow the placed ones, alphabetically.
- **Icons** (about 43 KB per app in PSRAM) are also kept in `/storage/icons`
  (`companion_set_cache_dir()`): one file per app, both sizes deflated with
  the ROM's miniz (about 4 KB per app), checked against the app's `updated`
  value, at most 640 KB, and only for apps still on the TV. So after a
  restart or waking up they load in under a second; only new or updated
  apps are downloaded. The deflate runs on a short-lived task with a
  10 KB stack (it uses about 7 KB), so the companion task's stack stays
  6 KB.
- **Pairing** (companion 0.4+): the companion answers 401 until the remote
  pairs. The client then reports "pairing needed"; Settings > Remotes shows
  **Pair companion**, which asks the companion to show a code on the TV
  (`/pair/start`) and opens the setup screen's code entry (with Cancel).
  The typed code goes to `/pair`; the token that comes back is saved in NVS
  and sent as `Authorization: Bearer` on every request.
- **Its version and updates** (companion 0.5+): on each connection the
  client reads `/status` (version code, name, whether it can update
  itself). `update.json` may carry the release's companion (version, code,
  APK name, size, SHA-256; `main/update.c`). When the release's code is
  higher, Settings > Remotes shows both versions and, if the companion can
  update itself, **Update companion**: the controller sends the APK's
  address (next to `update.json`) to `/update`, follows the companion's
  `{"update":...}` events, and counts the update done when the companion
  comes back with the new version.
- **The keyboard** types through the companion whenever it reports a focused
  field (`/text`, `/enter`): this works in apps that never tell the remote
  protocol about their fields, such as browsers.
  - While typing that way, the TV's late echoes of the field are ignored.
  - Number fields get the number pad alone, and passwords show as `*`.
  - The text clears when a different field gets focus.
  - While the remote's keyboard is open, the Streamer's on-screen keyboard is
    hidden (`/keyboard`).
  - Apps that draw their own keyboard (Plex): the companion reports it as a
    field and presses its keys (companion README, "On-screen keyboards").
- **The keyboard's modes** (`ui_keyboard.c`, picked when it opens and
  switched while open by `ui_kb_follow()` when the field or the app
  changes): `UI_KB_FIELD` (the TV's or the companion's field: whole text),
  `UI_KB_SEARCH` (Google TV's Home: Google TV search, `/search`),
  `UI_KB_KEYS` (anything else: characters as protocol text `UI_ACT_TYPE`,
  which Android hands to apps as virtual-keyboard keys; Backspace and Enter
  as key presses) and `UI_KB_LOCAL` (text for the remote itself).

## IR

Wiring, and the MOSFET stage for room range: [docs/hardware.md](../../docs/hardware.md).
Pins are in menuconfig (`Hubmote` > IR); `-1` turns sending or learning
off, and the UI then leaves out what can't work (`ui_set_ir_caps()`).

- **The TV:** built-in codes in `main/tv_codes.c`, generated by
  `tools/ir/import_tv_codes.py` ([docs/ir-tvs.md](../../docs/ir-tvs.md));
  keys learned from the TV's remote are stored as the remote `tv`.
- **HDMI switch inputs:** four inputs, each a name and a learned code,
  stored as the learned remote "HDMI switch", button slots 0-3
  (`main/ir_ctl.c`, `components/ui/ui_inputs.c`).
- **Learned remotes** are stored on LittleFS as `/storage/ir/<id>.json`.
- Learned remotes and the learn flow hide the rail and touchpad, so nothing
  goes to the Streamer by mistake.

## Layout of the code

| Path | What it is |
|---|---|
| `main/main.c` | Bring-up: NVS, cJSON's PSRAM allocator, board, LVGL, display, touch |
| `main/display.c` | The LVGL display: two PSRAM framebuffers swapped between frames, filled into the panel's bounce buffers, turned 180 degrees if set |
| `main/battery.c` | Battery level and USB power from the cell voltage: steady readings mean USB, ripple means battery |
| `main/controller.c` | The controller task: one queue for UI actions, TV, Wi-Fi, companion and update events; the Streamer's keys and IR actions, what Wi-Fi coming up starts, Home layout in NVS, heap and key logs. Its parts share `main/ctrl_internal.h`, each handling its own UI actions: |
| `main/ctrl_setup.c` | First-run flow: identity, Wi-Fi (network picker), finding the TV (mDNS or menuconfig address), pairing, finding it again by name when its address changes |
| `main/ctrl_companion.c` | The companion: app list on Home, pairing, its version and updates; typing (companion field, Google TV search on Home, protocol text, Enter) |
| `main/ctrl_screen.c` | Settings > Screen and Appearance in NVS, dimming and screen off (longer on USB), turning off after idle hours, the theme restart, the battery readout |
| `main/net.c` | Wi-Fi station: the network from NVS (or `secrets.env` the first time), scanning, joining another; the `hubmote.local` mDNS name |
| `main/netlog.c` | Log over Wi-Fi: ring buffer plus TCP server on port 2323 |
| `main/ota.c` | Updates over Wi-Fi from a PC (`/ota`, `/version`, `/nonce` on port 8080, developer builds; requests signed with the update token), rollback confirmation |
| `main/update.c` | Updates from the releases: reads `update.json` at `HM_UPDATE_URL` (and the release's companion, if any), compares versions (a release beats its own pre-releases), downloads its board's image (`HM_BOARD_ID`) into the spare slot with size and SHA-256 checks; checks a minute after Wi-Fi, then daily, and on request (Settings > About) |
| `main/build_info.c`, `main/build_stamp.cmake` | The build time, stamped on every build (ESP-IDF's own date rarely changes); shown in Settings and by `/version` |
| `main/devtools.c` | Developer tools on the update server: `/screenshot`, `/tap`, `/swipe` (`HM_DEV_TOOLS`) |
| `main/tv_ir.c`, `main/tv_codes.c` | The IR TV: its code set (NVS `irtv_*`) and learned keys (`/storage/ir/tv.json`); the built-in sets (generated, with `tv_codes_licenses.txt`) |
| `main/storage.c` | Mounts LittleFS at `/storage` (the `storage` partition: learned remotes, modules' files, the icon cache) |
| `main/ir_ctl.c` | Key routing to IR, Add a TV, the learn-and-save flow, the Inputs page, the start-up IR check |
| `components/board` | Board support: `board.h` is what the firmware needs from a board; each board has `boards/<id>.h` (its panel, expander pins and what it can do) and `<id>.defaults`, chosen with `-D HM_BOARD=<id>` (menuconfig > Hubmote > Board shows it). Waveshare's boards share `waveshare_s3_rgb.c` (I2C, GT911, the RGB panel, deep sleep) with `waveshare_ch32v003.c` (the 7B and 4.3C: backlight PWM, battery ADC) or `waveshare_ch422g.c` (on/off backlight). The 7B's panel: ST7262 at 14 MHz with bench-proven timings. Adding a board: [docs/porting.md](../../docs/porting.md) |
| `components/atv_remote` | Android TV Remote v2: identity, mDNS discovery, pairing (6467), control session (6466) |
| `components/atv_remote/tools`, `proto` | `gen_keycodes.py` (writes `atv_keycodes.h`, standard library only) and `gen_golden.py` (writes the self-test's `atv_golden.h`; needs protobuf and grpcio-tools in a venv, see its docstring), from the vendored androidtvremote2 protos |
| `components/companion` | Client for the Streamer companion app: pairing and its token, its version, event stream (field type and id, update progress), app list and icons, launch, set text, Enter, Google TV search (`/search`), keyboard hiding, asking it to update, what is playing (`companion_media()`, 0.6+) |
| `components/ui` | LVGL 9 screens (`ui.c` view switching, `ui_topbar.c` tabs and glance, `ui_switcher.c` the switcher sheet, `ui_dash.c` the Home tab and module host, `ui_tv_home.c` Google TV's Home: the app grid and Edit, `ui_app.c` the app context panel, `ui_rail.c` the Streamer's keys, `ui_pad.c`, `ui_keyboard.c`, `ui_setup.c` first run and code entry, `ui_settings.c` Settings, `ui_ir.c` the TV and learned remotes, `ui_tv_setup.c` Add a TV, `ui_learn.c` the learn flow, `ui_inputs.c`, `ui_wifi.c` the Wi-Fi network picker, `ui_status.c` loading, error and empty states, …); `include/ui_kit.h` is the part modules use, built in `ui_kit.c` (the themes are `ui_themes.c`); `include/ui_size.h` scales pixel sizes to the board's screen (`UI_S()`); `ui_mem.c` puts LVGL's heap in PSRAM; fonts in `fonts/h<screen height>/` |
| `components/hub` | The module platform: registry (`HUB_MODULE()`), events, glances, one HTTP worker, per-module NVS and files, SNTP time ([docs/modules.md](../../docs/modules.md)) |
| `modules/` | One component per module, each with `Kconfig.module` and a README: `weather/` (Open-Meteo, host test in `test/`) |
| `components/ir` | IR send and learn on the RMT peripheral, NEC / Samsung32 / SIRC / RC5 / RC6 encoding, NEC / Samsung32 decoding, learned remotes on LittleFS; host tests in `host_test/` (`run_host_tests.ps1`) |
| `tools/fonts/build_fonts.py`, `preview_fonts.py` | Generates the fonts for every screen height, `ui_fonts.h` and `ui_icons.h` (setup is in its docstring: venv with fontTools, then `npm install`); a preview of them |
| `sim/` | The UI simulator: the real UI drawn on a PC at every board's screen size, saved as PNGs ([sim/README.md](sim/README.md)) |
| `tools/web/make_site.py` | Assembles the installer site and `update.json` from release builds, one folder per board, optionally with the companion (used by CI and `hm-web.ps1`) |
| `tools/ir/import_tv_codes.py` | Builds `main/tv_codes.c` from SmartIR (MIT) and CC0 Flipper-IRDB files ([docs/ir-tvs.md](../../docs/ir-tvs.md)) |
| `tools/check_themes.py` | Checks every theme's contrast ([docs/themes.md](../../docs/themes.md)) |
| `tools/check_secrets.py` | Checks `secrets.env` against the compiled header without printing it |
| `tools/new_module.py`, `tools/module_template/` | Makes `modules/<id>/` for a new module |
| `tools/devtools.py` | Screenshot, tap and swipe over Wi-Fi (signed with the token from `secrets.env`, never printed) |
| `web/`, `scripts/hm-web.ps1` | The web installer: `index.html`, ESP Web Tools 10.4.0 in `web/esp-web-tools` (vendored by `tools/web/vendor_esp_web_tools.py`, one change: a failed install offers Close), and the script that builds every board's firmware without secrets (`HM_NO_SECRETS`, in `build-web/<board>/`) and the site (`build-web/site`, `-Serve` for localhost) |
| `scripts/` | `hm-idf.ps1` (build, USB flash), `hm-ota.ps1` (update over Wi-Fi), `hm-netlog.ps1` (log over Wi-Fi), `hm-monitor.ps1` and `hm-capture.ps1` (log over USB); Windows PowerShell |
| `tools/hm_ota.py`, `tools/hm_netlog.py` | The same updates and log over Wi-Fi from any OS (standard-library Python); on macOS and Linux, `idf.py build flash monitor` covers the rest |
| `tools/hm_auth.py` | The update token from `secrets.env` and request signing, shared by `hm_ota.py` and `devtools.py` |

**System screens:** the TV reports the screensaver (`...tv.dreamx`), the
Assistant (`katniss`), system panels and keyboards as the foreground app.
`ui_app_is_system_screen()` (`components/ui/ui_apps.c`) lists them; the
controller ignores those reports, so the remote stays on the app underneath
(log: `app: ... (system screen, ignored)`). Add a package there if another
one shows up as an app.

**Typing on Home** (`UI_KB_SEARCH`): with Google TV's launcher in front and
no field, the keyboard sends its text to Google TV's search through the
companion (`/search`), 1.5 s after typing pauses (3+ characters) or at once
on Enter (`SEARCH_PAUSE_US`, `SEARCH_MIN_CHARS` in `main/ctrl_companion.c`).

**Built-in apps:** the tiles and deep links are in `components/ui/ui_apps.c`.
They show as letter tiles (no logos are shipped); the companion's list
brings the real icons. To add one, add its package, name and deep link.

**Memory:** the logs print `ctrl: heap ...` lines at start-up and on
connect. Internal RAM should stay well above 50 KB: typically about 116 KB
after start-up and 89 KB once connected to the TV (lowest 78 KB).
Settings > About shows the current figure. Large or numerous buffers
belong in PSRAM.

## Tested on hardware

On a 7B with a Google TV Streamer and a TV over HDMI-CEC:
- display, touch, backlight, Rotate 180° (both ways, kept after a restart);
- Wi-Fi, discovery, pairing, reconnecting;
- volume through HDMI-CEC, including holding;
- app detection and launching from the Google TV app grid;
- the keyboard in Google TV fields, a browser, an app's own on-screen
  keyboard (Plex), YouTube and Google TV search, the number pad, hiding the
  Streamer's keyboard;
- the companion: app list, pairing (0.4), its version in Settings;
- updates and logs over Wi-Fi from a PC, updates from a release site
  (served from a PC), and the web installer;
- IR transmit, receive and learning, and the Inputs page;
- battery level, charging indicator, deep sleep and touch wake;
- the top bar, switcher and Home tab; Weather (location search, forecast,
  card, glance); Settings and themes.

Not checked on hardware yet:
1. **The built-in TV code sets** at a real TV.
2. **The release build** in use (it was flashed from the web installer),
   and the first-run Wi-Fi picker with no saved network.
3. **Companion updates** where Android lets apps install (the dialog, and
   the update without one).
4. **Deep links:** each built-in app tile, without the companion.
5. **Weather over days:** refresh timing, the cache after a restart,
   offline behaviour.

## What's next

The plan (modules, the setup experience, more TVs, releases) and its
progress are in [docs/PLAN.md](../../docs/PLAN.md); open problems are the
[GitHub issues](https://github.com/DovahBrownies/Hubmote/issues). On the
hardware side: a MOSFET transmit stage for IR range (docs/hardware.md), wake on pickup
(an accelerometer such as a BMA400, though GPIO6, the only free
wake-capable pin, holds the IR receiver), and the power-test measurements.
