# Writing a module

This guide is for developers who want to add a feature to the remote as a
module. It covers the API, threads, rules, testing and the pull request
checklist.

A module is an optional feature reached from the **Home** tab. It has a
card on the Home screen, a full screen of its own, and optionally a short
"glance" in the top bar (Weather shows an icon and the temperature). Each
module is one folder in `firmware/remote/modules/`. Nothing else in the
firmware has to change to add one.

These are native modules. They are compiled into the firmware, and
switched on or off in menuconfig. Card modules, which are data files
installed on the device without reflashing, are planned
([issue #26](https://github.com/DovahBrownies/Hubmote/issues/26)).

`modules/weather/` is the complete example. It has web requests, a cache
file, settings, a glance, a Home card, a full screen and a host test (see
its [README](../firmware/remote/modules/weather/README.md)).

On this page:

- [Quick start](#quick-start)
- [What's in a module folder](#whats-in-a-module-folder)
- [The module descriptor](#the-module-descriptor)
- [Threads](#threads)
- [Platform services](#platform-services)
- [The look](#the-look-ui_kith)
- [Rules](#rules)
- [Testing](#testing)
- [Pull request checklist](#pull-request-checklist)

## Quick start

In PowerShell, from `firmware/remote/`:

```powershell
python tools\new_module.py transit "Transit" --description "Next departures from my stop." --author "your-handle"
.\scripts\hm-idf.ps1 reconfigure
.\scripts\hm-idf.ps1 build
.\scripts\hm-ota.ps1
```

In the first command:

- `transit` is the module's id (see [Choosing an id](#choosing-an-id)).
- `"Transit"` is its name on the Home tab and in the switcher.
- `--description` is one or two sentences for menuconfig and the README.
- `--author` is your name or GitHub handle, for the README.

The last command installs the build over Wi-Fi. To flash over USB
instead, see [Build and flash](building.md#build-and-flash).

Run `reconfigure` once after you add or remove a module folder, so the
build and menuconfig find it. The new module is on by default
(**Hubmote > Modules** in menuconfig). It shows up as a card on the Home
tab and in the switcher. The template is a tap counter that keeps its
count across restarts.

### Choosing an id

The id (`transit` above) names the folder, the source file, the Kconfig
option (`CONFIG_HUB_MODULE_TRANSIT`) and the module's storage. Pick one
you won't need to change. An id:

- uses lowercase letters, digits and `_`, and starts with a letter;
- has at most 12 characters (`HUB_MODULE_ID_MAX`);
- must not be another component's name (`main`, `hub`, `ui`, `ir`,
  `board`, `json`, `log`...), because it is an ESP-IDF component name
  too.

Up to 24 modules can be registered (`HUB_MODULES_MAX`).

## What's in a module folder

```text
modules/transit/
  transit.c         the module: descriptor, callbacks, UI
  CMakeLists.txt    sources and requirements (left out when the option is off)
  Kconfig.module    the on/off option and any other options
  README.md         what it does, settings, data and privacy, credits
  test/             host tests for anything that parses data (recommended)
```

Once the code grows, split it the way Weather does:

- `*_data.c` for URLs and parsing, in plain C and cJSON, so it runs on a
  PC;
- `*.c` for state and refresh logic;
- `*_ui.c` for LVGL.

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

- **`REQUIRES` must be the same in both branches**, because ESP-IDF works
  out requirements before it reads `sdkconfig`. Only the sources depend on
  the option, so a disabled module adds nothing to the image.
- **`WHOLE_ARCHIVE` keeps the module linked in.** Nothing calls into it by
  name, because it registers itself (see
  [The module descriptor](#the-module-descriptor)). Without this flag,
  the linker drops it.
- **Add what you use to `REQUIRES`**, such as `json` for cJSON, or
  `esp_timer`.

### Kconfig.module

```kconfig
config HUB_MODULE_TRANSIT
    bool "Transit"
    default y
    help
        Next departures from my stop.
```

Every `modules/*/Kconfig.module` is pulled into the **Modules** menu. Put
any further build options for the module here. Name them
`HUB_MODULE_TRANSIT_*`, and give them `depends on HUB_MODULE_TRANSIT`.
Settings that people change on the device belong in NVS instead (see
[Storage](#storage-hub_storeh)).

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

The sources need no `#if CONFIG_HUB_MODULE_TRANSIT`, because
`CMakeLists.txt` leaves them out of the build when the option is off.
`HUB_MODULE()` registers the module before `app_main()` runs. Modules are
listed by name.

A module whose id is invalid or already taken is left out, with an error
in the log: `hub: module id "..." is invalid or taken`.

### Callbacks

Every callback is optional.

| Callback | Called | Task |
|---|---|---|
| `init()` | once at start-up, after storage is mounted, before Wi-Fi is up | controller |
| `event(ev)` | for each [event](#events) | controller |
| `create_card(parent)` | the first time the Home tab is shown | LVGL |
| `create_view(parent)` | the first time your screen is opened | LVGL |
| `show()` / `hide()` | your screen came into view / left | LVGL |
| `glance(out)` | when the top bar or switcher redraws | LVGL |

- `create_card()` returns your card.
- `create_view()` builds into `parent`, which is already `HUB_VIEW_W` x
  `HUB_VIEW_H` (1000 x 532 on a 1024x600 screen).
- `glance()` returns false when there's nothing to show. Its `text` holds
  15 characters.

### Cards and glances

- **Cards open your screen.** The Home tab makes the whole card open your
  screen when tapped, so the card needs no button of its own.
- **Cards are laid out in rows** that wrap, and stretch to fill their row.
  The width you give is the minimum. `UI_S(480)` fits two side by side.
- **The top bar shows one glance**, from the first switched-on module (by
  name) that has one. The switcher shows every module's glance.

### Events

| Event | When | Typical use |
|---|---|---|
| `HUB_EV_NETWORK_UP` | Wi-Fi connected | first fetch |
| `HUB_EV_NETWORK_DOWN` | Wi-Fi lost | show "offline", stop retrying |
| `HUB_EV_SCREEN_ON` | the screen woke from dark | refresh what is stale |
| `HUB_EV_SCREEN_OFF` | the screen went dark | stop polling |
| `HUB_EV_MINUTE` | every 60 s from start-up (not on the minute) | periodic refresh, clocks |

At start-up the screen is on, and no `HUB_EV_SCREEN_ON` is sent for it.

The remote is a battery device, so do network work only while the screen
is on. If a module needs otherwise, say so in its README. Nothing runs in
deep sleep. Waking from it reboots, so `init` runs again.

### When a module is switched off

People can switch modules off on the remote, in **Settings > Modules**.
An off module stays in the firmware, but it gets no `init` or events and
shows nowhere.

- **Switching it on later** runs `init` then (once), and sends
  `HUB_EV_NETWORK_UP` if the network is up.
- **Switching it off** just stops the events. So keep nothing running
  that events don't control.

## Threads

Two tasks call a module, and LVGL may only be touched from one of them.

- **Controller task:** `init` and `event` run here. Don't block it,
  because a slow call stalls the remote's own control logic. Use
  `hub_http_get()` for web requests, or start a task of your own for long
  work.
- **LVGL task:** `create_view`, `create_card`, `show`, `hide` and `glance`
  run here, with the LVGL lock already held. Keep them quick, because the
  screen doesn't redraw while they run.
- **Any other task** (an HTTP callback, your own task): reach the UI only
  through `hub_ui_async(fn, arg)`. It runs `fn(arg)` on the LVGL task with
  the lock held.

Call `hub_glance_changed()` when your glance changes. You can call it from
any task.

### Sharing data between tasks

Data shared between tasks needs care. Weather does it this way:

1. A mutex guards its state.
2. The HTTP callback parses into a scratch copy, then copies that into the
   state under the lock.
3. The screen takes a consistent copy (`weather_snapshot()`) before
   drawing, from `hub_ui_async()`.

Hold the lock only for the copy, never while drawing or waiting on the
network.

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

- **One task for everyone.** All modules share one HTTP task, and
  requests run one at a time, with a queue of 8.
- **Check what `hub_http_get()` returns.** On an error (a full queue, no
  memory), the callback is never called, so clear your "fetching" state
  yourself.
- **No TLS of your own.** TLS sessions take internal RAM, which is the
  scarce resource on this board, so modules never open their own.
- **HTTPS** uses ESP-IDF's certificate bundle. Plain HTTP works too.
- **`max_len`** caps the body, which goes in PSRAM. A longer body is cut
  short, so ask APIs for only the fields you need.
- **The timeout** is 15 s. Back off after errors (Weather waits 1, 2, 5,
  then 15 minutes).
- **`hub_url_encode()`** encodes text typed by the user for a query
  string.

Parse JSON with cJSON (`REQUIRES json`). Its allocations already go to
PSRAM.

### Storage (`hub_store.h`)

- **Settings:** `hub_kv_get_str/set_str/get_i32/set_i32(module_id, key, ...)`
  store small values in NVS, in the namespace `m_<id>`. Keys are at most
  15 characters. The values survive firmware updates.
- **Files:** `hub_file_path(id, "cache.json", path, sizeof(path))` gives
  `/storage/<id>/cache.json` and creates the folder. `hub_file_write()`
  writes a whole file safely: to a temporary file, then a rename.
  `hub_file_read()` reads one into PSRAM; free it when you're done.
- **Space:** the LittleFS partition is 2 MB, or 1.75 MB on boards with
  8 MB of flash. Learned IR remotes and the companion's icon cache (up to
  640 KB) share it, so keep files small. Store a cache, not a history.

Cache the last good answer in a file, so the screen fills at once after
start-up. Keep showing it, marked as old, when the network is down.

### Time (`hub_time.h`)

The clock is set over the network (SNTP) once Wi-Fi is up.
`hub_time_valid()` says whether it has been set yet.

`hub_time_local()` and `hub_time_to_local()` give local time, following
the time zone in **Settings > Modules**:

- **Automatic** (the default): the UTC offset a module reports for its
  place with `hub_time_set_utc_offset()`. Weather does this.
- **A zone from the list:** that zone's rules, daylight saving included.

`hub_time_utc_offset()` gives the offset in use now. A module that knows a
place should still report its offset, for remotes left on Automatic.

## The look (`ui_kit.h`)

Build screens from the UI kit, so modules match the rest of the remote and
follow its theme ([themes.md](themes.md)). `#include "ui_kit.h"` gives:

- **Layout:** `UI_W`, `UI_H`, `UI_TOPBAR_H`, `UI_GAP`, `UI_BODY_H` (532),
  `UI_WIDE_W` (1000) and `HUB_VIEW_W/H` (figures at 1024x600). Use
  `UI_S()` for every size of your own (see [Screen sizes](#screen-sizes)).
- **Colors by role:** `COL_*`. They cover surfaces, lines and three text
  levels; `COL_ACCENT` for fills, with `COL_ON_ACCENT` on them;
  `COL_ACCENT_TEXT` for accent-colored text and icons; `COL_IR`,
  `COL_GOOD` and `COL_BAD`; and data colors such as `COL_SUN` and
  `COL_RAIN`. The table is in [themes.md](themes.md#roles).
- **Corners and pressed shades:** corners through `ui_radius(r)` (square
  themes shrink them), and pressed shades through `ui_pressed(bg)`.
- **Building blocks:** `ui_box`, `ui_panel`, `ui_label`, `ui_label_wrap`
  (full width, wraps), `ui_btn`, `ui_btn_icon`, `ui_btn_text` and
  `ui_flex`.
- **States:** `ui_status_*` for loading, error and empty states, and
  `ui_spinner` for a small "refreshing" indicator (see
  [Loading, errors and empty screens](#loading-errors-and-empty-screens)).
- **Messages:** `ui_toast(icon, text)` shows a short message under the top
  bar.
- **Dialogs:** `ui_modal(title, message, buttons, count, done, user_data)`
  asks a question with up to three buttons, and `ui_modal_list(...)` lets
  the user pick from a list. `done` gets the choice, or -1 if the dialog
  was dismissed.
- **Text entry:** `ui_kb_open_local(value, title, max_len, done, cancel)`
  opens the remote's own keyboard, for text such as a city or a stop
  number. When it closes, show your screen again with
  `ui_module_open(id)`.
- **Fonts** (`ui_fonts.h`), each size in one weight: `font_sora_11`,
  `_12`, `_13m`, `_15m`, `_16sb`, `_18b`, `_20sb`, `_22m`, `_24m`,
  `_26m`; `font_mono_11` and `_12` (IBM Plex Mono); and `font_sora_88n`
  for big numbers (digits, `-`, `−`, `°` and space only).
- **Icons** (`ui_icons.h`): `ICON_*` strings for the
  `font_icons_16/20/24/28` fonts (Lucide). `font_weather_96` has the large
  weather icons.

Use colors and fonts only through these names. A hard-coded hex value
won't follow the theme.

### Screen sizes

Boards have screens from 800x480 to 1024x600, and 1280x800 was checked
once too. Design at 1024x600, and wrap every pixel size, gap and padding
in `UI_S()` (`ui_size.h`, included by `ui_kit.h`):

```c
ui_panel(parent, UI_S(480), UI_S(160));
```

- **`UI_S()`** scales by the screen's height at compile time, and is exact
  at 600 px. [Screen sizes](porting.md#screen-sizes) in porting.md says
  how the whole UI scales.
- **Widths that should fill a row** are better divided from `HUB_VIEW_W`
  or `UI_WIDE_W` than scaled, because the screens' proportions differ a
  little.
- **Fonts** already follow the screen (the same names, drawn for each
  height), and so do the kit's own sizes.
- **Percentages** (`LV_PCT`), counts and times stay as they are.
- **Radii are the exception.** `ui_radius(r)` and the `radius` argument of
  `ui_btn`, `ui_btn_icon` and `ui_btn_text` take design pixels unwrapped,
  and scale them inside.

### Loading, errors and empty screens

Every module shows these the same way, with `ui_status` from the UI kit.
Don't build your own spinner or "Loading…" label.

| Situation | Use | What it shows |
|---|---|---|
| Nothing to show yet, data on its way | `UI_STATUS_LOADING` | a spinner and a title, such as the place or item being loaded |
| It failed, and there's nothing old to show | `UI_STATUS_ERROR` | an alert icon, a short title ("Couldn't load the forecast") and a **Try again** action |
| Not set up yet | `UI_STATUS_EMPTY` | your icon and the action that sets it up (**Set location**) |
| Data on screen, a refresh running | `ui_spinner` | the data, with a small spinner next to its "Updated" time |
| Data on screen, the refresh failed | `COL_ACCENT_TEXT` | the data, with its "Updated" time in the accent color |

Never blank the screen to refresh.

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

- Actions belong to one state, and show only in it.
- Hide your content while a status is showing.
- A spinner turned off with `ui_spinner_run(s, false)` stops animating,
  so idle spinners cost nothing.

Weather uses all of these, in the full screen, the Home card (compact) and
the place search.

### Adding icons or glyphs

Fonts are generated. To add an icon or a glyph:

1. Edit `tools/fonts/build_fonts.py`. Its `ICONS` list takes a name, a
   Lucide icon name and a comment.
2. Run it with its venv. It rewrites `ui_icons.h` and the font files for
   every screen height in `components/ui/fonts/`.

Every glyph costs flash in every build, so add only what you use. Don't
add whole new font sizes for one module without asking in the pull
request.

## Rules

- **Memory.** Internal RAM is scarce, so big buffers go in PSRAM
  (`heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`). LVGL already allocates from
  PSRAM. After a change, compare the
  `ctrl: heap after modules: internal N KB free` line in the log. It
  should not drop by more than a few KB.
- **Network.** Use only `hub_http_get()`. No long-lived connections, no
  polling faster than the data changes, and nothing while the screen is
  off unless needed.
- **Threads.** LVGL only on the LVGL task (see [Threads](#threads)). Never
  block the controller task.
- **Size.** Code and assets count against the app slot. It is 6 MB on
  boards with 16 MB of flash, and 3 MB on those with 8 MB (the 7), where
  the firmware already takes about 2 MB. Prefer Lucide icons over images.
- **Labels only.** Screens show labels and data, not explanatory sentences
  or hints. If a screen needs explaining, fix the screen.
- **Privacy.** No accounts, tracking or analytics. Send only what the
  feature needs, to services listed in the module's README.
- **Licenses.** Data and assets must allow redistribution. Show an
  attribution line where the source's license requires one (Weather shows
  "Weather data by Open-Meteo.com"). That's the one exception to "labels
  only".
- **Secrets.** If a service needs a key, read it from NVS (entered on the
  device) or from a Kconfig option that defaults to empty. Never commit
  one.

## Testing

### On a PC (host tests)

Keep URL building and parsing in a file that only needs C and cJSON, with
saved real answers as fixtures. Weather's `test/run_test.py` compiles its
test with a native gcc and runs it. On Windows, that gcc is MSYS2's
MinGW, as in the [simulator's README](../firmware/remote/sim/README.md).

In PowerShell, from `firmware/remote/`:

```powershell
python modules\weather\test\run_test.py --idf $env:USERPROFILE\esp\v5.4.4\esp-idf
```

`--idf` is your ESP-IDF folder, where the test finds cJSON. Copy the
script for your module and change the file names.

### On the remote

Developer builds (`CONFIG_HM_DEVELOPER`, the default) answer screenshots
and simulated touches over Wi-Fi. So you can test a screen without walking
to the remote. They need the update token in `secrets.env` (see
[Secrets](building.md#secrets)).

In PowerShell, from `firmware/remote/`:

```powershell
python tools\devtools.py shot transit.png
python tools\devtools.py tap 275 27          # the Home tab on the 7B
python tools\devtools.py swipe 700 300 300 300
```

The first tap on a dark screen only wakes it.

Watch the log with `scripts\hm-netlog.ps1 -Remote hubmote.local`, or with
`hm-monitor.ps1` over USB:

- `hub: module transit` at start-up confirms registration.
- `hub_http: GET ... -> 200, N bytes` shows each request.

### What else to check

- No network.
- A slow or failing service.
- The first start, with no settings or cache.
- A restart: settings and cache come back.
- The screen going dark and waking.
- The module switched off in **Settings > Modules**: no card, glance or
  events.
- The module switched off in menuconfig: the build still works.

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
