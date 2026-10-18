# Hubmote

A touchscreen smart remote on the Waveshare ESP32-S3-Touch-LCD-7B. It
controls a Google TV Streamer (Android TV Remote v2 over Wi-Fi), a TV over IR
and other IR devices. `README.md` and the guides in `docs/` are for people;
this file holds the rules and facts an assistant needs. The user's own setup
(board MAC and ports, addresses, TV, preferences) is in `CLAUDE.local.md`
(gitignored): personal facts go there, never into tracked files.

## Layout

Git, branch `main`, remote `origin` = github.com/DovahBrownies/Hubmote.
Push only when the user asks (`CLAUDE.local.md` may have more rules about
this checkout's history). `secrets.env`, `sdkconfig`, keystores, build
output, `private/` and `CLAUDE.local.md` are ignored. `.gitattributes` keeps
LF, and CRLF for `.ps1`.

- `LICENSE` (Apache-2.0) and `NOTICE` (third-party work and licenses): add
  a NOTICE entry for any new dependency, font, icon set, code source or
  data source.
- `README.md`: the front page, kept short (what it is, what you need, three
  steps, links). The guides, by reader: `docs/getting-started.md`
  (install, first start, pairing, volume, updates), `docs/using.md` (the
  screens and settings), `docs/companion.md` (installing and pairing the
  companion), `docs/hardware.md` (IR wiring, battery, power switch, case),
  `docs/troubleshooting.md` (logs, problems), `docs/building.md`
  (developers: PC setup, secrets, build, flash, updates over Wi-Fi).
  Component READMEs (`firmware/remote`, `companion`) hold the code-level
  details. Put each fact in one place and link to it; keep the guide that
  covers a change current.
- Open problems and pending work are GitHub issues (`gh issue list`).
- `private/PLAN.md` (gitignored, may not exist): the original plan and
  the decisions behind the design (modules, navigation, themes, IR TVs,
  setup, the release), taken out of the repo on 2026-10-17. The public
  roadmap is the issues.
- `docs/modules.md`: how to write a module (API, threads, rules, testing,
  PR checklist). Keep it current when `components/hub` or `ui_kit.h`
  change.
- `docs/themes.md`: themes and the color roles; how to add a theme.
- `docs/ir-tvs.md`: the IR TV, Add a TV, code sources and licenses.
- `docs/porting.md`: the boards (a table of what each can do), building
  for one, adding one (the board component's contract, the test list);
  keep it current when a board, `board.h`, `main/display.c` or the
  board-specific settings change.
- `.github/workflows/build.yml` and `docs/releasing.md`: CI (host tests,
  the UI simulator, developer and release firmware for every board,
  installer site, companion APK; skipped when a push or pull request
  only changes Markdown, `docs/` or `hardware/`), Pages and
  releases on `v*` tags, the first publish with a fresh history, the
  companion's release key in repository secrets.
- `CONTRIBUTING.md` and `.github/ISSUE_TEMPLATE/`, `pull_request_template.md`:
  how others report and contribute; keep them in line with these rules.
- `docs/security.md` (and `SECURITY.md` for reports): connections, what
  is stored, the threat model and known gaps. Update it when a port,
  token, stored secret or outgoing connection changes.
- `private/` (gitignored, may not exist): a place for notes that stay out
  of the repo.
- `firmware/remote/`: the real firmware (ESP-IDF 5.4.4, LVGL 9). See its
  README for the code layout.
  - `components/hub/`: the module platform (registry, events, HTTP worker,
    storage, time, actions on the TV and its data). `components/ui/include/ui_kit.h` is the UI part
    modules may use.
  - `modules/<id>/`: one component per module (Weather, Sleep timer, Screen time,
    Guest Wi-Fi, Internet speed), each with
    a `Kconfig.module` and a README.
  - `scripts/`: `hm-idf.ps1` (build/flash), `hm-ota.ps1` (firmware over
    Wi-Fi to `hubmote.local:8080`, token from `secrets.env`),
    `hm-monitor.ps1` (live log),
    `hm-capture.ps1` (USB log to file, optional `-Reset`), `hm-netlog.ps1`
    (log over Wi-Fi from TCP port 2323: use it when the remote is off USB),
    `hm-web.ps1` (the web installer: the release build of every board, or
    `-Board <id>`, with the site assembled by `tools/web/make_site.py` as
    in CI, from `sdkconfig.defaults` + `sdkconfig.release` + the board's
    defaults with a fresh sdkconfig, and `HM_NO_SECRETS`, in
    `build-web/<id>/`; the site is a folder per board with `update.json`
    listing them; `-Serve` for localhost). Check a web build carries no
    secrets.env value before publishing it.
  - Developer vs release: `HM_DEVELOPER` (on in normal builds) holds the
    update server (`HM_LOCAL_OTA`), devtools, the Wi-Fi log and the
    self-test. A remote on a release build can't take `hm-ota.ps1` updates
    until it gets a developer build over USB.
  - `web/`: the installer page and ESP Web Tools 10.4.0 (vendored and
    patched by `tools/web/vendor_esp_web_tools.py`; don't edit by hand).
  - `sim/`: the UI simulator (`sim/run_sim.py`, README there): the real
    UI built for the PC with MinGW/gcc, every screen at every board's
    screen size, as PNGs. CI keeps them as the `screens` artifact.
  - `tools/`: `fonts/build_fonts.py` (fonts and icon glyphs, for every
    screen height in `components/ui/fonts/h<H>/`),
    `check_secrets.py`
    (checks Wi-Fi secrets without printing them), `check_themes.py`
    (contrast of every theme), `new_module.py` (a new
    module from `module_template/`), `devtools.py` (screenshot, tap and
    swipe over Wi-Fi; run it with the fonts venv's Python for PNG output).
- `firmware/power-test/`: a standalone current-measurement firmware with a
  results table in its README.
- `companion/`: the Android TV companion app for the Streamer (app list,
  icons, launching, focused-field type, typing into fields, hiding the TV's
  keyboard). 0.4 adds the pairing token, 0.5 updating itself when the remote
  asks, 0.6 what is playing (`/media`, with notification access granted by
  `install.py` / `install.ps1`), a pairing lockout and request limits, 0.7
  modules' messages at the top right of the TV (`/notify`, `hub_notify()`)
  and an internet speed test against Cloudflare's (`/speedtest`,
  `hub_tv_speedtest()`), 0.8 a speed test that copes with a poor line. `setup-tools.ps1` installs JDK 17 and
  the Android SDK; `build.ps1` builds it without Gradle; `install.ps1`
  installs and enables it over ADB.

## Build rules (hard)

- **Wrapper scripts only.** Build, flash and monitor every ESP-IDF project
  only through its own `scripts\hm-idf.ps1` and `scripts\hm-monitor.ps1`
  (`hm-capture.ps1` only reads the serial port, so it is fine too). Never
  call `idf.py` directly: several ESP32 projects share this machine, and an
  unwrapped build can poison `build/CMakeCache.txt`.
- **No redirection in PowerShell 5.1.** Do not pipe the wrapper through
  `2>&1` or `*>`. To capture logs, use `Start-Process ...
  -RedirectStandardOutput/-RedirectStandardError`.
- **ASCII-only scripts.** Keep `.ps1` files ASCII.
- **Updates over Wi-Fi** (`hm-ota.ps1`) are the normal way once the board
  runs update-capable firmware: the remote usually sits away from the PC,
  off USB. Rollback is on, and `ota_mark_good()` runs when Wi-Fi comes up.
  Never move `nvs` (0x9000) or `storage` (0x620000) in `partitions.csv`:
  that loses the pairing or the learned remotes.
- **Confirm the port.** Ask which COM port is the 7B before flashing:
  other Espressif boards may be plugged in too. Check the `MAC:` line in
  the flash output against the board's (`CLAUDE.local.md`). The 7B shows up
  as a USB-Serial/JTAG port (native USB connector, VID 303A, logs and
  flashing) or as a CH343 port (UART connector, flashing only).
- **Wi-Fi credentials** live on the remote (NVS namespace `wifi`, set in
  first-run setup or Settings > Wi-Fi). `firmware/remote/secrets.env`
  (gitignored; template `secrets.env.example`) may seed them with WIFI_*;
  it also holds OTA_TOKEN for the PC tools. The remote makes its own token,
  keeps it in NVS (namespace `ota`; never built into an image) and shows
  it in Settings > About behind Show: never press
  Show in a screenshot. Never print secrets.env values or copy them anywhere
  else; check them only with `tools/check_secrets.py`. Never type the
  user's Wi-Fi password through devtools, and never repeat network names
  (the user's or the neighbours' in scan lists) in messages or logs. The
  Wi-Fi driver is limited to warnings (`net.c`) so the SSID stays out of
  logs, but still filter `wifi:` lines out of log excerpts you show.
- **`sdkconfig`** is generated. After changing `sdkconfig.defaults`, move
  `sdkconfig` to a backup and rebuild, then diff the two to confirm that only
  the intended options changed.

## Hardware facts that matter

These are the 7B's, the board this is developed on (and the user's). The
other boards (Waveshare's 7, 5B, 5, 4.3, 4.3B, 4.3C) are supported but
untested: their facts and sources are in their headers in
`components/board/boards/` and in `docs/porting.md`.

- **Boards.** Each has `components/board/boards/<id>.h` (panel, expander
  pins, `BOARD_BACKLIGHT_DIMMABLE`, `BOARD_BATTERY_READING`,
  `BOARD_UNTESTED`) and `<id>.defaults` (its choice, and flash size and
  partition table where they differ: the 7 has 8 MB,
  `partitions_8mb.csv`). Code is shared: `waveshare_s3_rgb.c` (I2C, GT911,
  panel, deep sleep) with `waveshare_ch32v003.c` (7B, 4.3C) or
  `waveshare_ch422g.c` (the others). Build another board with `-D
  HM_BOARD=<id>` and its own `-B`/`SDKCONFIG` (the build refuses an
  sdkconfig of another board); the default build stays the 7B. CI builds
  every board that has a `.defaults` file. Without IR pins the UI drops
  IR (`ui_set_ir_caps()`), without dimming or a battery reading it drops
  those settings (`ui_set_board_caps()`).
- **IO expander.** It is a CH32V003 at I2C 0x24 (reg 0x02 mode, 0x03 out,
  0x05 backlight PWM inverted, 0x06 battery ADC), not a TCA9554. It
  sometimes drops writes, so the firmware re-sends the backlight state every
  second. Don't toggle EXIO0: it breaks touch wake.
- **Panel.** Pixel clock 14 MHz, with the porch timings in
  `components/board/boards/waveshare_s3_touch_lcd_7b.h`. These were proven
  on a second S3-7B; 18 MHz slips and 20 MHz breaks touch. The other
  boards use 14 MHz too (Waveshare's demos use 16 or 21).
- **Free pins.** GPIO6 (J8, wake-capable) and GPIO43/44 (H3 UART header,
  after SW1). SW1 is the slide switch left of the BOOT button, silkscreen
  `UART2 UART1`; UART2 = header H3, UART1 = CH343. It drives the select pin
  of analog switch U13 (FSUSB42). SW2 (`120R`, CAN/RS-485) is only bus
  termination and unrelated. The console is on USB-Serial/JTAG so that GPIO43/44 can be used
  for IR.
- **IR wiring.** The receiver is on J8 (3V3, GND, GPIO6). The transmitter
  is on H3 (3V3, GND, RXD = GPIO44) and needs SW1 on `UART2`. Both work
  (2026-10-11). GPIO43 (H3 TX) is still free.
- **Display turned 180 degrees** for the enclosure (Settings > Screen >
  Rotate 180°, NVS `ctrl`/`turn`, applied at once; `HM_DISPLAY_ROTATE_180`
  is only the first-start default, off for bare boards). The panel is
  created with `no_fb` (`components/board/boards/waveshare_s3_rgb.c`), and
  `main/display.c`'s bounce-buffer callback copies from LVGL's two PSRAM
  framebuffers, backwards when turned; touch follows through the driver's
  mirror flags.
  Don't go back to `lvgl_port_add_disp_rgb` or driver framebuffers: in
  direct mode they can't rotate. Devtools taps and screenshots are in
  upright screen coordinates either way.
- **Charging** isn't wired to the ESP32 (CHRG/STDBY only drive LEDs).
  On battery the CS8501 boost draws in bursts, so ADC readings swing
  100-300 mV; on USB they sit within ~10 mV. `main/battery.c` uses that
  spread, not voltage steps (steps failed: the ripple looked like
  plug/unplug). The same bursts make a faint coil whine. The ADC reads
  ~1% low (4.20 V reads ~4150).
- **Off = deep sleep** (`board_deep_sleep()`): backlight and panel rails
  off, wake on GT911 INT (GPIO4, opposite of its idle level) or BOOT
  (GPIO0, also LCD G3). Waking reboots. Asleep, OTA and netlog don't answer.
  "Turn off remote" (Settings) and 12 idle hours (Settings > Screen;
  default `HM_SLEEP_AFTER_H`) do it.
- **The rail** (the Streamer's keys) is hidden on every screen that isn't
  the Streamer's (`k_view_flags` in `ui.c`, one row per view: no rail, no
  pad, full width, a remote's screen, not the TV). Settings, the Home tab,
  modules, the learn flow, Add a TV and the TV panel widen to the full
  width (`UI_WIDE_W`, 1000 px on the 7B); learned remotes and Inputs stay a
  centered column (`UI_CENTER_W`). The TV panel has its own VOL keys, sent via the Streamer
  unless the TV volume setting says IR.
- **The IR TV** (`docs/ir-tvs.md`, `main/tv_ir.c`, `ui_ir.c`,
  `ui_tv_setup.c`): a built-in code set plus learned keys. Code sets come
  only from sources that allow reuse: SmartIR (MIT, its notice in
  `main/tv_codes_licenses.txt`) and Flipper-IRDB files contributed after
  commit 2319685 (CC0; older files are not licensed). Regenerate with
  `tools/ir/import_tv_codes.py`; never hand-edit `tv_codes.c` or add codes
  from unlicensed sources (LIRC, probonopd/irdb, older Flipper files).
  Samsung's navigation keys (Back, Home, Menu...) aren't in any clean set
  and must be learned.
- **Memory.** LVGL must allocate from PSRAM (`CONFIG_LV_USE_CUSTOM_MALLOC`,
  `components/ui/ui_mem.c`); on plain malloc, internal RAM fell to 3 KB.
  cJSON allocates from PSRAM too (hooked in `main.c`). Watch the `ctrl: heap ...`
  log lines after UI changes.
- **Sockets.** `CONFIG_LWIP_MAX_SOCKETS=16` and the update server's
  `max_open_sockets = 3`: at the default 10, module HTTPS requests failed
  with "esp-tls: Failed to create socket".

## UI and modules

- **Screen sizes.** The UI is designed at 1024x600; every pixel size goes
  through `UI_S()` (`ui_size.h`), which scales by the screen height at
  compile time and is the identity at 600 px. Grids that fill a width
  divide it instead (`UI_WIDE_INNER_W`, `PANE_W`). The size is the
  board's (`CONFIG_HM_SCREEN_W/H`), fonts come from
  `components/ui/fonts/h<H>/`. Check a layout change with the simulator at
  every size; at 1024x600 its pictures match the remote's screenshots
  pixel for pixel, so a change meant to keep the 7B's screens the same
  can be checked by comparing them before and after.
- **Navigation:**
  the top bar has a remote tab (current remote; its chevron opens the
  switcher sheet, `ui_switcher.c`), a Home tab (module cards,
  `ui_dash.c`: small, medium or large on a four-column grid, arranged with
  a long press or Settings > Modules > Arrange Home, saved in NVS
  `ctrl`/`home`; the switcher and the top bar's glance follow its order),
  a context pill, a module glance, Wi-Fi, battery and a gear
  (Settings, `VIEW_SETTINGS`, `ui_settings.c`: Remotes, Wi-Fi, Screen,
  Appearance, Modules, About). Modules share `VIEW_MODULE`.
- **Settings in NVS.** Screen settings live in `ctrl` (`bright`, `dim_s`,
  `off_s`, `dim_c`/`off_c` while on USB power, `sleep_h`, `turn`; Kconfig
  values are only defaults). "On USB" comes from `battery.c` (charging, or no cell
  connected). Keep the turn-off (deep sleep) time the same on any power:
  with the screen dark an unplug isn't detected, so a charging-only "never"
  could drain the battery. Switched-off
  modules in `hub_off` (`hub_module_set_enabled()`, controller task).
- **Module settings** are declared in the module (`hub_setting_t`, kinds
  switch, choice, number, text) and drawn by Settings > Modules
  (`ui_settings.c`); values live in the module's NVS namespace (`m_<id>`),
  `setting_changed()` runs on the controller task. Weather's Units is the
  first.
- **Modules self-register** with `HUB_MODULE()` (a constructor) and are
  linked with `WHOLE_ARCHIVE`. After adding or removing a folder in
  `modules/`, run `hm-idf.ps1 reconfigure` before building. A module's
  `REQUIRES` must not depend on its Kconfig option (ESP-IDF reads
  requirements before sdkconfig).
- **Themes** (`docs/themes.md`, `components/ui/ui_themes.c`). `COL_*` are
  roles read from the active theme, never fixed colors: `COL_ACCENT` for
  fills, `COL_ACCENT_TEXT` for accent text and icons (they differ in
  Daylight), radii through `ui_radius()`, pressed shades `ui_pressed()`.
  No `lv_color_hex()` in screens. The theme is chosen before `ui_init`
  (NVS `ctrl`/`theme`) and applies with a restart; an RTC flag reopens
  Settings > Appearance afterwards. Check a new palette with
  `tools/check_themes.py`, and screenshot changes in Daylight too.
- **Loading, error and empty states** use `ui_status` and `ui_spinner`
  (`ui_kit.h`) everywhere, for one look; don't hand-roll spinners or
  "Loading..." labels. Refreshes keep data on screen. Questions and
  pick-lists use `ui_modal()` / `ui_modal_list()`.
- **Threads.** Module `init`/`event` run on the controller task; LVGL only
  on the LVGL task or through `hub_ui_async()`.
- **Modules act on the TV** through `hub_actions.h` (keys, IR TV keys,
  turning the TV off, opening apps, messages on the remote and the TV
  (`hub_notify`), the TV's state, what's playing, the app list, the speed
  test, holding the remote awake). The calls queue `MSG_HUB` for the controller (`handle_hub()` in
  `controller.c`), which takes the UI's own paths.
- **Weather** (Open-Meteo, no key): the place's UTC offset sets the
  remote's local time (`hub_time`) while the time zone (Settings >
  Modules, NVS `hub`/`tz`) is Automatic. The zones and their POSIX rules
  are generated into `components/hub/hub_tz.c` by
  `tools/time/gen_zones.py` (IANA tz, public domain); don't edit it by
  hand.
  The attribution line "Weather data by Open-Meteo.com" is a license
  requirement and stays.
- **Updates from the releases** (`main/update.c`): Settings > About checks
  `update.json` at `HM_UPDATE_URL` (GitHub Pages) and installs over HTTPS,
  only images with the project's signature (ECDSA P-256 over board,
  version and SHA-256; public key `main/update_key.pem`, private key in the
  `UPDATE_SIGNING_KEY` secret, made by the user with
  `tools/web/make_update_key.py`; never handle it). CI's Pages job signs
  (`make_site.py --sign`). Test sites need a throwaway key and a test
  build with `-D HM_UPDATE_KEY=<its public half>` (`docs/building.md`).
  To test updates from a PC: build in a scratch folder with
  `HM_UPDATE_URL` pointing at a site on the PC (developer builds accept
  http://) and that throwaway key; serve `make_site.py` output from a
  developer build made with `-D HM_NO_SECRETS=1`, with `--developer` (it
  refuses developer builds otherwise, and images with secrets.env values),
  signed with `--sign <key> --public <its public half>`. The site's
  version must be newer than the remote's. Put the remote back on its
  normal developer build afterwards.
- **Version.** `firmware/remote/version.txt` sets the app version
  (the coming release with `-dev`, `0.1.1-dev` after 0.1.0); reconfigure after changing it. Settings shows version and
  build time in the context chip (`ui_settings_version_text()`), and TV
  updates don't overwrite it there (`tv_context()`). The build time comes
  from `main/build_info.c`, stamped every build: ESP-IDF's
  `esp_app_desc` date only changes when ESP-IDF recompiles that file.
- **Checking the screen yourself.** With `CONFIG_HM_DEV_TOOLS` (developer builds)
  the update server answers `GET /screenshot`, `POST /tap` and
  `POST /swipe` (signed with the token, `tools/hm_auth.py`);
  `tools/devtools.py` wraps them.
  Use it to verify UI changes instead of asking the user. The first tap on
  a dark screen only wakes it. Top bar targets: remote tab (120, 27),
  Home tab (275, 27).

## Streamer and protocol facts

- **The Streamer.** A Google TV Streamer (codename `kirkwood`), found by its
  mDNS name. Remote protocol on ports 6466 (control) and 6467 (pairing);
  the companion on 6470. Its address can change (Ethernet vs Wi-Fi, DHCP).
- **The remote** answers to `hubmote.local` (mDNS). Its DHCP address
  changes, so use the name. Log over Wi-Fi on port 2323, updates on 8080.
- **Testing off USB.** Record with `hm-netlog.ps1 -Remote hubmote.local`
  in the background; it reconnects by itself after a restart (TCP
  keepalive). The remote serves one log client at a time and the newest
  wins, so never run two recorders at once. `tools/hm_ota.py` and
  `tools/hm_netlog.py` are the cross-platform versions.
- **Pairing detection.** An unpaired client certificate gets a TLS alert and
  then a TCP reset. lwIP reports the reset first, so mbedTLS returns
  `-0x0050` (`NET_CONN_RESET`). `atv_remote.c` treats a reset after the
  client certificate was sent as "pairing needed".
- **Volume** goes through the Streamer (key presses over the protocol) by
  default: Settings > Remotes > TV volume on Google TV. The IR option is
  for TVs without HDMI-CEC (the user's preference is in `CLAUDE.local.md`).
  It only works when the Streamer's volume control is set to the TV over
  HDMI-CEC (Anynet+ on Samsung). With the wrong
  setting the keys arrive but nothing happens, and the TV reports
  `volume 0/0`.
- **Power keys.** The Streamer ignores KEYCODE_SLEEP and KEYCODE_WAKEUP
  sent over the remote protocol (2026-10-17, awake or on its
  screensaver; over ADB they work), so modules' TV off and
  `HUB_KEY_SLEEP`/`WAKEUP` toggle with POWER, sent only when it changes
  the state the Streamer reported (`streamer_power()` in `controller.c`).
- **Deep links** launch built-in apps. Launching by package over the remote
  protocol broke with Play Store 52.6, so the companion's `/launch` is used
  instead.
- **Companion pairing** (0.4+): every endpoint but `/status` and `/pair*`
  needs `Authorization: Bearer <token>`. Settings > Remotes > Pair companion
  shows a code on the TV (`/pair/start`); the remote saves the token in NVS
  `companion`/`token`. Never request a code (or tap Pair companion) while
  someone may be watching without asking: the code covers the picture.
- **Typing.** Apps such as browsers never send `remote_ime_show_request`, so
  the remote types through the companion (`/text`, accessibility
  `ACTION_SET_TEXT`) whenever it reports a focused field. In order of
  preference: the TV's field (IME), the companion's field or an app's own
  on-screen keyboard (companion presses its keys), Google TV's search on
  Home (`/search`), else text through the protocol (YouTube; Netflix only
  while its keyboard has focus) with Backspace/Enter as key presses.
  Prime Video can't be typed into (search from Home). Details: `docs/using.md`. The companion must
  never do network I/O on Android's main thread: 0.1 and 0.2 crashed that
  way, and Android then waits 30 minutes before restarting the service.
- **Companion updates** (0.5+): `update.json` carries the release's companion
  (only release-key APKs; `make_site.py --any-key` for tests); Settings >
  Remotes > Update companion sends it to `/update`. Android installs it
  silently only with "Install unknown apps" allowed (`install.ps1` does
  that), asks on the TV otherwise, and not at all where the user
  restriction `no_install_unknown_sources` is set (then the companion
  reports `"updates":false` and the remote hides the button).
  Test with a companion built as the installed version code (so ADB keeps
  its data) and the next one on a PC site; never leave a test companion
  installed above the real version.
- **Never `am force-stop` the companion:** Android doesn't rebind a
  force-stopped accessibility service by itself; reinstall it
  (`companion/install.py`) to bring it back.
- **Testing the companion over ADB:** `uiautomator dump` unbinds every
  accessibility service for a moment (the companion restarts and forgets
  its keyboard session): verify typing with screenshots. `pm clear` on the
  companion also switches its accessibility service off: re-enable with
  `settings put secure enabled_accessibility_services
  io.github.dovahbrownies.hubmote/.CompanionService`. Netflix blocks
  screenshots (they come out black). In Git Bash set `MSYS_NO_PATHCONV=1`
  for `adb shell screencap -p /sdcard/...` and pull to a Windows path, or
  the device path gets rewritten. Check the foreground app before
  sending OK: a stray OK in the Play Store or a player acts on the user's
  account.
- **ADB to the Streamer** works only through Wireless debugging, which works
  only on Wi-Fi: on Ethernet it switches itself off at once. Pair with
  `adb pair`, then `adb mdns services` gives the connect port. The Android
  tools live in `%LOCALAPPDATA%\Android` (JDK 17, SDK 35, build-tools
  35.0.0).
