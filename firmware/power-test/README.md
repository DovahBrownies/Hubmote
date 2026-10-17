# Power test firmware

Puts the Waveshare ESP32-S3-Touch-LCD-7B into one known power state at a time
so an external meter can read what each costs. It answers the open
questions about power: real screen-on current per backlight
level, the stock standby floor (estimated 6-22 mA, never measured), the cost
of keeping Wi-Fi up for the TV, and how long a cold wake takes.

## Build and flash

Always go through the wrapper scripts. They activate ESP-IDF v5.4.4 for this
project only and keep the build cache local, so other ESP32 projects on this
machine are untouched. Never call `idf.py` directly.

```powershell
cd firmware\power-test
.\scripts\hm-idf.ps1 menuconfig          # optional: Power test -> Wi-Fi SSID / password
.\scripts\hm-idf.ps1 build
.\scripts\hm-idf.ps1 -p COMx flash
.\scripts\hm-monitor.ps1 COMx            # the native USB port; log viewer that does not reset the board
```

Leave the SSID empty to skip every Wi-Fi state. It is stored in `sdkconfig`,
which is gitignored. Do not pipe the wrapper through `2>&1` or `*>`; see the
header of `hm-idf.ps1` for capturing a build log.

## Measuring

The board runs on battery through a CS8501 boost (cell to 5 V) and then an
LDO (5 V to 3.3 V). Only battery-side current gives true runtime.

**Best: battery side.** Put the meter in series with the battery lead, with
USB unplugged.

- **Nordic PPK2 (or similar).** Use source-meter mode at 3.8 V in place of the
  battery and log the full sequence. The serial log prints
  `STEP n/19 <name>: start at <ms>` for lining up the trace.
- **Multimeter.** Screen-on states draw roughly 300-600 mA, which can blow
  a typical 400 mA fused mA range. Use the 10 A range for screen-on states.
  For sleep states, wire a switch or jumper across the meter leads. Keep it
  closed (bypassing the meter) through boots and screen-on states, and open
  it only while reading a dark screen. Every screen-off test ends with a
  reboot at full screen current.

**Good enough for comparisons: USB side.** Put a USB-C power meter between a
wall charger and the board, with the battery disconnected (otherwise you
measure charging current).

- The readings are at 5 V and skip the boost converter, so they understate
  standby. To estimate battery current, multiply by about 1.6.
- Use a wall charger, not a PC, so USB enumeration and serial traffic don't
  add to the reading.

## Running the tests

- **Screen-on states.** Change one setting in the right-hand rows, wait about
  10 s, and note the reading. The left card always says exactly what is
  being measured.
- **Screen-off tests.** Pick a duration, then tap a test. The screen goes
  dark, and the board reboots when the time is up. After the reboot the left
  card shows what just finished, the reset reason, and the boot time.
  **RESET aborts any test.**
- **Run full sequence.** All 19 states run back to back, 60 s each
  (configurable in menuconfig), about 20 minutes in total, rebooting through
  the screen-off ones. Wi-Fi steps are skipped if no SSID is set. Tap the
  screen to abort during a screen-on step, or press RESET.
- **Wake latency.** Runs a 3 s deep sleep, then reports the time from boot to
  first frame and the Wi-Fi connect time. It reconnects to the cached AP
  without a scan, using DHCP.

| State | What it isolates |
|---|---|
| Backlight 100 / 75 / 50 / 25 / 10 / 1 % | Backlight cost curve; the default and auto-dim levels |
| CPU 160 / 80 MHz | CPU clock cost while the panel scans |
| Pixel clock 7 MHz | Cost of PSRAM scan-out traffic (14 MHz is the remote's value) |
| Full-screen animation | Worst-case render load |
| Wi-Fi no PS / modem sleep | Radio cost with the screen on |
| Backlight off, panel scanning | Panel logic plus RGB DMA without light |
| Panel off, CPU idle | Panel bias rails and RGB peripheral off; the chip awake but idle |
| + Touch sleep | GT911 sleep command |
| Light sleep, radio off | Chip asleep; mostly the board's own floor |
| Warm standby | Light sleep + Wi-Fi associated at DTIM3 + a 5 s keepalive, the "TV link stays up" case |
| Deep sleep | Stock board floor, which decides whether the hardware mods are needed |
| Deep sleep, pins parked | Same, with the LCD lines driven low instead of floating |

## Results

Write down each reading here, then re-measure the deep-sleep and light-sleep
rows after each hardware mod (removing the CAN transceiver U7, removing the
power LED / R106).

| # | State | Battery mA | USB mA @ 5 V | Notes |
|---|---|---|---|---|
| 1 | Backlight 100% | | | |
| 2 | Backlight 75% | | | |
| 3 | Backlight 50% | | | |
| 4 | Backlight 25% | | | |
| 5 | Backlight 10% | | | |
| 6 | Backlight 1% | | | |
| 7 | CPU 160 MHz | | | |
| 8 | CPU 80 MHz | | | |
| 9 | Pixel clock 7 MHz | | | |
| 10 | Full-screen animation | | | |
| 11 | Wi-Fi on, no power save | | | |
| 12 | Wi-Fi on, modem sleep | | | |
| 13 | Backlight off, panel scanning | | | |
| 14 | Panel off, CPU idle | | | |
| 15 | Panel off + touch asleep | | | |
| 16 | Light sleep, radio off | | | |
| 17 | Warm standby | | | |
| 18 | Deep sleep | | | |
| 19 | Deep sleep, LCD pins parked | | | |
| - | Wake latency: boot to first frame / Wi-Fi up | ms | ms | |

## Known limits

- **The keepalive is a stand-in.** It is an ICMP ping to the gateway every
  5 s, not a real Android TV Remote v2 TLS session. The traffic is similarly
  tiny, but the real protocol runs the other way: the TV pings the remote,
  and the AP holds those frames until the next listen beacon.
- **Touch sleep is unconfirmed.** "+ Touch sleep" sends the GT911 sleep
  command only; the measurement will show whether it reaches the datasheet's
  70-120 uA.
- **Strapping pins are not parked.** The parked deep sleep leaves the four
  strapping pins (GPIO 0/3/45/46) to the board's resistors, so the next boot
  straps the same way.
- **Battery reading is approximate.** The voltage on screen comes from the
  IO expander's 10-bit ADC through a 3:1 divider; treat it as about ±50 mV.
- **Boot time may not be absolute.** It is read from `esp_timer` at the first
  frame. Whether that clock includes the ROM and bootloader stages is not
  verified, so compare runs against each other rather than reading it as an
  absolute figure.
