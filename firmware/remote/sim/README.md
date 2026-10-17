# UI simulator

The UI simulator draws the remote's screens on a PC, for developers who
change the UI. It builds the real `components/ui` and the Weather module's
screens, lets LVGL draw them into memory at any screen size, and saves them
as PNGs.

Use it to see a UI change at every board's screen size without the boards,
and to check a layout before you flash it. The GitHub workflow runs it on
pushes to `main`, on pull requests and on tags, and keeps the screenshots
as the `screens` artifact.

## Running it

Run these from `firmware/remote`:

```sh
python sim/run_sim.py                              # every board's size, Midnight and Daylight
python sim/run_sim.py --size 800x480 --theme dusk  # one size, one theme
python sim/run_sim.py --size 1280x800              # another size, after making its fonts (below)
```

The screenshots go to `sim/out/<W>x<H>/<theme>/`, which is gitignored. There
is one PNG per screen: setup, the Streamer's home and app panels, the
keyboard, the switcher, Home and Weather, the IR screens, Add a TV and
every Settings section.

- `--out DIR` puts them somewhere else.
- `--keep-bmp` also keeps the exact pixels as BMPs.

Each size is built in `build-sim/<W>x<H>/`.

## What it needs

- **CMake, Ninja and a C compiler.** On Linux and macOS, use gcc or clang.
  On Windows, install MSYS2's MinGW-w64 packages, then put
  `C:\msys64\mingw64\bin` on PATH:

  ```sh
  pacman -S mingw-w64-x86_64-{gcc,cmake,ninja}
  ```

- **LVGL.** The simulator uses the firmware's own copy in
  `managed_components/lvgl__lvgl`, so build the firmware once first. Or
  pass a clone of the version in `dependencies.lock` with `--lvgl DIR`.
- **cJSON.** The simulator finds ESP-IDF's copy through `IDF_PATH`. Or
  pass a clone with `--cjson DIR`. To clone it:

  ```sh
  git clone --branch v1.7.18 https://github.com/DaveGamble/cJSON
  ```

- **Fonts for the screen's height** in `components/ui/fonts/h<H>/`. The repo
  has the boards' heights (600 and 480). For another height, run
  `tools/fonts/build_fonts.py --height <H>` first. It needs its venv and
  `npm install`, as its docstring explains. Off Windows, add `--no-check`.

## How it works

- `sim_main.c` starts LVGL with a display of the chosen size and builds the
  UI as the remote does. It feeds the UI sample data (a Streamer, its apps,
  learned remotes, a Wi-Fi scan...) through the same `ui.h` calls the
  controller uses, and saves a BMP after each screen.
- Time is simulated at 33 ms per frame, so a build always draws the same
  pixels. The 1024x600 screens match the remote's own screenshots.
- `sim_hub.c` stands in for the module platform (`components/hub`) and
  Weather's data side. The forecast comes from
  `modules/weather/test/forecast_sydney.json`.
- `stubs/` replaces the few ESP-IDF headers the UI includes: logging,
  `esp_lvgl_port`'s lock and heap capabilities. `sim_compat.h` adds what a
  PC's C library may lack.
- `lv_kconfig.h` is LVGL's configuration, generated from the firmware's by
  `gen_lv_kconfig.py`. It uses the PC's malloc instead of the firmware's
  PSRAM allocator. After you change LVGL options in `sdkconfig.defaults`,
  build the firmware once, then run `gen_lv_kconfig.py` again.
- The screen size comes in as `UI_W` and `UI_H`. The firmware takes these
  from the board's `CONFIG_HM_SCREEN_W/H`
  (`components/ui/include/ui_size.h`).

To check a new screen or state, add it to `sim_main.c` as another `shot()`.
