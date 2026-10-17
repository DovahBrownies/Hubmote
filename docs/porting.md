# Boards, and porting Hubmote to another one

This page lists the boards Hubmote runs on and what each one can do. The
rest is for developers: how to build for a board, what a board needs, and
how to add one.

Hubmote is developed on the Waveshare ESP32-S3-Touch-LCD-7B. It also has
board support for Waveshare's other ESP32-S3 boards with a landscape RGB
panel. Everything specific to a board lives in one component,
`firmware/remote/components/board`, and the UI lays itself out for the
board's screen.

If you have a board you'd like to bring, open an issue first. It helps to
know who is working on what.

On this page:

- [The boards](#the-boards)
- [Building for a board](#building-for-a-board)
- [What a board needs](#what-a-board-needs)
- [Adding a board](#adding-a-board)
- [Screen sizes](#screen-sizes)
- [Display types](#display-types)
- [Sources for Waveshare's boards](#sources-for-waveshares-boards)

## The boards

| Board | Screen | Backlight | Battery reading | IR | Flash | State |
|---|---|---|---|---|---|---|
| ESP32-S3-Touch-LCD-7B | 7", 1024x600 | dims | yes | sends and learns | 16 MB | tested |
| ESP32-S3-Touch-LCD-7 | 7", 800x480 | on/off | no | sends and learns | 8 MB | untested |
| ESP32-S3-Touch-LCD-5B | 5", 1024x600 | on/off | no | none | 16 MB | untested |
| ESP32-S3-Touch-LCD-5 | 5", 800x480 | on/off | no | none | 16 MB | untested |
| ESP32-S3-Touch-LCD-4.3 | 4.3", 800x480 | on/off | no | sends only | 16 MB | untested |
| ESP32-S3-Touch-LCD-4.3B | 4.3", 800x480 | on/off | no | none | 16 MB | untested |
| ESP32-S3-Touch-LCD-4.3C | 4.3", 800x480 | dims | yes | none | 16 MB | untested |

What the columns mean on the remote:

- **Backlight: on/off.** Only the backlight driver's enable is wired to
  the ESP32. The dimming input is a test pad or isn't fitted. So
  **Settings > Screen** has no **Brightness** or **Dim after**, and the
  screen goes straight from on to off.
- **Battery reading: no.** The cell's voltage doesn't reach the ESP32 or
  the expander. The top bar shows no battery, and **Settings > Screen** has
  one set of timeouts (the battery ones) instead of **On battery** and
  **Charging**.
- **IR: none.** Every GPIO is taken (RS485, CAN, I2S, an RTC's
  interrupt), so the board is a Google TV remote only. The UI leaves out
  the IR TV, learned remotes and Inputs. If you modify a board to free a
  pin, set it in **menuconfig > Hubmote > IR**.
- **IR: sends only.** The 4.3 has a pin for the IR LED but no receiver.
  Sending works (**Add a TV** with its built-in codes), but learning
  doesn't.
- **State: untested.** The board's support is written from Waveshare's
  schematics, ESP-IDF demos and board support package, and it builds in
  CI. But nobody has run Hubmote on one yet. If you do, please report how
  it went in
  [issue #44](https://github.com/DovahBrownies/Hubmote/issues/44),
  whether it works or not. [The test list](#the-test-list) says what to
  try.

The 5 and 5B are one PCB with two panels. The 4.3B is also sold as the
4.3B-BOX.

### Ids, expanders and IR pins

For developers, here is each board's id, IO expander and IR pins:

| Board | Id | Expander | IR pins |
|---|---|---|---|
| 7B | `waveshare_s3_touch_lcd_7b` | CH32V003 | LED on H3 (GPIO44), receiver on J8 (GPIO6) |
| 7 | `waveshare_s3_touch_lcd_7` | CH422G | as the 7B |
| 5B | `waveshare_s3_touch_lcd_5b` | CH422G | none |
| 5 | `waveshare_s3_touch_lcd_5` | CH422G | none |
| 4.3 | `waveshare_s3_touch_lcd_4_3` | CH422G | LED on J6 (GPIO6), no receiver |
| 4.3B | `waveshare_s3_touch_lcd_4_3b` | CH422G | none |
| 4.3C | `waveshare_s3_touch_lcd_4_3c` | CH32V003 | none |

### Boards that aren't supported

- **The ESP32-S3-Touch-LCD-7C.** Its I2C bus and two panel pins differ,
  and it has a 16-bit expander and an octal-flash module.
- **The square 480x480 boards** (4, 4B, 2.8D).
- **The small SPI/QSPI boards and the round ones** (see
  [Display types](#display-types)).

## Building for a board

The 7B is the default. For any other board, build from a new sdkconfig
with the board's id. The build then adds
`components/board/boards/<id>.defaults`, which holds the board's choice,
and its flash size and partition table where they differ.

In PowerShell, from `firmware/remote/`:

```powershell
.\scripts\hm-idf.ps1 -B build-<id> -D HM_BOARD=<id> -D SDKCONFIG=build-<id>/sdkconfig build
.\scripts\hm-idf.ps1 -B build-<id> -D HM_BOARD=<id> -D SDKCONFIG=build-<id>/sdkconfig -p COM5 flash
```

Replace `<id>` with the board's id from
[the table above](#ids-expanders-and-ir-pins), and `COM5` with the
board's serial port. On macOS and Linux, run `idf.py` with the same
options.

- **Give each board its own build folder and sdkconfig**, to keep them
  apart. Building in the default folder works too, after you move
  `sdkconfig` away.
- **The build won't mix boards.** With `-D HM_BOARD`, it refuses an
  sdkconfig made for another board. The id also stays in that build
  folder's cache, which is another reason to give each board its own `-B`
  folder.
- **Don't switch boards in menuconfig.** **Hubmote > Board** there shows
  the board, but an sdkconfig keeps the old board's IR pins and flash
  size.

The web installer has every board, so pick yours there (see
[getting started](getting-started.md)). Remotes update from the releases
with their own board's image.

## What a board needs

- **An ESP32-S3**, for its Wi-Fi and its RGB LCD peripheral. Other chips
  aren't planned (the ESP32-P4 has no Wi-Fi of its own).
- **8 MB of octal PSRAM.** LVGL's two framebuffers, its heap, app icons
  and JSON all live there. The 7B leaves about 3.4 MB free (the PSRAM
  figure in the `ctrl: heap` log line).
- **8 or 16 MB of flash.** Boards with 16 MB use `partitions.csv`, with
  two 6 MB app slots. Boards with 8 MB use `partitions_8mb.csv`, with two
  3 MB slots. The app is about 2 MB.
- **A landscape RGB touchscreen** from 800x480 to 1280x800 (see
  [Screen sizes](#screen-sizes)).
- **Two free GPIOs for IR**, on headers you can reach. One drives the IR
  LED stage, and the other reads the receiver's output. One pin is enough
  for sending. With none, the board is a Google TV remote only.
- **Optional:** a dimmable backlight, a battery voltage reading, and touch
  that can wake the chip from deep sleep.

## Adding a board

1. **Add the board to the Kconfig choice.** In `main/Kconfig.projbuild`,
   add `config HM_BOARD_<VENDOR>_<MODEL>` to the `HM_BOARD` choice at the
   top. Add the board's `HM_BOARD_ID` there too (lower case, the same
   name), with its `HM_SCREEN_W` and `HM_SCREEN_H`.
2. **Give its IR pins a default.** In the same file, give `HM_IR_TX_GPIO`
   and `HM_IR_RX_GPIO` a default for the board, such as
   `default 17 if HM_BOARD_...`. Put it above the `default -1` line,
   because Kconfig takes the first default whose condition holds. In their
   help, say which header the pins are on.
3. **Write its defaults file**, `components/board/boards/<id>.defaults`.
   It sets the choice (`CONFIG_HM_BOARD_<...>=y`), plus whatever differs
   from `sdkconfig.defaults`. That file is written for an
   ESP32-S3-WROOM-1-N16R8: 16 MB QIO flash, octal PSRAM at 80 MHz, the
   console on USB-Serial/JTAG, and the RGB driver's ISR settings. The 7's
   defaults file, for example, sets 8 MB of flash and
   `partitions_8mb.csv`. CI builds every board that has a defaults file.
4. **Write its header**, `components/board/boards/<id>.h`, and include it
   from `components/board/include/board.h` under the new option.
   `board.h` lists what every header defines:
   - `BOARD_NAME`.
   - `BOARD_LCD_H_RES` and `BOARD_LCD_V_RES`, matching `HM_SCREEN_W/H`
     (`main/display.c` checks this).
   - `BOARD_LCD_PCLK_HZ`, the fastest pixel clock that stays stable while
     Wi-Fi is busy. On the 7B, 18 MHz slipped and 20 MHz broke touch.
   - `BOARD_BACKLIGHT_DIMMABLE` and `BOARD_BATTERY_READING`.
   - `BOARD_UNTESTED`, until someone has run Hubmote on the board.
5. **Write its code**, implementing every function in `board.h` (see
   [The board's functions](#the-boards-functions)). A Waveshare board with
   the same pins can reuse the shared code.
6. **Add the sources** to `components/board/CMakeLists.txt` under the
   board's option. List their requirements in `REQUIRES` for every board,
   because ESP-IDF reads requirements before the configuration. Add
   managed drivers (a different touch chip, for example) to
   `components/board/idf_component.yml`.
7. **Make its fonts**, if the screen's height is new. Run
   `tools/fonts/build_fonts.py --height <H>`, which makes
   `components/ui/fonts/h<H>/` (see [Screen sizes](#screen-sizes)).
8. **Check the battery rule.** `main/battery.c` tells USB from battery by
   the ripple of the reading. The rule is tuned to the 7B's boost
   converter (the 4.3C has the same one). A board with a charge-status pin
   needs its own rule there. Without a reading (`BOARD_BATTERY_READING`
   0), the rule isn't used.
9. **Check the screens** in the simulator at the board's size. From
   `firmware/remote/`:

   ```sh
   python sim/run_sim.py --size WxH
   ```

   `WxH` is the screen's size, such as `800x480`. The simulator's
   [README](../firmware/remote/sim/README.md) says what it needs.
10. **Test on the board** with a developer build, using
    [the test list](#the-test-list). Note the results in the pull request.
11. **Remove `BOARD_UNTESTED`** from the board's header once it is tested.
12. **Update the docs.** Add the board to [the tables above](#the-boards)
    and to [hardware.md](hardware.md), with its IR wiring and anything
    that differs from the 7B. Update the IR pins' Kconfig help. Add any new
    source (schematics, demo code, a driver) to `NOTICE`.

### The board's functions

Every board implements these functions from `board.h`:

- `board_init()` sets up the buses and helper chips (an IO expander),
  with the backlight off.
- `board_touch_init(turned, out)` starts the touch controller through
  `esp_lcd_touch`. With `turned`, it reports touches turned 180 degrees
  (with the driver's mirror flags), to match a turned picture.
- `board_lcd_init(pclk, cbs, ctx, out)` starts the RGB panel with no
  framebuffer of its own (`no_fb`) and a bounce buffer. It registers
  `cbs` before the panel starts. Through them, `main/display.c` fills the
  bounce buffers from LVGL's framebuffers, which is how it turns the
  picture.
- `board_backlight_set/get/changed_us/refresh` control the backlight.
  `refresh` may do nothing.
- `board_deep_sleep()` turns the backlight and panel off, and sets the
  chip to wake on touch (and on a button, if there is one). Then it calls
  `esp_deep_sleep_start()`.
- `board_battery_mv()` returns the cell's voltage, or 0 without a reading.

Boards that share chips share code. A Waveshare board with the same pins
needs only a header with its panel timings and expander pins
(`BOARD_EXIO_*`). The shared files are:

- `waveshare_s3_rgb.c`: the I2C bus, GT911 touch, the panel and deep
  sleep.
- `waveshare_ch422g.c` or `waveshare_ch32v003.c`: the expander, the
  backlight and the battery.

A board of another design gets files of its own.

### The test list

Test these on the board with a developer build:

- **Picture:** steady, with no shifting or tearing while Wi-Fi is busy.
  Open Home and the Weather screen to keep it busy.
- **Touch** everywhere on the screen, upright and with
  **Settings > Screen > Rotate 180°**.
- **Backlight:** its levels, dimming and screen off.
- **Sleep:** **Turn off remote**, then wake it by touch.
- **Battery:** the top bar's level, and the log's `battery: ... mV`,
  `on battery` and `on USB` lines, both on battery and on USB.
- **Google TV:** Wi-Fi setup, pairing with a Google TV, and the protocol
  self-test in the log (`atv self-test passed`).
- **IR:** **Add a TV**, sending, and learning a button.
- **An update over Wi-Fi** from the board's own build folder. The remote
  doesn't check which board an image is for. From `firmware/remote/`, in
  PowerShell:

  ```powershell
  .\scripts\hm-ota.ps1 -Image build-<id>\hubmote.bin
  ```

  Or, with Python on any system:

  ```sh
  python tools/hm_ota.py --image build-<id>/hubmote.bin
  ```

- **Screenshots** with `tools/devtools.py`.

## Screen sizes

The UI is designed at 1024x600. When it is built, it scales to the
board's screen:

- **Pixel sizes.** Every pixel size in the UI goes through `UI_S()`
  (`components/ui/include/ui_size.h`). It scales by the screen's height
  against 600 px at compile time, and is exact at 1024x600.
- **Widths that must fill the screen** are divided from `UI_W` rather
  than scaled, because screens' proportions differ a little. The touchpad
  takes whatever width is left.
- **Fonts** are generated for each screen height under the same names, in
  `components/ui/fonts/h<H>/`. The build compiles the folder for
  `CONFIG_HM_SCREEN_H`, and stops if there is none. The repo has `h600`
  and `h480`. `tools/fonts/build_fonts.py --height <H>` makes another.
  Without `--height`, it rebuilds every folder.
- **The size** comes from the board (`HM_SCREEN_W/H`). CI's simulator
  draws every screen at 800x480 and 1024x600. 1280x800 was checked once,
  with fonts made for it.
- **Modules** follow the same rule (see
  [modules.md](modules.md#screen-sizes)).

A compact layout for small, portrait or round screens would be a project
of its own, because the rail, panel and touchpad don't fit there.

## Display types

`board_lcd_init()` and `main/display.c` assume an RGB parallel panel
driven through bounce buffers, as on Waveshare's boards. Boards with SPI
or QSPI panels (most small ones) need a second display path in
`main/display.c`. It would use LVGL's partial rendering, and turn the
picture with the panel's own swap and mirror settings.

`firmware/power-test` (the current-measurement firmware) has its own copy
of the 7B's board code, and stays 7B-only.

## Sources for Waveshare's boards

The board headers and the shared code files name their sources. In short:

- **Waveshare's wiki and schematics**
  (`files.waveshare.com/wiki/<board>/`).
- **Its demo repositories** (`github.com/waveshareteam/<board>`,
  Apache-2.0), for the pins, panel timings and expander writes in
  `waveshare_rgb_lcd_port.c`.
- **Its board support package**
  (`waveshareteam/Waveshare-ESP32-components`), for the 4.3C.
- **ESPHome's `waveshare_io_ch32v003`**, for the CH32V003's registers and
  PWM limits.
- **Espressif's `esp_io_expander_ch422g` and ESP32_Display_Panel**, for
  comparison.

Where the sources disagreed, the header says which one was followed and
why. That was the case for the 7's flash size and its EXIO6, the 5's
vertical porches, and the pixel clocks.
