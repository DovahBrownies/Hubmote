# Power test firmware

This firmware is for developers who want to measure the board's power use.
It puts the Waveshare ESP32-S3-Touch-LCD-7B into one known power state at a
time, so an external meter can read what each state costs. It answers the
open questions about power:

- the real screen-on current at each backlight level;
- the stock standby floor (estimated at 6-22 mA, never measured);
- the cost of keeping Wi-Fi up for the TV;
- how long a cold wake takes.

## Build and flash

Always go through the wrapper scripts. They activate ESP-IDF v5.4.4 for
this project only and keep the build cache local, so other ESP32 projects
on this machine are untouched. Never call `idf.py` directly.

From the repository's root folder, in PowerShell:

```powershell
cd firmware\power-test
.\scripts\hm-idf.ps1 menuconfig          # optional: Power test -> Wi-Fi SSID / password
.\scripts\hm-idf.ps1 build
.\scripts\hm-idf.ps1 -p COMx flash
.\scripts\hm-monitor.ps1 COMx            # the native USB port; log viewer that does not reset the board
```

Replace `COMx` with the board's COM port.

- **Wi-Fi.** Leave the SSID empty to skip every Wi-Fi state. The SSID is
  stored in `sdkconfig`, which is gitignored.
- **Build logs.** Don't pipe the wrapper through `2>&1` or `*>`. The header
  of `hm-idf.ps1` shows how to capture a build log.

## Measuring

The board runs on battery through a CS8501 boost (cell to 5 V) and then an
LDO (5 V to 3.3 V). Only the current on the battery side gives the true
runtime.

### Battery side (best)

Put the meter in series with the battery lead, with USB unplugged.

- **Nordic PPK2 (or similar).** Use source-meter mode at 3.8 V in place of
  the battery, and log the full sequence. To line up the trace, the serial
  log prints `STEP n/19 <name>: start at <ms>`.
- **Multimeter.** Screen-on states draw roughly 300-600 mA, which can blow
  a typical 400 mA fused mA range.
  - Use the 10 A range for screen-on states.
  - For sleep states, wire a switch or jumper across the meter leads. Keep
    it closed, bypassing the meter, through boots and screen-on states.
    Open it only while you read a dark screen.
  - Every screen-off test ends with a reboot at full screen current.

### USB side (good enough for comparisons)

Put a USB-C power meter between a wall charger and the board, with the
battery disconnected. Otherwise you measure the charging current.

- The readings are at 5 V and skip the boost converter, so they understate
  standby. To estimate the battery current, multiply by about 1.6.
- Use a wall charger, not a PC, so USB enumeration and serial traffic don't
  add to the reading.

## Running the tests

- **Screen-on states.** Change one setting in the right-hand rows, wait
  about 10 s, and note the reading. The left card always says exactly what
  is being measured.
- **Screen-off tests.** Pick a duration, then tap a test. The screen goes
  dark, and the board reboots when the time is up. After the reboot, the
  left card shows what just finished, the reset reason and the boot time.
  **RESET aborts any test.**
- **Run full sequence.** All 19 states run back to back, 60 s each, which
  takes about 20 minutes in total. You can change the time per state in
  menuconfig. The board reboots after each screen-off state and the
  sequence carries on. Wi-Fi steps are skipped if no SSID is set. To abort,
  tap the screen during a screen-on step, or press RESET.
- **Wake latency.** This runs a 3 s deep sleep, then reports the time from
  boot to the first frame and the Wi-Fi connect time. It reconnects to the
  cached AP without a scan, using DHCP.

The states, and what each one isolates:

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
| Warm standby | The "TV link stays up" case: light sleep + Wi-Fi associated at DTIM3 + a 5 s keepalive |
| Deep sleep | Stock board floor, which decides whether the hardware mods are needed |
| Deep sleep, pins parked | Same, with the LCD lines driven low instead of floating |

## Results

Write down each reading here. After each hardware mod, measure the
deep-sleep and light-sleep rows again. The mods are removing the CAN
transceiver (U7) and removing the power LED (R106).

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
  5 s, not a real Android TV Remote v2 TLS session. The traffic is
  similarly tiny, but the real protocol runs the other way. The TV pings
  the remote, and the AP holds those frames until the next listen beacon.
- **Touch sleep is unconfirmed.** "+ Touch sleep" only sends the GT911
  sleep command. The measurement will show whether it reaches the
  datasheet's 70-120 uA.
- **Strapping pins are not parked.** The parked deep sleep leaves the four
  strapping pins (GPIO 0/3/45/46) to the board's resistors, so the next
  boot straps the same way.
- **The battery reading is approximate.** The voltage on screen comes from
  the IO expander's 10-bit ADC through a 3:1 divider. Treat it as accurate
  to about ±50 mV.
- **The boot time may not be absolute.** It is read from `esp_timer` at the
  first frame. Nobody has checked whether that clock includes the ROM and
  bootloader stages. So compare runs against each other, rather than
  reading it as an absolute figure.
