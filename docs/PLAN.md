# Plan: modules, more TVs, and an open-source release

Status: in progress (2026-10-16). Decisions taken are in section 14.
Built so far: the native module framework (section 6), navigation A with
E's switcher (section 5), themes (5a), the Weather module (section 7), TVs
over IR (section 8), for setup the web installer, Wi-Fi setup on the
device, updates from the releases, and the companion's pairing and updates
(section 9), other boards and screen sizes with a UI simulator (section
16), the guides (section 12), the enclosure and a code of conduct. The
repository is on GitHub (one commit, private until the first release). The rest is a plan; the current state is in the guides and
the GitHub issues. Marks: [x] done, [~] partly done or waiting for a check on the
device, [ ] open.

## 1. Goals

1. **Modules.** Optional features that turn the remote into a small
   smart-home controller: Weather first, then things like a security panel.
   Others can add their own modules to the repo.
2. **More TVs over IR.** The Samsung remote stops being built in. Any
   common TV brand can be added from a code database, with a "does this
   work?" test.
3. **An open-source release** that a non-developer can set up: flash from a
   web page, set up on the device, install the companion without fuss.
   The repo and docs are clean enough for contributors.

## 2. Prior art: nothing does this yet

Research on 2026-10-12, with links:

| Project | What it has | What it lacks |
|---|---|---|
| [tronikos/androidtvremote2](https://github.com/tronikos/androidtvremote2) (Apache-2.0, Python) | The reference Android TV Remote v2 client; Home Assistant uses it | Not embedded; no UI |
| [nguyluky/ESP32_android_tv_remote_v2](https://github.com/nguyluky/ESP32_android_tv_remote_v2), [synapse-2/Arduino_GoogleTV_IP_Remote](https://github.com/synapse-2/Arduino_GoogleTV_IP_Remote) | ESP32 ports of the protocol | **No license** (can't be reused), headless or unfinished |
| [XDA article (2026-05)](https://www.xda-developers.com/replaced-google-tv-remote-esp32-display-controls-home-assistant/) | 3.5" ESP32 Google TV remote plus a Home Assistant page | No IR, code not published |
| [OMOTE](https://github.com/OMOTE-Community/OMOTE-Firmware) (GPL-3.0, ~1.8k stars) | DIY universal IR remote, compiled-in "devices/scenes" modules | No Google TV IP protocol, small screen, no prebuilt firmware |
| [openHASP](https://github.com/HASwitchPlate/openHASP), [ESPHome LVGL](https://esphome.io/components/lvgl/), [Tasmota + Berry](https://tasmota.github.io/docs/LVGL/) | Touch-panel smart-home controllers (pages from JSON/YAML, Tasmota loads scripts at runtime) | No Google TV remote |
| [alexf13e/tv-remote](https://github.com/alexf13e/tv-remote) | Same board family, IR | No Google TV, no license |
| [CuriousTech/Smart-Universal-Remote](https://github.com/CuriousTech/Smart-Universal-Remote) | Universal remote on small Waveshare ESP32-S3 round/square displays | No Google TV protocol, tiny screens |
| [EspControl](https://jtenniswood.github.io/espcontrol/) | No-code Home Assistant touch panels, browser installer | Smart home only, no TV remote |
| [CouchPilot (Android)](https://github.com/tzvika-por/CouchPilot) | Phone app: universal remote for LG, Google TV, a Samsung soundbar | A phone app, not hardware; also takes that name |
| [Unfolded Circle Remote](https://github.com/unfoldedcircle/integration-androidtv) | Commercial remote with Android TV support | Closed; its own docs say it can't list apps |

**The gap this project fills:** an openly licensed, native ESP-IDF Google
TV client with app context, typing (through the companion) and app icons,
plus IR learning and smart-home modules, on a 7" panel. The companion app
looks unique.

Lessons taken: OMOTE's compiled-in module folders are the closest precedent
for our modules; Tasmota and WLED show the web-installer pattern; Bruce
shows IR databases as data files on SD.

## 3. Constraints that shape the design

| Resource | Size | Used today | Verdict |
|---|---|---|---|
| Flash app slot | 6 MB (two, for updates) | about 2 MB (33 %) | Plenty of room for compiled-in modules |
| Unallocated flash | 1.875 MB at 0xE20000 | none | Can become a read-only `assets` partition without moving `nvs` or `storage` |
| `storage` (LittleFS) | 2 MB | learned remotes | User data, module caches |
| PSRAM | 8 MB | ~3.6 MB free | UI, JSON, images: fine |
| **Internal RAM** | ~117 KB free when connected | Wi-Fi, TLS, tasks | **The real limit.** Modules must not hold TLS sessions open or create big tasks |
| microSD | optional | unused | Bulk data (full IR database, asset packs), not code |

So "tree-shaking" isn't needed for flash space, but it's still worth having
for builds from source (less code, less attack surface, faster boot). ESP-IDF
already drops unreferenced functions at link time (`--gc-sections`); gating
each module with a Kconfig option makes sure a disabled module is never
compiled or referenced.

Loading module **code** from SD was considered and rejected: Espressif's
`elf_loader` works on the S3 but is young, has no isolation, and every
module would break whenever the firmware or LVGL configuration changes.
Scripting (Berry, Lua, MicroPython, WASM) costs 30-300 KB and needs UI
bindings. Modules are compiled in; SD and LittleFS hold their data.

## 4. Two kinds of modules, so the catalog can grow to dozens

Expectation (2026-10-13): once open source, people will write dozens of
modules. Compiling them all in doesn't scale: the 6 MB app slot holds
roughly 15-25 typical native modules, and each one is C code someone has to
review. So there are two kinds:

**1. Card modules (the long tail): data files, installed on the device.**
Most community ideas are "fetch something, show it, maybe press a button":
transit times, sports scores, stocks, a calendar, solar output, a server
status, a "movie night" button. A card module is a small package of files,
not code:

```
modules/transit/
  module.json     id, name, icon, version, author, permissions
  card.json       layout built from UI-kit parts (big number, list, chart,
                  buttons, image) with bindings into the data
  icon.png        optional
```

- **Data sources:** HTTP(S) JSON with a refresh interval, later WebSocket;
  values picked with a small path syntax (`$.current.temperature_2m`).
- **Actions:** HTTP request, send a learned IR code, Google TV key or app
  launch, open another module. So a "Movie night" card can switch the HDMI
  input over IR and open Netflix.
- **Settings:** declared fields (text, number, choice, API key) rendered by
  Settings like native modules.
- **Safety:** no code runs. A module can only reach the hosts listed in its
  `permissions`, shown when it's installed. API keys stay in NVS on the
  device.
- **Size:** a few KB to tens of KB each, stored on LittleFS (`storage`) or
  the SD card. Hundreds fit.
- **Install:** from a catalog (an index on GitHub Pages, built from a
  `modules/` folder in the repo that anyone can send a pull request to), or
  from the SD card. No reflashing.
- **Themes:** cards are built only from UI-kit parts, so they follow the
  theme automatically.

**2. Native modules (C, compiled in): for what data files can't do.** Custom
protocols, live streams, heavy UI. Weather starts as native (it proves the
platform and the UI kit); a Home Assistant client or an Eufy bridge client
would be native.

| Native tier | In the official image? | How to get it |
|---|---|---|
| Core | Yes, switched on/off on the device | Web installer |
| Extra | No | Build-time: menuconfig; planned: `tools/hm.py configure`, or a "Build custom firmware" GitHub Action with checkboxes |

Planned: CI prints each native module's flash and RAM cost and fails if the
official image passes a set size (for example 5 MB of the 6 MB slot).

**If cards outgrow data files** (real logic per module), the next step is a
scripting runtime for cards, not native plugins: Berry (MIT, ~40 KB, runs
Tasmota's LVGL scripts) or WebAssembly through WAMR (Apache-2.0, ~60 KB,
sandboxed, any language). Loading native code from SD (`elf_loader`)
stays rejected: no sandbox, and every module breaks when the firmware
changes.

What a module costs at runtime, either kind:

| | Switched off | Switched on |
|---|---|---|
| Flash / storage | native: its code; card: its files | same |
| RAM | nothing, never started | a little while idle; its screen is built when opened and freed when closed |
| Network | nothing | one request at a time through the shared HTTP worker |

Three ways to get firmware:

| Who | How | Native modules |
|---|---|---|
| Everyone | Web installer (GitHub Pages) flashes the official image | Core, switched on the device; cards from the catalog |
| People who want a custom image but no toolchain | Planned: fork, run a "Build custom firmware" GitHub Action with checkboxes, flash the result with the web installer | Any, at build time |
| Developers | menuconfig > Hubmote > Modules (`scripts\hm-idf.ps1 menuconfig` on Windows); planned: `tools/hm.py configure` | Any, at build time |

## 5. Navigation once modules exist

Before modules, the device chip opened one crowded "Devices" screen (TV
pairing, brightness, turn off, IR devices). It is now split in three
(built 2026-10-13):

```
Top bar:  [ Remote: Google TV v ]   Home    ...      [ 18° ] [wifi] [battery] [gear]
              |                       |                  |                     |
              v                       v                  v                     v
          Remotes                  Modules           Weather              Settings
          Google TV                Weather           (a module's          Wi-Fi, display, power,
          TVs (IR)                 Security          top-bar glance)      Google TV pairing,
          Learned remotes          ...                                    companion, modules,
          + Add a TV / remote      (tiles)                                updates, about, turn off
```

- **Remotes:** the current "remote picking" screen, without the settings.
- **Home:** module cards (`ui_dash.c`), one per enabled module. A module
  opens full width without the Streamer's rail, like other non-Streamer
  screens.
- **Glances:** a module may show a tiny glance in the top bar (Weather: icon
  and temperature). Tapping it opens the module.
- **Settings:** everything that is configuration, including a generated page
  per module (see 6.4).
- Later: an optional **idle screen** while charging (clock, weather glance)
  instead of turning the screen off.

Five navigation designs and five themes were mocked up on a design canvas:

- **A. Top tabs:** "Google TV v" (remote switcher) and "Home" (module
  dashboard) as tabs in the existing top bar, plus a settings button.
- **B. Hub launcher:** a start screen of tiles for every remote and module.
- **C. Side dock:** a 64 px icon dock on the left, always visible.
- **D. Bottom dock:** a bar along the bottom with labels and live glances.
- **E. Quick switcher:** one chip that opens a sheet listing remotes,
  modules and Settings.

Recommendation: **A with E's sheet** behind the remote tab. No extra chrome,
the rail and pad keep their size, modules are one tap away, and switching
remotes stays a two-tap action. Chosen: decision 7 (section 14).

## 5a. Themes

Theming is part of the repo (decided 2026-10-13). Built 2026-10-14
(`docs/themes.md`): roles instead of colors in code, the five themes below,
Settings > Appearance with previews, a contrast check
(`tools/check_themes.py`). Differences from this plan: a new theme applies
with a restart (about 10 s) rather than a live rebuild, because a rebuild
would have to refill every screen with the remote's state; Terminal keeps
Sora, since monospace at every size costs about 0.5 MB of flash; custom
JSON themes and a corner-radius scale per theme are still to do.

- **Tokens, not colors in code.** Every screen takes its colors and shapes
  from a theme: background, three surface levels, lines, three text levels,
  accent (Google TV) and accent-for-text, IR accent, good/warning/bad,
  weather colors (sun, rain, cool/warm ends of a temperature bar), corner
  radius scale, and the UI font. Built: `COL_*` are roles read from the
  active theme (`ui_themes.c`).
- **Built-in themes** (mocked up on the canvas): Midnight (today's look),
  Daylight (light), Black (high contrast), Dusk (indigo with coral), Terminal
  (green monospace, square corners).
- **Light themes** need separate "accent for text" values: amber text on a
  light background is unreadable, so Daylight uses a darker amber for text
  and keeps the bright amber for filled buttons.
- **Switching** in Settings > Appearance applies with a restart (see
  above).
- **Custom themes** as JSON files on LittleFS or the SD card (colors and
  radius). Fonts are limited to the ones compiled in: each extra font family
  costs flash, so Terminal's monospace-everywhere look needs its own font
  sizes (budget in section 4).
- `docs/themes.md` explains the tokens and how to add a theme; the module UI
  kit only exposes tokens, so modules follow the theme automatically.

## 6. Module framework

Built (2026-10-13) for native modules; the guide for module authors is
`docs/modules.md`. What exists and what is still to do:

### 6.1 Layout in the repo

```
firmware/remote/
  components/
    hub/            module registry, events, glances, HTTP worker, storage, time
    ui/             UI; include/ui_kit.h is the public part for modules
  modules/
    weather/        CMakeLists.txt, Kconfig.module, weather*.c/.h, test/, README.md
  tools/
    new_module.py   makes modules/<id>/ from tools/module_template/
    devtools.py     screenshots and simulated touches over Wi-Fi
```

`modules/` is added with `EXTRA_COMPONENT_DIRS`; every
`modules/*/Kconfig.module` is pulled into menuconfig's *Modules* menu with
`orsource`. A disabled module registers an empty component, so its code is
never compiled.

### 6.2 Registering a module

One `hub_module_t` per module, registered with `HUB_MODULE()` (a
constructor that runs before `app_main()`; the component is linked with
`WHOLE_ARCHIVE` so nothing has to reference it). No central list to edit.
Callbacks: `init`, `event`, `create_view`, `show`, `hide`, `create_card`,
`glance`. Invalid or duplicate ids are left out with an error.

Differences from the first sketch: no settings schema yet (Weather keeps
its settings on its own screen), no flags, no "settings changed" or "sleep"
events (waking from deep sleep reboots anyway).

### 6.3 Services the platform gives modules

| Service | API | State |
|---|---|---|
| UI kit | `ui_kit.h`: layout, `COL_*`, fonts, icons, `ui_panel`, `ui_btn*`, `ui_label`, `ui_flex` | Done, with `ui_spinner` and `ui_status` (loading, error and empty states); list rows, toggles and chips still private to `ui` |
| Dialogs | `ui_toast()` | Done; `ui_modal()` to do (generalize the low-battery card) |
| Text entry | `ui_kb_open_local(value, title, max, done, cancel)` | Done |
| UI thread | `hub_ui_async(fn, arg)` | Done |
| HTTP | `hub_http_get(url, max_len, done, ctx)`, one shared task | Done; HTTPS with the certificate bundle, PSRAM bodies |
| Storage | `hub_kv_*` (NVS `m_<id>`), `hub_file_path/read/write` | Done; `/sdcard/<id>/` with section 10 |
| Time | SNTP, `hub_time_valid()`, `hub_time_local()`, UTC offset | Done; offset comes from Weather until there's a time-zone setting |
| Events | network up/down, screen on/off, minute | Done, on the controller task |
| Settings | schema rendered by a Settings page | Settings screen done (2026-10-13); the schema for module settings to do |
| On/off at runtime | Settings toggle per module | Done (2026-10-13): `hub_module_set_enabled()`, kept in NVS `hub_off` |

### 6.4 View system changes

Done in part: one shared module view whose content is swapped
(`VIEW_MODULE`), a Home dashboard (`VIEW_DASH`), the top bar with tabs,
context and glance (section 5, design A with E's switcher), and the local
keyboard no longer showing the Streamer's rail. The view descriptors are
a table in `ui.c` (`k_view_flags`).

## 7. The Weather module

Built (2026-10-13), `firmware/remote/modules/weather/`. Differences from
this plan: the large icons are Lucide glyphs at 96 px (`font_weather_96`),
not Meteocons images, which saves flash and needs no image pipeline; the big
numbers are Sora 600 at 88 px; settings live on the Weather screen (place,
°C/°F) until modules get a settings schema (6.3); the glance is always on;
and it shipped before a time-zone setting (the place's UTC offset sets the
clock). Sections 7.1 to 7.4 are the original plan, kept for reference.

### 7.1 Data

- **Open-Meteo** (no API key). One request every refresh:
  `current` (temperature, feels like, weather code, day/night, wind,
  humidity, precipitation), 24 hours of `hourly` (temperature, code,
  precipitation chance, day/night), 7 days of `daily` (code, max/min,
  precipitation chance, sunrise, sunset, UV), `timezone=auto`,
  `timeformat=unixtime`. Measured: about 2 KB per response.
- **Location search** with Open-Meteo's geocoding API, typed on the
  on-screen keyboard, picked from a list. The result includes the time zone,
  which can also set the remote's clock zone.
- **Terms:** free for non-commercial use, data under CC BY 4.0 with an
  attribution link next to the data ("Weather data by Open-Meteo.com").
  That small line is a license requirement, not filler, so it stays.
- **Refresh:** when opened if older than 10 minutes, every 30 minutes while
  the screen is on, after waking if stale, never in deep sleep; back off on
  errors (1, 2, 5, 15 minutes). Keep the last good forecast in
  `/storage/weather/last.json` so the screen fills at once after boot.

### 7.2 Screen (full width, 1000 x 532)

```
+----------------------------------+---------------------------------------------+
| Sydney             Updated 10:42 | Next 24 hours (scrolls sideways)            |
|                                  |  Now  11   12   13   14   15   16   17 ...  |
|   [large icon]   23°             |  [ic] [ic] [ic] ...                         |
|   Partly cloudy                  |  23°  24°  25°  ...                         |
|   Feels 25°  H 26°  L 17°        |  10%  20%  60%  ...                         |
|                                  +---------------------------------------------+
| [Wind 14 km/h] [Humidity 62 %]   | 7 days                                      |
| [UV 7]  [Sunrise 06:02 / 17:48]  |  Today [ic] 20%  17° ---[====]---- 26°      |
|                                  |  Tue   [ic] 60%  15° -[====]------ 22°      |
|                                  |  ...                                        |
| Weather data by Open-Meteo.com   |                                             |
+----------------------------------+---------------------------------------------+
```

- Day temperature ranges drawn as bars on the week's scale, colored from
  cool to warm.
- **Icons:** small icons from Lucide (already in the icon font pipeline;
  add about 20 weather glyphs, tinted: sun amber, rain blue, storm violet).
  The large "now" icon: colorful Meteocons (MIT) rendered to images, about
  12 KB each, roughly 20 needed. Mapping table from WMO weather codes plus
  day/night to icons and text lives in one place.
- **Big numbers:** a new Sora 600 font at about 96 px with only the
  characters `0-9 ° -`, so it stays small.
- **States:** no location yet (one "Set location" button), loading,
  showing old data ("Updated 2 h ago" in amber), offline, error.
- **Glance:** cloud/sun icon and temperature in the top bar (setting:
  show or hide).

### 7.3 Settings

Location (search), units (°C/°F, km/h, mph or m/s, mm or inch), show in top
bar. Possibly several locations later.

### 7.4 Platform work Weather needs first

SNTP time sync and a time zone setting (none today), the HTTP worker, the
settings schema and page, the module view, the UI kit header, new fonts and
icons. That's most of the framework, which is the point: Weather proves it.

## 8. TVs over IR (Samsung becomes optional)

Built 2026-10-14 (`docs/ir-tvs.md`), with one change of plan: Flipper-IRDB
turned out to license only files contributed after commit 2319685 (August
2025), which leaves 2 of its ~400 TV files. Decided (2026-10-14): clean
sources only, so the code sets come from SmartIR (MIT, ~25 TVs, 18 brands
with the CC0 files) and every missing key can be learned from the TV's own
remote. Hisense and Panasonic have no clean set yet. Done: the TV profile,
the generic TV panel, Add a TV, learning single keys, the volume setting,
SIRC/RC5/RC6 encoders with host tests, the importer. Not done: the SD-card
full set, a guided learn-every-key flow.

### 8.1 What changes

- **TV profiles**: a profile is a brand plus a code set: codes for a
  standard list of keys (power, power on/off, volume, mute, channel,
  input, HDMI 1-4, arrows, OK, back, home, menu, settings, info, exit,
  digits, play/pause). Built differently from the first idea of JSON files:
  the code sets are compiled in (`main/tv_codes.c`, generated), the chosen
  set is kept in NVS (`ctrl`/`irtv_set`), and only learned keys are files.
- The Samsung panel becomes a **generic TV panel** that shows only the keys
  the profile has.
- **Add a TV:** Remotes > Add a TV > brand > the remote tries code set 1
  ("Did the TV react?" Yes / Try next) > save. "Learn from its remote"
  stays as the alternative.
- **Volume:** a setting, "TV volume through: Google TV (HDMI-CEC) / the
  TV's IR". Default Google TV, which is how this remote works today. Many
  people lack CEC, so IR must be an option.
- Samsung isn't preinstalled. Done: the old built-in Samsung codes are
  SmartIR's set 1063, picked like any other brand.

### 8.2 Code source

[SmartIR](https://github.com/smartHomeHub/SmartIR) (MIT) is the main
source, plus the [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB)
files that are CC0 (those contributed after commit 2319685; older ones are
not licensed). `tools/ir/import_tv_codes.py` picks them, normalizes key
names and writes `main/tv_codes.c`, recording each set's source
(`docs/ir-tvs.md`). Not usable: older Flipper-IRDB files, Flipper's own
`tv.ir` (GPL-3.0), probonopd/irdb (its license demands notices and free
units), the LIRC database (no license). Still to do: a full set for the SD
card.

### 8.3 IR encoders to add

Built: NEC, Samsung32, Sony SIRC (12/15/20 bit), RC5/RC5X, RC6 (mode 0)
and raw, each with golden-vector host tests in `components/ir/host_test`.
Still to add: NECext and Kaseikyo (Panasonic), with RCA/Sharp/JVC if needed.
Port from Arduino-IRremote (MIT) or the public-domain IRP protocol
definitions, not from LGPL code.

### 8.4 Fixes found on the way

Fixed: the "HDMI switch" learn preset no longer collides with the Inputs
page (a second one becomes "HDMI switch 2"), and Learn > Done returns to
Settings (the Devices screen is gone). Deleting and renaming learned
remotes and their buttons: done 2026-10-16 (Edit on the remote's screen).

## 9. Setup experience

### 9.1 Web installer (flash from the browser)

Built 2026-10-14 and tested on a board (no erase): `hm-web.ps1`,
`web/index.html`, ESP Web Tools 10.4.0 vendored (a failed install offers
Close instead of Back). The release build (developer tools off) and the
GitHub workflow that publishes it to Pages (on a version tag,
`docs/releasing.md`) followed the same day. Still to do: the first-board
walk-through with pictures.

- A GitHub Pages site with [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
  (Chrome, Edge, Opera; Firefox 151+). Four-part manifest: bootloader,
  partition table, `ota_data_initial`, app. Pairing and learned remotes
  survive unless the user ticks "erase".
- Binaries are copied into the Pages site by the release workflow (GitHub
  release downloads lack CORS headers, so browsers can't fetch them).
- **First install on a new 7B:** the native USB port only appears after our
  firmware switches the board's USB/CAN selector, so a fresh board needs
  BOOT held while plugging in, or the UART port with SW1 on UART1 (then
  back to UART2 for the IR transmitter). The page says to hold BOOT if it
  can't connect; a walk-through with pictures is still to do.
- After the first install, updates come over Wi-Fi (9.4).

### 9.2 Setup on the device (first run)

Wi-Fi done (2026-10-14): the network picker in setup and Settings > Wi-Fi,
credentials in NVS, `secrets.env` optional. Still to do: the welcome,
time-zone, companion, TV and module steps below. The update token is made
on the device (NVS `ota`, shown in Settings > About) and copied into the
PC's secrets.env.

Welcome > **Wi-Fi** (scan list, password on the keyboard; stored on the
device, so `secrets.env` becomes optional and for developers only) > **time
zone** > **find Google TV** > **pair** (code from the TV) > **companion**
(9.3; the screen shows whether it's detected) > **TVs over IR** (add a
brand, or skip) > **modules** (switch on; Weather asks for a location) >
done. Every step can be skipped and redone later from Settings.

Optional later: Improv Wi-Fi over USB, so the web installer can send the
Wi-Fi details right after flashing.

### 9.3 Installing the companion app

Ranked by effort for the user:

1. **No PC (to verify first).** Android 13+ blocks enabling accessibility
   services of sideloaded apps ("restricted settings"), but AOSP's TV
   Settings app doesn't seem to apply that check. If it holds on the
   Streamer, the user installs `companion.apk` from the project's site
   (GitHub Pages carries it) with the free Downloader app and enables it in
   Settings > System > Accessibility. **Experiment needed** (the Downloader
   test, section 13). It can't work on Google TV setups that have
   `no_install_unknown_sources` (found 2026-10-15: Settings > Apps >
   Security & restrictions > Unknown sources is greyed out for every app).
2. **Phone app** (e.g. Bugjaeger): pair with Wireless debugging, install,
   run one command. A planned fallback, not tried.
3. **PC script**: `companion/install.py`, the cross-platform version of
   `install.ps1` (built 2026-10-16).
4. **Stretch: the remote installs it itself** over ADB Wireless debugging
   (TLS 1.3 + SPAKE2 pairing, then install and enable). Feasible: our
   mbedTLS has what's needed except the Ed25519 SPAKE2 maths, which would be
   ported from BoringSSL (Apache-2.0). About 2-4 weeks. A standout feature
   for a later release.

Publishing on Google Play is possible but heavy for a hobby project
(accessibility declaration, TV review, 12 testers for 14 days).

### 9.4 Updates

Settings > About shows the version and "Update available" by reading the
release manifest on the Pages site, then downloads over HTTPS into the
spare slot (rollback stays on). Local updates from a PC (`hm-ota`) stay as a
developer option, off in release builds.

Built 2026-10-15 (`main/update.c`): `tools/web/make_site.py` writes
`update.json` (version from the image itself; for each board its image's name, size and SHA-256)
next to the installer; the remote checks it a minute after Wi-Fi comes up,
then daily, and from Settings > About (Check, then Install). A web page
can't push updates instead: an HTTPS page can't send to an HTTP device on
the home network, and release builds have no update server. Tested the
same day with two builds served from the PC (0.1.0-dev, then 0.1.1-test):
the automatic check found the newer one, Install showed its progress and
restarted into the spare slot, the image was confirmed on Wi-Fi and the
next check said up to date. Until the first release the Pages site has no
`update.json`; that 404 shows as up to date, not as a failure. Still to do:
a signature on the image (it is trusted through HTTPS and the site). A dot
on the Settings gear shows when an update is waiting.

**The companion** (0.5, built 2026-10-15): `update.json` also carries the
release's companion (only an APK signed with the project's key), the remote
shows its version against the TV's in Settings > Remotes, and Update
companion makes the companion download and install it itself
(`PackageInstaller`, silent where Android allows it). Tested with a test
0.4 and 0.5 served from the PC: offered, downloaded and checked, then
Android's turn. The Streamer it was tested on forbids installs from apps
(`no_install_unknown_sources`; Unknown sources greyed out for every app),
so its installer closed without a word: the companion now reports that
(`"updates":false`) and the remote only shows the versions. Still to try on
a TV that allows it: the dialog and the silent update.

### 9.5 Custom builds

Planned, not built (menuconfig works today).

- **Developers:** menuconfig, or `tools/hm.py configure` (a small
  cross-platform script: pick modules and options, writes
  `sdkconfig.defaults.local`, builds, flashes).
- **No toolchain:** the "Build custom firmware" GitHub Action in a fork
  (checkboxes for modules and TV profiles), then the web installer's
  "use your own file".

## 10. SD card and storage

- **Interface:** SDMMC in 1-bit mode on GPIO12 (CLK), GPIO11 (CMD), GPIO13
  (D0), as Waveshare's demos do. The card's D3/CS line is held high through
  the IO expander, which our firmware already does. Faster and simpler than
  SPI with chip select on the expander.
- **Mounted on demand** (there's no card-detect line), FAT with long file
  names enabled.
- **Holds:** the full IR database, optional asset packs, module caches,
  logs. Never code. Everything works without a card.
- **`assets` partition:** the free 1.875 MB of flash could become a
  read-only LittleFS partition for data such as a larger IR set, flashed by
  the web installer and replaceable without a firmware change (weather
  icons turned out to be font glyphs, and the companion APK is on the
  Pages site). `nvs` and `storage` stay where they are.
- To measure: the card's current in deep sleep (it stays powered), and PSRAM
  bandwidth while the screen is scanning.

## 11. Open-source cleanup (release checklist)

From the repo audit (2026-10-12):

**Must fix before publishing**
- [x] `git init`; a root `.gitignore` (build dirs, `__pycache__`, logs,
      `secrets.env`, `sdkconfig`, keystores). Done 2026-10-13, local only.
      The local history holds personal data and the research, so the public
      repo started from a fresh history (one commit), pushed 2026-10-16
      (`docs/releasing.md`).
- [x] **License:** Apache-2.0 for code (decided; same as ESP-IDF and
      androidtvremote2, includes a patent grant), CC BY-SA 4.0 for the
      enclosure (a remix of a CC BY-SA case). `LICENSE` and `NOTICE` added 2026-10-14 (androidtvremote2
      port, ESP-IDF and registry components, LVGL, esp_littlefs/littlefs,
      ESP Web Tools, fonts (OFL), Lucide (ISC), SmartIR (MIT),
      Flipper-IRDB (CC0), Open-Meteo (CC BY 4.0), Waveshare and ESPHome as
      references). The enclosure's own license file came with its files
      (2026-10-16, `hardware/enclosure/LICENSE`).
- [x] **Remove the companion's `debug.keystore`** from the tree;
      `build` makes a key per builder. Releases are signed in CI with a
      secret key. Checked 2026-10-14: it was never committed (`*.keystore`
      is gitignored), and `build.ps1` makes one on a builder's first build.
      A developer's own key stays on their PC; an app installed with it
      needs a reinstall to move to release builds. CI signs tag builds
      with the project key from the repository secrets; other builds use a
      throwaway key (`docs/releasing.md`).
- [x] **Drop the scraped brand logos** (`app_icons.c`, `tools/app_icons.py`).
      Built-in apps use the companion's icons or the existing monogram tiles.
      Done 2026-10-14 (the app panel's header got a letter tile too).
- [x] **Personal data:** the board's MAC, home IP addresses, COM ports,
      Windows paths, "Living room" (also a hard-coded label in the top bar),
      references to another project (including one on the power-test screen),
      dated diary notes, product purchase notes. Done 2026-10-14 for the
      code, scripts, CLAUDE.md and the READMEs: the author's own setup moved
      to `CLAUDE.local.md` (gitignored), examples use COM5 and 192.168.1.x,
      `hm-idf.ps1` finds ESP-IDF without a personal path (HM_IDF_PATH,
      HM_IDF_TOOLS_PATH, HM_IDF_PYTHON_ENV override), and the top-bar label
      was already gone. The READMEs lost their dated notes and personal
      test state the same day (a "Tested on hardware" list instead). PLAN
      checked again 2026-10-15; the working diary (`TODO.md`) became
      GitHub issues at the first publish. The research is in `private/`
      (gitignored).
- [~] **Security:**
  - [~] The companion's HTTP API is unauthenticated and can type into any
    field: add a pairing token, shown on the TV once and entered on the
    remote. Built 2026-10-14 (companion 0.4, remote firmware; works with
    0.3 too): a 6-character code on the TV, Settings > Remotes > Pair
    companion, token in NVS. Checked on the Streamer 2026-10-15. Still
    plain HTTP: TLS would also hide typed text from the network.
  - [x] The Wi-Fi log port is unauthenticated: off in release builds
    (2026-10-14).
  - [x] Local OTA uses HTTP and a token: developer builds only
    (2026-10-14). Every build updates from the releases over HTTPS (9.4,
    built 2026-10-15); release builds have no other way.
  - [x] Document the threat model in `docs/security.md` (2026-10-14), with
    `SECURITY.md` for reports (GitHub private vulnerability reporting,
    switched on when the repository goes public).
- [x] Recreate the missing generators for `atv_keycodes.h` and
      `atv_golden.h` (2026-10-14): `components/atv_remote/tools/`
      (`gen_keycodes.py`, `gen_golden.py`), protos vendored in
      `components/atv_remote/proto/` (androidtvremote2 v0.3.2). Both
      reproduce the committed headers byte for byte.

**Tidy**
- [x] Kconfig submenus (2026-10-14): Google TV, Display, Power, IR,
      Updates, Modules, and Developer build (`HM_DEVELOPER`: updates from a PC,
      devtools, Wi-Fi log, self-test, battery preview). `HM_SCREEN_OFF_TEST`
      and `HM_IR_TX_DIAG` are gone (both had done their job); the boot IR
      check defaults off; the boot self-test is optional. Release builds
      (`sdkconfig.release`, `hm-web.ps1`) turn `HM_DEVELOPER` off.
- [~] Per-unit values become settings: screen rotation done 2026-10-15
      (Settings > Screen > Rotate 180°, applied at once; the menuconfig
      default is off for a bare board and only sets a new remote's first
      start). mDNS name: ESP-IDF's mDNS renames a remote whose name is
      taken (`hubmote-2.local`), and About shows the name in use. Still:
      battery ADC calibration (one constant from this board, about 1%
      low; an automatic one at the end of a charge would suit every
      board).
- [x] Names: `UI_TARGET_TV` became `UI_TARGET_STREAMER` (2026-10-15);
      one project name everywhere: Hubmote, done 2026-10-14 (UI, mDNS
      `hubmote.local`, project `hubmote`, Kconfig `HM_*`, scripts
      `hm-*.ps1`, companion `io.github.dovahbrownies.hubmote` on the GitHub
      owner's domain). NVS namespaces and the folder names stay.
- [x] Split long files: `controller.c` done 2026-10-15 (`ctrl_setup.c`,
      `ctrl_companion.c`, `ctrl_screen.c`; 1150 lines became four files,
      now 260-480 each). `atv_remote.c` (about 1700 lines) stays whole: it is one task's
      state machine around a shared connection context, and the protocol
      pieces already have their own files (messages, protobuf, identity,
      pairing secret, discovery). `ir_ctl.c` (about 670) stays too.
- [x] Remove dead code and unused icons; merge duplicated helpers
      (2026-10-15): three unused functions gone (`board_touch_sleep`,
      `ir_ctl_ready`, `net_is_up`); the copied `wrapped()` label became
      `ui_label_wrap()` in the UI kit. The six icons nothing uses yet
      (HDMI switch, keyboard-off, four weather ones) stay: they are part
      of the icon set modules get.
- [x] Version from git tags (`PROJECT_VER`). For now `firmware/remote/version.txt`
      (`0.1.0-dev`) sets it, shown with the build time in Settings' top-bar
      chip. On a `v*` tag CI writes the tag's version into `version.txt`
      (`version.txt` wins over git).
- [x] Cross-platform tooling (2026-10-16): `tools/hm_ota.py`,
      `tools/hm_netlog.py` and `companion/install.py`, standard-library
      Python; `companion/build.ps1` already runs under pwsh on Linux (CI),
      and on macOS and Linux `idf.py build flash monitor` covers building,
      USB flashing and the USB log. The PowerShell scripts stay as Windows
      conveniences.
- [x] `power-test` stays as `firmware/power-test` (decided 2026-10-16): a
      small standalone firmware with its own README and results table,
      still needed for the battery measurements; not built by CI.
- [x] `TODO.md` left the repository; its open items are GitHub issues
      (#6-#46, 2026-10-16).
- [x] Research stays private for now (decided 2026-10-13): `reports/` and
      `research_notes/` are in the git-ignored `private/` folder.
- [x] Enclosure files in `hardware/enclosure/` (STL and 3MF, 2026-10-16),
      under CC BY-SA 4.0 (its own `LICENSE`), as the case they remix:
      Apache-2.0 is a code license and fits 3D models poorly. Print settings and photos could follow.
- [x] A code of conduct (Contributor Covenant 2.1, 2026-10-16); reports go
      privately through the repository's Security tab, no email.
- [x] A screenshot endpoint for docs images and remote debugging:
      `GET /screenshot`, `POST /tap`, `POST /swipe` on the update server
      (`CONFIG_HM_DEV_TOOLS`, `tools/devtools.py`). Off in release builds
      (2026-10-14).

## 12. Docs for the release

Done (2026-10-16): the 945-line README became a short front page and
guides, one per reader, each fact in one place:

```
README.md              what it is, a screenshot, what you need, 3-step start, links
docs/
  getting-started.md   web installer, first start, pairing, volume, updates
  using.md             the screens and settings
  companion.md         installing, pairing, updating the companion
  hardware.md          IR wiring, SW1, battery, power switch, the case
  troubleshooting.md   common problems, logs
  building.md          developers: PC setup, secrets, build, flash, updates over Wi-Fi
  porting.md           the boards, adding one
  modules.md, themes.md, ir-tvs.md, security.md, releasing.md, PLAN.md
firmware/remote/README.md, companion/README.md, sim/README.md   the code
CONTRIBUTING.md  CODE_OF_CONDUCT.md  SECURITY.md  LICENSE  NOTICE
CLAUDE.md              agent rules without personal facts (those are in a
                       local, ignored file)
```

Not written: `architecture.md` (the firmware README's code layout covers
it for now), building in Docker.

CI (GitHub Actions, `.github/workflows/build.yml`, `docs/releasing.md`):
host tests (IR codecs, theme contrast, Weather parsing, the generated
headers against their generators), the UI simulator at every board's size,
a developer and a release build for every board, the installer site, the
companion APK, and on `v*` tags Pages and a release. It first ran on
GitHub on 2026-10-16, green on every job. Not in it yet: a battery-detector
host test, module size reports (section 4).

## 13. Phases

Each phase ends with something working on the device.

| Phase | Content | Done when | State |
|---|---|---|---|
| 0. Decisions and experiments | Section 14; the Downloader test | Answers in this file | Decisions taken; Downloader test not run (blocked on Google TV setups with `no_install_unknown_sources`) |
| 1. Clean foundation | Section 11 must-fix and tidy items, docs skeleton, git, CI build | A fresh clone builds in CI; no personal data left | Done: git, license, personal data, generators, CI, Kconfig tidy, long files split, the docs split (section 12), the issues |
| 2. Platform | Wi-Fi setup on the device, SNTP and time zone, Settings screen, Remotes/Modules/Settings split, view descriptors, UI kit header, toast/modal, HTTP worker, events, module registry and settings schema, screenshot endpoint | An empty "Hello" module appears, can be switched on/off and has a settings page | Part done: registry, events, HTTP, storage, SNTP, UI kit, toast, navigation, screenshot endpoint, module template, Settings screen, runtime on/off, Wi-Fi setup, themes, view descriptors (`k_view_flags`). Open: time zone, settings schema, modal |
| 3. Weather | Section 7 | Weather runs for a week on the remote | Built; the week-long run is still to do |
| 3b. Card modules | Card runtime (data sources, bindings, actions, permissions), catalog on GitHub Pages, install from catalog or SD, two example cards | A card installed from the catalog without reflashing | |
| 4. TVs over IR | Section 8 | A non-Samsung TV added by brand and working; Samsung via its profile | Built: Add a TV by brand, Samsung through its code set (still to check at a TV). No non-Samsung TV tried yet. Open: the SD-card set, a guided learn flow |
| 5. Setup experience | Section 9 | Someone else sets one up from the README alone | Part done: web installer, Wi-Fi setup, updates from the releases, companion pairing and updates. Open: the other first-run steps, the companion without a PC, the walk-through with pictures, custom builds (9.5) |
| 6. SD card and assets partition | Section 10 | Full IR database browsable from SD | |
| 7. Release 0.1 | Docs, security items, demo media, repo published | Public repo, tagged release, working web installer | Part done: the repo on GitHub (private), CI green, the guides, photos. Open: going public, the signing key, tagging v0.1.0, a demo clip |
| 8. After 0.1 | Next modules (section 15: Now Playing, Sleep timer, Home Assistant first), the remote installing the companion itself, idle screen | | |

**The Downloader test** (phase 0, about 10 minutes at the TV): the steps
are in docs/companion.md ("On the TV, without a PC"). If Android allows both
the install and the accessibility switch, setup needs no PC at all. It
can't work on Google TV setups with `no_install_unknown_sources`
(Downloader included), so first find out whether a setting lifts it.

## 14. Decisions

Taken (2026-10-13):

1. **License:** Apache-2.0 for code; Creative Commons for the enclosure
   files: CC BY-SA 4.0, as it remixes a CC BY-SA case (3D_Project3R's on
   Printables; planned as CC BY 4.0 until that was known, 2026-10-16).
2. **Modules:** native modules are chosen at build time (section 4);
   card modules installed on the device come on top (decision 8).
3. **Theming:** part of the repo (section 5a).
4. **Research notes:** private for now.
5. **Enclosure:** published with the project.
6. **Security system:** Eufy was the example looked at. It has no local API. The
   community route is Home Assistant: the `eufy_security` integration with
   the `eufy-security-ws` bridge gives an alarm panel with arm home, arm away,
   disarm and custom modes. That stack broke in mid-2026 and the bridge is
   now deprecated in favour of "Eufy Mega Security", whose HomeBase mode
   control is still a feature request. Talking to Eufy directly from the
   ESP32 isn't realistic (reverse-engineered, encrypted, changes often).
   Without Home Assistant, Eufy would need a small bridge running on an
   always-on machine (the Eufy client in Docker), with a native module
   talking to it. Parked until the module system exists and a maintained
   bridge is clear.

7. **Navigation:** A (top tabs) with E's switcher sheet behind the remote
   tab (2026-10-13).
8. **Modules:** both kinds (section 4); contributing must be easy: a
   template, one folder per module, a checklist, CI that validates modules.
9. **Name:** Hubmote ("a touchscreen remote and home hub for Google TV").
   Renamed in one go on 2026-10-14 (repo, mDNS name, scripts, companion
   package; section 11).
10. **Order:** the Weather module first, with the platform pieces it needs.

## 15. Next modules (brainstorm, 2026-10-16)

What fits the hardware: one HTTPS request at a time through the shared
worker, internal RAM as the real limit (no long-lived TLS sessions per
module, no BLE stack next to Wi-Fi), PSRAM for data and images, plain HTTP
or MQTT on the home network costing little. Local APIs over cloud ones
where both exist: no account, no key, nothing leaves the house.

| Module | What it does | How | Kind | Needs from the platform | Effort |
|---|---|---|---|---|---|
| **Now Playing** card | Home card with the TV's title, app icon, play/pause, ±10 s; later the artwork | The companion's `/media` (0.6); artwork as RGB565A8 like icons (`MediaMetadata` art) | Native, in core | A hub API for companion data (now playing, apps), so modules don't reach into `ctrl_*` | S |
| **Sleep timer** | Turns the TV off (IR power, or the Streamer's power key) after 15-120 min; a countdown glance | No network; a timer on the controller task | Native | An actions API (send a TV key, an IR code) | S |
| **Home Assistant** | Lights, switches, scenes, climate, the alarm panel (covers Eufy through its integration), cameras' snapshots | Local REST API with a long-lived token, or its WebSocket API for live states (plain HTTP on the LAN: no TLS memory) | Native | Settings schema (URL, token), a WebSocket client in the HTTP worker, JPEG decode for snapshots | L |
| **MQTT** | Toasts from topics (doorbell, washer done), buttons that publish | ESP-IDF's MQTT client, one connection | Native | Settings schema; `ui_toast` from any task (exists via `hub_ui_async`) | M |
| **Wake and play** | Wakes a gaming PC (Wake-on-LAN), then opens Steam Link or Parsec on the TV | A UDP broadcast, then the companion's `/launch` | Native | Actions API (launch an app) | S |
| **Air quality** | AQI, pollen, UV next to Weather | Open-Meteo's air-quality API (no key, CC BY 4.0), same pattern as Weather | Native, or a card | Nothing new | S |
| **Calendar** | Today's and tomorrow's events | A private ICS link (Google, iCloud, Outlook all give one); parse VEVENT lines | Native (ICS isn't JSON) | Settings schema (the link is a secret) | M |
| **Philips Hue** | Rooms, scenes, brightness, without Home Assistant | The bridge's local API (v1 over HTTP; pairing by pressing its button) | Native | Settings schema | M |
| **Plex / Jellyfin** | Continue watching, recently added, what is playing; tap to open it on the TV | Their local APIs with a token (sessions give title, artwork, progress); open with the app's deep link | Native | Actions API (deep link) | M |
| **YouTube link** | What YouTube on the TV is playing (title, position), play/pause/seek, optionally skipping sponsored segments | The lounge API behind "Link with TV code" (unofficial), titles from oEmbed; SponsorBlock's public API for segments | Native | One long-poll HTTPS connection (internal RAM: check it fits next to the Streamer's TLS session) | M |
| **Guest Wi-Fi** | A QR code guests scan to join the guest network | LVGL's QR code widget (`LV_USE_QRCODE`); the network and password kept in NVS | Native | Settings schema | S |
| **ESPHome devices** | Sensors and switches from ESPHome boards on the home network | Their built-in web server's REST and event stream (plain HTTP, no account) | Native | Settings schema | M |
| **Spotify** | Artwork, next and previous, choosing a speaker | Spotify's Web API (cloud, OAuth with PKCE; one HTTPS request at a time) | Native | Settings schema, an OAuth device flow on the remote's screen | M |
| **Idle screen** | A clock, the Weather glance and the next event while charging, instead of turning the screen off | Uses the other modules' glances | Core | A screen-saver hook in `ctrl_screen.c` | M |
| **Scenes ("movie night")** | One tap: HDMI input over IR, open an app, dim the lights, TV volume | A list of actions, edited on the remote | Core, then cards | Actions API; a scene editor | L |
| Pi-hole, server status, solar output, stocks, transit, scores | Numbers from a JSON API | Card modules (section 4), once the card runtime exists | Card | Card runtime | — |

**Platform work these share**, in order of how many modules need it: the
settings schema (6.3), an actions API (keys, IR codes, app launch, deep
links) for modules and scenes, a hub API for companion data, then a
WebSocket client and JPEG decoding. Secrets such as tokens would sit in NVS
like the others (`docs/security.md`: not encrypted).

**Suggested order:** Now Playing card and Sleep timer first (small, no new
services, and they make the actions and companion APIs real), then Home
Assistant (the largest ecosystem; it also covers Eufy and cameras), then
MQTT and Wake and play. Air quality is a good first module for a new
contributor.

**What is playing, app by app** (looked into 2026-10-16, and checked at the
TV the same day: YouTube shares the video and channel; Netflix
no metadata; Prime Video only "PrimeVideo"; Plex no session at all).

The one general source is Android's media sessions, which the companion
reads (`/media`, 0.6): an app shares a title if it fills in its session's
metadata, the same data Google TV's own "now playing" uses. On the
Streamer, Netflix, Prime Video, Spotify and Sling publish sessions, and
YouTube while playing; Plex doesn't.

Other routes, and why they don't help in general:
- The remote protocol (Android TV Remote v2) reports the foreground app's
  package, nothing about what it plays.
- Accessibility (the companion is an accessibility service): Netflix,
  YouTube and Prime Video draw their interface as one surface, so there is
  no text to read. Reading other apps' screens would also go beyond what
  the companion promises (it never reads a field's contents).
- Google TV's "Continue watching" (the TV provider's Watch Next rows): only
  the system may read other apps' entries.
- Notifications: TV apps rarely post media notifications, and the
  companion's listener reads none by design.
- Netflix itself: no public API; its second-screen protocol is private and
  tied to the account; viewing history exists only on its website.
- Cast: the Streamer's Cast status describes sessions cast from a phone,
  not apps opened on the TV.

Routes that work for particular apps:
- **YouTube:** the "Link with TV code" protocol phone apps use (the lounge
  API) reports the video playing and its position, and can play, pause and
  seek; the title comes from YouTube's oEmbed (no key). Unofficial, but
  open-source tools have relied on it for years. A module candidate (table
  below).
- **Plex, Jellyfin, Emby:** their servers' session APIs (local, with a
  token) report what each player is playing, with artwork and progress:
  the Plex / Jellyfin module can show it. For Plex that is the only way,
  as its TV app publishes no session.
- **Spotify:** its session already has title and artist; its Web API
  (cloud, OAuth) would add artwork.
- **Kodi:** JSON-RPC on the home network.

So Netflix's and Prime Video's titles can't be had, and the app panel says
these apps don't share them (as for Plex until a Plex module).

## 16. Other boards and screen sizes (2026-10-16)

The firmware was written for one board, the Waveshare ESP32-S3-Touch-LCD-7B.
What is specific to a board already sits behind one small interface
(`components/board/include/board.h`: start-up, touch, the RGB panel, the
backlight, deep sleep, the battery); outside it only `main/display.c` (the
RGB bounce-buffer path) and the panel's size depend on the board. The UI is
a different matter: about 210 sizes in pixels across 15 files, laid out for
1024x600, and fonts drawn at fixed sizes.

Stages:
1. **Done (2026-10-16):** the board is a choice (`-D HM_BOARD=<id>`; a
   header and a `.defaults` file per board in `components/board/boards/`,
   code shared by chip), IR pin
   defaults per board, a build for another resolution stops with a clear
   message, and [porting.md](porting.md) says what a port takes.
2. **Done (2026-10-16): a PC simulator** of the UI
   (`firmware/remote/sim`): the real UI drawn into memory by LVGL at any
   size, one PNG per screen, in CI on every push. The UI no longer reads
   ESP-IDF state itself (the controller pushes Settings > About). At
   1024x600 it matches the remote's screenshots pixel for pixel, on
   Windows and Linux alike.
3. **Done (2026-10-16): a layout that follows the screen.** Pixel sizes go
   through `UI_S()` (a compile-time scale by the screen height; the
   identity at 600 px, so the 7B's screens didn't change by a pixel),
   width-filling grids divide the width, and fonts are generated per
   screen height (`components/ui/fonts/h600`, `h480`). Checked at 800x480,
   1024x600 and 1280x800.
4. **Done (2026-10-16), untested: Waveshare's other S3 RGB boards.** The 7,
   5B, 5, 4.3, 4.3B and 4.3C, from Waveshare's schematics, demos and BSP
   (`docs/porting.md` has the table). Boards share code by chip
   (`waveshare_s3_rgb.c` with `waveshare_ch422g.c` or
   `waveshare_ch32v003.c`); a board is a header and a `.defaults` file,
   built with `-D HM_BOARD=<id>`. Boards without dimming, a battery reading
   or IR pins leave those out of the UI. CI builds every board; the
   installer has a board picker and `update.json` an image per board. What
   is left is someone running each one ([issue #44](https://github.com/DovahBrownies/Hubmote/issues/44)). Not supported: the 7C (other I2C pins, a 16-bit
   expander, octal flash).
5. **A compact layout** for small, portrait or round screens (SPI/QSPI
   panels, a second display path): only if someone wants it; the rail,
   panel and touchpad don't fit there.

Hard limits whatever the board: an ESP32-S3 with Wi-Fi, 8 MB of PSRAM,
8 MB of flash (`partitions_8mb.csv`; 16 MB for `partitions.csv`), a
touchscreen.
