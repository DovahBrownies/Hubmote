# Writing a module

A module is an optional feature reached from the **Home** tab: a card on
the Home screen, a full screen of its own, and optionally a short "glance"
in the top bar (Weather shows an icon and the temperature). Each module is
one folder in `firmware/remote/modules/` and nothing else in the firmware
has to change to add one.

These are native modules, compiled into the firmware and switched on or off
in menuconfig. Card modules, which are data files installed on the device
without reflashing, are planned (`docs/PLAN.md`, section 4).

`modules/weather/` is the complete example: web requests, a cache file,
settings, a glance, a Home card, a full screen and a host test.

## Quick start

From `firmware/remote/`:

```powershell
python tools\new_module.py transit "Transit" --description "Next departures from my stop." --author "your-handle"
.\scripts\hm-idf.ps1 reconfigure
.\scripts\hm-idf.ps1 build
.\scripts\hm-ota.ps1          # or flash over USB: docs/building.md
```

`reconfigure` is needed once after adding or removing a module folder, so
the build and menuconfig find it. The new module is on by default
(menuconfig: *Hubmote > Modules*). It shows up as a card on the Home
tab and in the switcher; the template is a tap counter that keeps its count
across restarts.

People can switch modules off on the remote (Settings > Modules). An off
module stays in the firmware but gets no `init` or events and shows
nowhere. Switching it on later runs `init` then (once) and sends
`HUB_EV_NETWORK_UP` if the network is up; switching it off just stops the
events, so keep nothing running that events don't control.

The id (`transit`) is lowercase letters, digits and `_`, starting with a
letter, at most 12 characters. Up to 24 modules can be registered
(`HUB_MODULES_MAX`). It names the folder, the source file, the Kconfig option
(`CONFIG_HUB_MODULE_TRANSIT`) and the module's storage, so pick one you
won't need to change. It is also an ESP-IDF component name, so it must not
be another component's (`main`, `hub`, `ui`, `ir`, `board`, `json`,
`log`...).

## What's in a module folder

```
modules/transit/
  transit.c         the module: descriptor, callbacks, UI
  CMakeLists.txt    sources and requirements (left out when the option is off)
  Kconfig.module    the on/off option and any other options
  README.md         what it does, settings, data and privacy, credits
  test/             host tests for anything that parses data (recommended)
```

Split the code the way Weather does once it grows: `*_data.c` for URLs and
parsing (plain C and cJSON, so it runs on a PC), `*.c` for state and
refresh logic, `*_ui.c` for LVGL.

### CMakeLists.txt

```cmake
if(CONFIG_HUB_MODULE_TRANSIT)
    idf_component_register(SRCS "transit.c"
                           REQUIRES hub ui
                           WHOLE_ARCHIVE)
else()
    idf_component_register(REQUIRES hub ui)
endif()
```

- `REQUIRES` must be the same in both branches: ESP-IDF works out
  requirements before it reads `sdkconfig`. Only the sources depend on the
  option, so a disabled module adds nothing to the image.
- `WHOLE_ARCHIVE` keeps the module linked in. Nothing calls into it by
  name; it registers itself (below), and without this flag the linker drops
  it.
- Add what you use to `REQUIRES`: `json` for cJSON, `esp_timer`, and so on.

### Kconfig.module

```kconfig
config HUB_MODULE_TRANSIT
    bool "Transit"
    default y
    help
        Next departures from my stop.
```

Every `modules/*/Kconfig.module` is pulled into the *Modules* menu. Put any
further build options for the module here, named `HUB_MODULE_TRANSIT_*` and
`depends on HUB_MODULE_TRANSIT`. Settings people change on the device
belong in NVS instead (see Storage).

## The module descriptor

A module fills in one `hub_module_t` and registers it:

```c
#include "hub.h"
#include "ui_kit.h"

static const hub_module_t s_module = {
    .id = "transit",
    .name = "Transit",
    .icon = ICON_GRID,
    .init = init,
    .event = event,
    .create_view = create_view,
    .show = show,
    .hide = hide,
    .create_card = create_card,
    .glance = glance,
};
HUB_MODULE(s_module);
```

The sources need no `#if CONFIG_HUB_MODULE_TRANSIT`: when the option is
off, `CMakeLists.txt` leaves them out of the build. `HUB_MODULE()` registers it before `app_main()` runs. Modules are listed by
name. A module whose id is invalid or already taken is left out, with an
error in the log (`hub: module id "..." is invalid or taken`).

Every callback is optional:

| Callback | Called | Task |
|---|---|---|
| `init()` | once at start-up, after storage is mounted, before Wi-Fi is up | controller |
| `event(ev)` | for each event below | controller |
| `create_card(parent)` | the first time the Home tab is shown; return your card | LVGL |
| `create_view(parent)` | the first time your screen is opened; build into `parent`, already `HUB_VIEW_W` x `HUB_VIEW_H` (1000 x 532 on a 1024x600 screen) | LVGL |
| `show()` / `hide()` | your screen came into view / left | LVGL |
| `glance(out)` | when the top bar or switcher redraws; return false when there's nothing to show (`text` holds 15 characters) | LVGL |

The Home tab makes the whole card open your screen when tapped, so the card
needs no button of its own. Cards are laid out in rows that wrap and
stretch to fill their row: the width you give is the minimum, and
`UI_S(480)` fits two side by side. The top bar shows one glance, from the
first switched-on module (by name) that has one; the switcher shows every
module's.

### Events

| Event | When | Typical use |
|---|---|---|
| `HUB_EV_NETWORK_UP` | Wi-Fi connected | first fetch |
| `HUB_EV_NETWORK_DOWN` | Wi-Fi lost | show "offline", stop retrying |
| `HUB_EV_SCREEN_ON` | the screen woke from dark | refresh what is stale |
| `HUB_EV_SCREEN_OFF` | the screen went dark | stop polling |
| `HUB_EV_MINUTE` | every 60 s from start-up (not on the minute) | periodic refresh, clocks |

At start-up the screen is on; no `HUB_EV_SCREEN_ON` is sent for it.

The remote is a battery device: do network work only while the screen is
on, unless the module needs otherwise (and say so in its README). Nothing
runs in deep sleep; waking from it reboots, so `init` runs again.

## Threads

Two tasks call a module, and LVGL may only be touched from one of them.

- **Controller task** (`init`, `event`): don't block here. A slow call
  stalls the remote's own control logic. Use `hub_http_get()` for web
  requests, or start a task of your own for long work.
- **LVGL task** (`create_*`, `show`, `hide`, `glance`): the LVGL lock is
  already held. Keep these quick; the screen doesn't redraw while they run.
- **Any other task** (an HTTP callback, your own task): reach the UI only
  through `hub_ui_async(fn, arg)`, which runs `fn(arg)` on the LVGL task
  with the lock held. Call `hub_glance_changed()` when your glance changes;
  it can be called from any task.

Data shared between tasks needs care. Weather's pattern: its state is
guarded by a mutex; the HTTP callback parses into a scratch copy, then
copies that into the state under the lock, and
the screen takes a consistent copy (`weather_snapshot()`) before drawing,
from `hub_ui_async()`. Hold the lock only for the copy, never while
drawing or waiting on the network.

## Platform services

`#include "hub.h"` gives all of these.

### Web requests (`hub_http.h`)

```c
static void on_answer(int status, const char *body, size_t len, void *ctx)
{
    /* On the HTTP task. status is -1 if the request failed before a reply.
     * body is NUL-terminated and freed after this returns. */
    if (status == 200 && parse(body, &s_data)) { /* simplified: guard shared data, below */
        hub_ui_async(redraw, NULL);
        hub_glance_changed();
    }
}

hub_http_get("https://api.example.com/v1/now?stop=123", 8 * 1024, on_answer, NULL);
```

- All modules share one HTTP task, and requests run one at a time (queue of
  8). Check what `hub_http_get()` returns: on an error (a full queue, no
  memory) the callback is never called, so clear your "fetching" state
  yourself. TLS sessions take internal RAM, which is the scarce resource on this
  board, so modules never open their own.
- HTTPS uses ESP-IDF's certificate bundle; plain HTTP works too.
- `max_len` caps the body (it goes in PSRAM). A longer body is cut short.
  Ask APIs for only the fields you need.
- Timeout is 15 s. Back off after errors (Weather waits 1, 2, 5, then
  15 minutes).
- `hub_url_encode()` encodes text typed by the user for a query string.

Parse JSON with cJSON (`REQUIRES json`). Its allocations already go to
PSRAM.

### Storage (`hub_store.h`)

- **Settings:** `hub_kv_get_str/set_str/get_i32/set_i32(module_id, key, ...)`
  store small values in NVS, in the namespace `m_<id>`. Keys are at most
  15 characters. They survive firmware updates.
- **Files:** `hub_file_path(id, "cache.json", path, sizeof(path))` gives
  `/storage/<id>/cache.json` and creates the folder. `hub_file_write()`
  writes a whole file safely (temporary file, then rename);
  `hub_file_read()` reads one into PSRAM (free it). The LittleFS partition
  (2 MB; 1.75 MB on boards with 8 MB of flash) is shared with learned IR
  remotes and the companion's icon cache (up to 640 KB): keep files small
  (a cache, not a history).

Cache the last good answer in a file so the screen fills at once after
start-up, and keep showing it (marked as old) when the network is down.

### Time (`hub_time.h`)

The clock is set over the network (SNTP) once Wi-Fi is up.
`hub_time_valid()` says whether it has been. There is no time-zone setting
yet: Weather reports its place's UTC offset with
`hub_time_set_utc_offset()`, and `hub_time_local()` /
`hub_time_to_local()` follow it (`hub_time_utc_offset()` reads it).

### The look (`ui_kit.h`)

Build screens from the UI kit so modules match the rest of the remote and
follow its theme ([themes.md](themes.md)):

- layout: `UI_W`, `UI_H`, `UI_TOPBAR_H`, `UI_GAP`, `UI_BODY_H` (532),
  `UI_WIDE_W` (1000), `HUB_VIEW_W/H` (figures at 1024x600), and `UI_S()`
  for every size of your own (below);
- colors by role: `COL_*` (surfaces, lines, three text levels,
  `COL_ACCENT` for fills with `COL_ON_ACCENT` on them, `COL_ACCENT_TEXT`
  for accent-colored text and icons, `COL_IR`, `COL_GOOD`, `COL_BAD`, and
  data colors such as `COL_SUN`, `COL_RAIN`). The table is in themes.md;
- corners through `ui_radius(r)` (square themes shrink them), and pressed
  shades through `ui_pressed(bg)`;
- building blocks: `ui_box`, `ui_panel`, `ui_label`, `ui_label_wrap` (full
  width, wraps), `ui_btn`, `ui_btn_icon`, `ui_btn_text`, `ui_flex`;
- `ui_status_*` for loading, error and empty states, and `ui_spinner` for
  a small "refreshing" indicator (see below);
- `ui_toast(icon, text)` for a short message under the top bar;
- `ui_kb_open_local(value, title, max_len, done, cancel)` for text entry on
  the remote's own keyboard (a city, a stop number). When it closes, show
  your screen again with `ui_module_open(id)`;
- fonts (`ui_fonts.h`), each size in one weight: `font_sora_11`, `_12`,
  `_13m`, `_15m`, `_16sb`, `_18b`, `_20sb`, `_22m`, `_24m`, `_26m`,
  `font_mono_11` and `_12` (IBM Plex Mono), and `font_sora_88n` for big
  numbers (digits, `-`, `−`, `°` and space only);
- icons (`ui_icons.h`): `ICON_*` strings for the `font_icons_16/20/24/28`
  fonts (Lucide). `font_weather_96` has the large weather icons.

Use colors and fonts only through these names; a hard-coded hex value
won't follow the theme.

**Screen sizes.** Boards have screens from 800x480 to 1024x600 (1280x800
was checked once too). Design at 1024x600 and wrap every pixel
size, gap and padding in `UI_S()` (`ui_size.h`, included by `ui_kit.h`):
`ui_panel(parent, UI_S(480), UI_S(160))`. It scales by the screen's
height at compile time and is exact at 600 px. Widths that should fill a
row are better divided from `HUB_VIEW_W` or `UI_WIDE_W` than scaled, as
the screens' proportions differ a little. Fonts already follow the
screen (the same names, drawn for each height), and so do the kit's own
sizes; percentages (`LV_PCT`), counts and times stay as they are. Radii
are the exception: `ui_radius(r)` and the `radius` argument of `ui_btn`,
`ui_btn_icon` and `ui_btn_text` take design pixels unwrapped and scale
them inside.
Screens show labels and data, not explanatory sentences or hints: if a
screen needs explaining, fix the screen.

### Loading, errors and empty screens

Every module shows these the same way, with `ui_status` from the UI kit.
Don't build your own spinner or "Loading…" label.

| Situation | Show |
|---|---|
| Nothing to show yet, data on its way | `UI_STATUS_LOADING`: a spinner, plus a title such as the place or item being loaded |
| It failed and there's nothing old to show | `UI_STATUS_ERROR`: an alert icon, a short title ("Couldn't load the forecast"), and a *Try again* action |
| Not set up yet | `UI_STATUS_EMPTY`: your icon and the action that sets it up ("Set location") |
| Data on screen, a refresh running | Keep the data and put a small `ui_spinner` next to its "Updated" time. Never blank the screen to refresh |
| Data on screen, the refresh failed | Keep the data and show its "Updated" time in the accent color (`COL_ACCENT_TEXT`) |

```c
/* Once, when building the screen (or a card, with compact = true): */
s_status = ui_status_create(parent, HUB_VIEW_W, HUB_VIEW_H, false);
ui_status_set_icon(s_status, UI_STATUS_EMPTY, &font_icons_28, ICON_MAP_PIN, COL_TEXT2);
ui_status_add_action(s_status, UI_STATUS_EMPTY, ICON_MAP_PIN, "Set location", true, on_setup, NULL);
ui_status_add_action(s_status, UI_STATUS_ERROR, ICON_REFRESH, "Try again", true, on_retry, NULL);

/* Whenever the state changes: */
ui_status_set(s_status, UI_STATUS_LOADING, "Lisbon", NULL);
ui_status_set(s_status, UI_STATUS_NONE, NULL, NULL); /* hidden: show your content */
```

Actions belong to one state and show only in it. Hide your content while a
status is showing. A spinner turned off with `ui_spinner_run(s, false)`
stops animating, so idle spinners cost nothing. Weather uses all of these:
the full screen, the Home card (compact) and the place search.

**Adding icons or glyphs:** fonts are generated, so edit
`tools/fonts/build_fonts.py` (the `ICONS` list takes a name, a Lucide icon
name and a comment) and run it with its venv; it rewrites `ui_icons.h` and
the font files for every screen height in `components/ui/fonts/`. Every glyph costs flash in every build, so add only what you
use. Don't add whole new font sizes for one module without asking in the
pull request.

## Rules

- **Memory.** Internal RAM is scarce; big buffers go in PSRAM
  (`heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`). LVGL already allocates from
  PSRAM. After a change, compare the `ctrl: heap after modules: internal
  N KB free` line in the log (it should not drop by more than a few KB).
- **Network.** Only `hub_http_get()`, no long-lived connections, no polling
  faster than the data changes, nothing while the screen is off unless
  needed.
- **Threads.** LVGL only on the LVGL task (see above). Never block the
  controller task.
- **Size.** Code and assets count against the app slot: 6 MB on boards
  with 16 MB of flash, 3 MB on those with 8 MB (the 7), where the firmware
  already takes about 2 MB. Prefer Lucide icons over images.
- **Privacy.** No accounts, tracking or analytics. Send only what the
  feature needs, to services listed in the module's README.
- **Licenses.** Data and assets must allow redistribution. Show an
  attribution line where the source's license requires one (Weather shows
  "Weather data by Open-Meteo.com"); that's the one exception to "labels
  only".
- **Secrets.** If a service needs a key, read it from NVS (entered on the
  device) or a Kconfig option that defaults to empty. Never commit one.

## Testing

**On a PC (host tests).** Keep URL building and parsing in a file that only
needs C and cJSON, with saved real answers as fixtures. Weather's
`test/run_test.py` compiles its test with a native gcc (on Windows,
MSYS2's MinGW, as in [sim/README.md](../firmware/remote/sim/README.md))
and runs it:

```powershell
python modules\weather\test\run_test.py --idf $env:USERPROFILE\esp\v5.4.4\esp-idf
```

Copy it for your module and change the file names.

**On the remote.** Developer builds (`CONFIG_HM_DEVELOPER`, the default)
answer screenshots and simulated touches over Wi-Fi, so you can test a
screen without walking to the remote. They need the update token in
`secrets.env` ([building](building.md#secrets)):

```powershell
python tools\devtools.py shot transit.png
python tools\devtools.py tap 275 27          # the Home tab on the 7B
python tools\devtools.py swipe 700 300 300 300
```

The first tap on a dark screen only wakes it. Watch the log with
`scripts\hm-netlog.ps1 -Remote hubmote.local` (or `hm-monitor.ps1`
over USB): `hub: module transit` at start-up confirms registration, and
`hub_http: GET ... -> 200, N bytes` shows each request.

Also check: no network, a slow or failing service, first start with no
settings or cache, a restart (settings and cache come back), the screen
going dark and waking, the module switched off in Settings > Modules (no
card, glance or events), and switched off in menuconfig (the build still
works).

## Pull request checklist

- [ ] Made with `tools/new_module.py`, or follows the same layout.
- [ ] Builds with the module on and off.
- [ ] `README.md` filled in: what it does, settings, data and privacy
      (which services it contacts and what it sends), credits and licenses.
- [ ] Parsing has a host test with a saved real answer.
- [ ] No LVGL calls off the LVGL task; no blocking on the controller task.
- [ ] Network work stops while the screen is off (or the README says why
      not).
- [ ] Only UI kit colors (by role), fonts and icons; radii through
      `ui_radius()`, pressed shades through `ui_pressed()`; no hint text on
      screens.
- [ ] Pixel sizes in `UI_S()`, so the module fits every board's screen
      (checked at 800x480: on such a board, or by adding the module to
      `sim/`).
- [ ] Screenshots in Midnight and Daylight (the light theme catches most
      color mistakes).
- [ ] Loading, error and empty states use `ui_status`; refreshes keep the
      data on screen.
- [ ] No keys, tokens or personal data in the code or fixtures.
- [ ] Data sources, icons or code it uses are in `NOTICE` with their
      licenses.
- [ ] Screenshots of the card and the screen (from `tools/devtools.py`).
- [ ] Free internal RAM checked in the `ctrl: heap` log line.
