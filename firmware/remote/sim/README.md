# UI simulator

The remote's UI on a PC: the real `components/ui` and the Weather module's
screens, drawn by LVGL into memory at any screen size and saved as PNGs.
Use it to see a UI change at every board's screen size without the boards,
and to check a layout before flashing. The GitHub workflow runs it on
pushes to `main`, on pull requests and on tags, and keeps the screenshots
as the `screens` artifact.

From `firmware/remote`:

```
python sim/run_sim.py                              # every board's size, Midnight and Daylight
python sim/run_sim.py --size 800x480 --theme dusk  # one size, one theme
python sim/run_sim.py --size 1280x800              # another size, after making its fonts (below)
```

The screenshots go to `sim/out/<W>x<H>/<theme>/` (gitignored), one PNG per
screen: setup, the Streamer's home and app panels, the keyboard, the
switcher, Home and Weather, the IR screens, Add a TV and every Settings
section. `--out DIR` puts them elsewhere; `--keep-bmp` keeps the exact
pixels as BMPs too.

## What it needs

- CMake, Ninja and a C compiler: gcc or clang on Linux and macOS; on
  Windows, MSYS2's MinGW-w64 packages (`pacman -S
  mingw-w64-x86_64-{gcc,cmake,ninja}`, then put `C:\msys64\mingw64\bin`
  on PATH).
- LVGL: the firmware's own copy in `managed_components/lvgl__lvgl` (build
  the firmware once), or a clone of the version in `dependencies.lock`
  passed with `--lvgl DIR`.
- cJSON: ESP-IDF's (found through `IDF_PATH`), or a clone passed with
  `--cjson DIR` (`git clone --branch v1.7.18
  https://github.com/DaveGamble/cJSON`).
- Fonts for the screen's height in `components/ui/fonts/h<H>/`. The repo
  has the boards' heights (600 and 480); for another, run
  `tools/fonts/build_fonts.py --height <H>` first, with its venv and
  `npm install` (its docstring), adding `--no-check` off Windows.

Each size is built in `build-sim/<W>x<H>/`.

## How it works

- `sim_main.c` starts LVGL with a display of the chosen size, builds the
  UI as the remote does, feeds it sample data (a Streamer, its apps,
  learned remotes, a Wi-Fi scan...) through the same `ui.h` calls the
  controller uses, and saves a BMP after each screen. Time is simulated
  (33 ms per frame), so a build always draws the same pixels: the
  1024x600 screens match the remote's own screenshots.
- `sim_hub.c` stands in for the module platform (`components/hub`) and
  Weather's data side, with the forecast from
  `modules/weather/test/forecast_sydney.json`.
- `stubs/` replaces the few ESP-IDF headers the UI includes (logging,
  `esp_lvgl_port`'s lock, heap capabilities); `sim_compat.h` adds what a
  PC's C library may lack.
- `lv_kconfig.h` is LVGL's configuration, generated from the firmware's
  (`gen_lv_kconfig.py`, after changing LVGL options in
  `sdkconfig.defaults` and building the firmware once), with the PC's
  malloc instead of the firmware's PSRAM allocator.
- The size comes in as `UI_W`/`UI_H`, which the firmware takes from the
  board's `CONFIG_HM_SCREEN_W/H` (`components/ui/include/ui_size.h`).

A new screen or state worth checking goes into `sim_main.c` as another
`shot()`.
