# Boards, and porting Hubmote to another one

Hubmote is developed on the Waveshare ESP32-S3-Touch-LCD-7B and has board
support for Waveshare's other ESP32-S3 boards with a landscape RGB panel.
Everything specific to a board lives in one component,
`firmware/remote/components/board`; the UI lays itself out for the board's
screen. This guide lists the boards, says what a board needs, how to build
for one, what a port involves, and which display types need more work. If you have a
board you'd like to bring, open an issue first: it helps to know who is
working on what.

## The boards

| Board (id) | Screen | Expander | Backlight | Battery reading | IR | Flash | State |
|---|---|---|---|---|---|---|---|
| ESP32-S3-Touch-LCD-7B (`waveshare_s3_touch_lcd_7b`) | 7", 1024x600 | CH32V003 | dims | yes | LED on H3 (GPIO44), receiver on J8 (GPIO6) | 16 MB | tested |
| ESP32-S3-Touch-LCD-7 (`waveshare_s3_touch_lcd_7`) | 7", 800x480 | CH422G | on/off | no | as the 7B | 8 MB | untested |
| ESP32-S3-Touch-LCD-5B (`waveshare_s3_touch_lcd_5b`) | 5", 1024x600 | CH422G | on/off | no | none | 16 MB | untested |
| ESP32-S3-Touch-LCD-5 (`waveshare_s3_touch_lcd_5`) | 5", 800x480 | CH422G | on/off | no | none | 16 MB | untested |
| ESP32-S3-Touch-LCD-4.3 (`waveshare_s3_touch_lcd_4_3`) | 4.3", 800x480 | CH422G | on/off | no | LED on J6 (GPIO6), no receiver | 16 MB | untested |
| ESP32-S3-Touch-LCD-4.3B (`waveshare_s3_touch_lcd_4_3b`) | 4.3", 800x480 | CH422G | on/off | no | none | 16 MB | untested |
| ESP32-S3-Touch-LCD-4.3C (`waveshare_s3_touch_lcd_4_3c`) | 4.3", 800x480 | CH32V003 | dims | yes | none | 16 MB | untested |

"Untested" means the board's support is written from Waveshare's
schematics, ESP-IDF demos and board support package, and builds in CI,
but nobody has run Hubmote on one yet. If you do, please report how it
went in [issue #44](https://github.com/DovahBrownies/Hubmote/issues/44) (the test list below), whether it works or
not.

What the columns mean on the remote:

- **Backlight on/off:** the backlight driver's enable is the only control
  wired to the ESP32 (the dimming input is a test pad or isn't fitted), so
  Settings > Screen has no Brightness or Dim after; the screen goes
  straight from on to off.
- **No battery reading:** the cell's voltage doesn't reach the ESP32 or the
  expander, so the top bar shows no battery, and Settings > Screen has one
  set of timeouts (the battery ones) instead of On battery / Charging.
- **IR none:** every GPIO is taken (RS485, CAN, I2S, an RTC's interrupt),
  so the board is a Google TV remote only: the UI leaves out the IR TV,
  learned remotes and Inputs. With a free pin of your own (a modified
  board), set it in menuconfig > Hubmote > IR. **No receiver** (the 4.3):
  sending works (Add a TV with its built-in codes), learning doesn't.
- The 5 and 5B are one PCB with two panels; the 4.3B is also sold as the
  4.3B-BOX.

Not supported: the ESP32-S3-Touch-LCD-7C (its I2C bus and two panel pins
differ, a 16-bit expander, an octal-flash module), the square 480x480
boards (4, 4B, 2.8D) and the small SPI/QSPI and round ones (see Display
types below).

## Building for a board

The 7B is the default. For any other board, build from a new sdkconfig
with the board's id; it adds `components/board/boards/<id>.defaults` (the
board's choice, and its flash size and partition table where they
differ):

```powershell
.\scripts\hm-idf.ps1 -B build-<id> -D HM_BOARD=<id> -D SDKCONFIG=build-<id>/sdkconfig build
.\scripts\hm-idf.ps1 -B build-<id> -D HM_BOARD=<id> -D SDKCONFIG=build-<id>/sdkconfig -p COM5 flash
```

(On macOS and Linux, `idf.py` with the same options.) A separate build
folder and sdkconfig per board keep them apart; building in the default
folder works too after moving `sdkconfig` away. With `-D HM_BOARD`, the
build refuses an sdkconfig made for another board rather than mixing the
two; the id stays in that build folder's cache, another reason to give
each board its own `-B` folder. Menuconfig >
Hubmote > Board shows the board; don't switch boards there: an sdkconfig
keeps the old board's IR pins and flash size.

The web installer ([getting started](getting-started.md)) has every board: pick yours there.
Remotes update from the releases with their own board's image.

## What a board needs

- **An ESP32-S3**, for its Wi-Fi and its RGB LCD peripheral. Other chips
  aren't planned (the ESP32-P4 has no Wi-Fi of its own).
- **8 MB of octal PSRAM.** LVGL's two framebuffers, its heap, app icons
  and JSON all live there; the 7B leaves about 3.4 MB free (the PSRAM
  figure in the `ctrl: heap` log line).
- **8 or 16 MB of flash**: `partitions.csv` for 16 MB (two 6 MB app
  slots), `partitions_8mb.csv` for 8 MB (two 3 MB slots; the app is about
  2 MB).
- **A landscape RGB touchscreen** from 800x480 to 1280x800 (Screen sizes
  below).
- **For IR:** two free GPIOs, one to drive the IR LED stage and one for
  the receiver's output, on headers you can reach. One is enough for
  sending; none makes a Google TV remote only.
- Optional: a dimmable backlight, a battery voltage reading, and touch
  able to wake the chip from deep sleep.

## What a port involves

1. **The choice.** Add `config HM_BOARD_<VENDOR>_<MODEL>` to the
   `HM_BOARD` choice at the top of `main/Kconfig.projbuild`, and the
   board's `HM_BOARD_ID` (lower case, the same name), `HM_SCREEN_W` and
   `HM_SCREEN_H` there. Give `HM_IR_TX_GPIO` and `HM_IR_RX_GPIO` a
   default for it (`default 17 if HM_BOARD_...`, above the `default -1`
   line: Kconfig takes the first default whose condition holds), and say
   in their help which header they are on.
2. **Its defaults**, `components/board/boards/<id>.defaults`: the choice
   (`CONFIG_HM_BOARD_<...>=y`), plus whatever differs from
   `sdkconfig.defaults` (written for an ESP32-S3-WROOM-1-N16R8: 16 MB QIO
   flash, octal PSRAM at 80 MHz, the console on USB-Serial/JTAG, the RGB
   driver's ISR settings). The 7's sets 8 MB of flash and
   `partitions_8mb.csv`. CI builds every board that has this file.
3. **Its header**, `components/board/boards/<id>.h`, included from
   `components/board/include/board.h` under the new option. `board.h`
   lists what every header defines: `BOARD_NAME`, `BOARD_LCD_H_RES`,
   `BOARD_LCD_V_RES` (matching `HM_SCREEN_W/H`, which `main/display.c`
   checks), `BOARD_LCD_PCLK_HZ` (the fastest pixel clock that stays stable
   with Wi-Fi busy; on the 7B, 18 MHz slipped and 20 MHz broke touch),
   `BOARD_BACKLIGHT_DIMMABLE`, `BOARD_BATTERY_READING`, and
   `BOARD_UNTESTED` until someone has run it.
4. **Its code**, implementing every function in `board.h`:
   - `board_init()`: buses and helper chips (an IO expander), backlight
     off.
   - `board_touch_init(turned, out)`: the touch controller through
     `esp_lcd_touch`. With `turned`, report touches turned 180 degrees
     (the driver's mirror flags), to match a turned picture.
   - `board_lcd_init(pclk, cbs, ctx, out)`: the RGB panel with no
     framebuffer of its own (`no_fb`) and a bounce buffer, with `cbs`
     registered before the panel starts: `main/display.c` fills the bounce
     buffers from LVGL's framebuffers, which is how it turns the picture.
   - the backlight (`board_backlight_set/get/changed_us/refresh`);
     `refresh` may do nothing.
   - `board_deep_sleep()`: backlight and panel off, wake on touch (and a
     button if there is one), then `esp_deep_sleep_start()`.
   - `board_battery_mv()`: the cell's voltage, or 0 without a reading.

   Boards that share chips share code. A Waveshare board with the same
   pins needs only a header with its panel timings and expander pins
   (`BOARD_EXIO_*`): `waveshare_s3_rgb.c` has the I2C bus, GT911 touch,
   the panel and deep sleep, and `waveshare_ch422g.c` or
   `waveshare_ch32v003.c` the expander, backlight and battery. Another
   design gets files of its own. Add the sources to
   `components/board/CMakeLists.txt` under the board's option. Their
   requirements go in `REQUIRES` for every board (ESP-IDF reads
   requirements before the configuration), and managed drivers (a
   different touch chip) in `components/board/idf_component.yml`.
5. **Fonts**, if the screen's height is new: `tools/fonts/build_fonts.py
   --height <H>` makes `components/ui/fonts/h<H>/` (see Screen sizes).
6. **Battery.** `main/battery.c` tells USB from battery by the ripple of
   the reading, tuned to the 7B's boost converter (the 4.3C has the same
   one). A board with a charge-status pin needs its own rule there;
   without a reading (`BOARD_BATTERY_READING` 0) it isn't used.
7. **Check the screens** in the simulator at the board's size
   (`python sim/run_sim.py --size WxH`, `firmware/remote/sim/README.md`).
8. **Test on the board** (developer build), and note the results in the
   pull request:
   - picture: steady, no shifting or tearing, while Wi-Fi is busy (open
     Home and the Weather screen);
   - touch everywhere on screen, upright and with Settings > Screen >
     Rotate 180°;
   - backlight levels, dimming and screen off;
   - Turn off remote, and waking it by touch;
   - the battery: the top bar's level, and the log's `battery: ... mV`,
     `on battery` and `on USB` lines, on battery and on USB;
   - Wi-Fi setup, pairing with a Google TV, the protocol self-test in the
     log (`atv self-test passed`);
   - IR: Add a TV, sending, learning a button;
   - an update over Wi-Fi from the board's own build folder
     (`hm-ota.ps1 -Image build-<id>\hubmote.bin`, or `tools/hm_ota.py
     --image build-<id>/hubmote.bin`): the remote doesn't check which board
     an image is for;
   - screenshots with `tools/devtools.py`.

   Then drop `BOARD_UNTESTED` from its header.
9. **Docs.** The board in the table above and in
   [hardware.md](hardware.md), with its wiring for IR and anything that
   differs from the 7B; the IR pins' Kconfig help; and `NOTICE` for any new
   source (schematics, demo code, a driver).

## Screen sizes

The UI is designed at 1024x600 and scales to the board's screen when it
is built:

- Every pixel size in the UI goes through `UI_S()`
  (`components/ui/include/ui_size.h`): it scales by the screen's height
  against 600 px at compile time, and is exact at 1024x600. Widths that
  must fill the screen are divided from `UI_W` rather than scaled (the
  touchpad takes whatever width is left), as screens' proportions differ a
  little.
- The fonts are generated for each screen height under the same names, in
  `components/ui/fonts/h<H>/`; the build compiles the folder for
  `CONFIG_HM_SCREEN_H` and stops if there is none. The repo has `h600` and
  `h480`; `tools/fonts/build_fonts.py --height <H>` makes another (and,
  without `--height`, rebuilds every folder).
- The size comes from the board (`HM_SCREEN_W/H`). CI's simulator draws
  every screen at 800x480 and 1024x600; 1280x800 was checked once, with
  fonts made for it.
- Modules follow the same rule (`docs/modules.md`).

A compact layout for small, portrait or round screens would be a project
of its own: the rail, panel and touchpad don't fit there.

## Display types

`board_lcd_init()` and `main/display.c` assume an RGB parallel panel
driven through bounce buffers, as on Waveshare's boards. Boards with SPI
or QSPI panels (most small ones) need a second display path in
`main/display.c`: LVGL's partial rendering, and turning the picture with
the panel's own swap and mirror settings.

`firmware/power-test` (the current-measurement firmware) has its own copy of
the 7B's board code and stays 7B-only.

## Sources for Waveshare's boards

The board headers and the shared code files name their sources. In
short: Waveshare's wiki and schematics
(`files.waveshare.com/wiki/<board>/`), its demo repositories
(`github.com/waveshareteam/<board>`, Apache-2.0: the pins, panel timings
and expander writes in `waveshare_rgb_lcd_port.c`), its board support
package (`waveshareteam/Waveshare-ESP32-components`, for the 4.3C),
ESPHome's `waveshare_io_ch32v003` (the CH32V003's registers and PWM
limits), and Espressif's `esp_io_expander_ch422g` and ESP32_Display_Panel
for comparison. Where sources disagreed (the 7's flash size and its EXIO6,
the 5's vertical porches, the pixel clocks), the header says which was
followed and why.
