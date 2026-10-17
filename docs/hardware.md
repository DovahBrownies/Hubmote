# Hardware

This page is for anyone building the remote. It covers the parts, wiring
IR (infrared, the invisible light TV remotes use), adding a battery and a
power switch, and the case.

It describes the ESP32-S3-Touch-LCD-7B. The 7 is wired the same way for
USB and IR. But it can't read its battery, and it only switches its
backlight (the light behind the screen) on and off. What the other boards
can do is in [the boards table](porting.md#the-boards).

![Hubmote in its case](images/remote-photo.jpg)

On this page:

- [Parts](#parts)
- [USB connectors](#usb-connectors)
- [IR](#ir)
- [Battery](#battery)
- [Power switch](#power-switch)
- [The case](#the-case)

## Parts

The remote in the photo is built from these parts:

| Part | Used here |
|---|---|
| The board | Waveshare ESP32-S3-Touch-LCD-7B ([Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7B)) |
| IR receiver and transmitter | DKARDU 38 kHz kit ([Amazon](https://www.amazon.com/dp/B0C9TGJ99R)) |
| Battery | [Amazon](https://www.amazon.com/dp/B0GL1MH5YT); see [Battery](#battery) for what to look for |
| Power switch | [Amazon](https://www.amazon.com/dp/B0BZHZDBBY) |
| USB-C extension | [Amazon](https://www.amazon.com/dp/B0H6WVZ9L5) |
| Or a short, flat USB-C extension with a 90° plug | [Amazon](https://www.amazon.com/dp/B0G28KVFP3) |
| Four M3 × 14 mm screws | For example from this set ([Amazon](https://www.amazon.com/dp/B0FS1R3HVB)) |
| The case | 3D-printed, from [hardware/enclosure](../hardware/enclosure) |

[The case](#the-case) explains which USB-C extension to use.

## USB connectors

The 7B has two USB-C connectors, and so do the 7 and the 4.3. The 4.3B,
4.3C, 5 and 5B have only the native one, the ESP32-S3 chip's own USB.

| Connector | Shows up as | Use it for |
|---|---|---|
| **USB** (native) | "USB Serial Device" or "USB JTAG/serial debug unit", VID 303A | Installing **and** logs. Use this one. |
| **UART** (CH343) | "USB-Enhanced-SERIAL CH343" | Installing only, with SW1 on UART1. No logs. |

Installing puts the firmware (the remote's software) on the board.
[SW1](#set-sw1-to-uart2) is a slide switch on the board.

## IR

The IR transmitter lets the remote control your TV. The IR receiver lets
it learn keys from the TV's own remote.

### What you need

- **A receiver:** a 38 kHz IR receiver module ("1838").
- **A transmitter:** an IR LED module.
- **Leads:** HY2.0 leads, the small plug-in cables. The 7B's box has one
  3-pin and two 4-pin leads.

The [DKARDU kit](https://www.amazon.com/dp/B0C9TGJ99R) has both modules.

**Unplug the board before you start wiring.**

### Connect the receiver to J8

J8 is the 3-pin socket marked "GPIO". Connect the receiver like this:

| J8 pin | Signal | Receiver pin |
|---|---|---|
| 1 | 3V3 (3.3 volts) | VCC ("+", usually the middle pin) |
| 2 | GND (ground) | GND ("−") |
| 3 | GPIO6 | OUT / S / DAT |

> [!WARNING]
> Never power the receiver from 5 V. Its output goes straight into GPIO6,
> a pin on the board's chip.

### Connect the transmitter to H3

H3 is the 4-pin UART socket. Connect the transmitter like this:

| H3 pin | Signal | Transmitter pin |
|---|---|---|
| 1 | 3V3 | VCC ("+") |
| 2 | GND | GND ("−") |
| 3 | RXD = GPIO44 | S / signal |
| 4 | TXD = GPIO43 | Not used |

### Set SW1 to UART2

The transmitter only works with SW1 on **UART2**. SW1 is the small slide
switch just left of the BOOT button, marked `UART2 UART1`. It connects the
chip's UART (serial) pins to one of two places:

- **UART1:** the USB-to-serial chip. The transmitter gets nothing.
- **UART2:** the H3 socket, where the transmitter is.

Don't confuse SW1 with SW2, marked `120R`. SW2 only adds bus termination
for CAN and RS-485, and has nothing to do with IR. Leave it off.

### Where they sit in the case

In the case, the transmitter and receiver look out of the same edge, side
by side:

![Where the IR transmitter and receiver sit in the case](images/enclosure-ir.jpg)

### Check that it works

1. Add a TV, as described in [TVs over IR](ir-tvs.md#adding-a-tv).
2. Watch the transmitter's LED through your phone's front camera. Many
   rear cameras filter out infrared.
3. Tap a TV key on the remote. The LED should flicker on the phone's
   screen.

   If it doesn't, check that SW1 is on **UART2**.

The installer's firmware sends on H3 pin 3. Moving the wire to pin 4 needs
your own build, with `HM_IR_TX_GPIO` set to 43
([building](building.md#options)).

### More range

At 3.3 V, the kit's LED reaches about 1 m. To cover a room, drive an IR
LED from 5 V through a MOSFET, a transistor used as a switch. That gives
100–200 mA pulses and a range of 5–8 m. Wire it like this:

- GPIO44 (H3 pin 3) → a 100 Ω resistor → the gate of a logic-level
  N-channel MOSFET, such as an AO3400 or a 2N7002.
- A 100 kΩ resistor from the gate to GND.
- 5 V, from the board's J9 header → about 15 Ω → a 940 nm IR LED → the
  MOSFET's drain.
- The MOSFET's source to GND.
- A 220–470 µF capacitor across 5 V and GND, near the LED.

Two LEDs in series, with about 10 Ω, widen the beam.

### Other boards

- **The 4.3** takes the transmitter on its "Sensor AD" socket J6 (3V3,
  GND, GPIO6). It has no pin left for a receiver.
- **The 4.3B, 4.3C, 5 and 5B** have no free pin for IR.

## Battery

The figures here come from the 7B's schematic and its charger's
datasheet.

### Which battery

Use a **protected single-cell 3.7 V LiPo with a PH2.0 plug**. A LiPo
(lithium-polymer) battery is a flat, rechargeable pouch. PH2.0 is the
type of small plug the board takes.

> [!CAUTION]
> Check the plug's "+" and "−" against the markings on the board before
> you plug the battery in. Sellers wire PH2.0 plugs both ways round, and a
> battery plugged in the wrong way round can destroy the board's charger.
> To fix a reversed plug, lift each pin's latch with a needle and swap the
> wires.

How long a battery lasts depends on its capacity, in mAh
(milliamp-hours). The board charges at a fixed 580 mA.

| Battery | With the screen on | Charging time |
|---|---|---|
| About 3000 mAh | Roughly 5–7 h | About 6 h |
| 5000 mAh "115659" (about 11 × 56 × 59 mm, 90 g) | 8–11 h | About 10 h |

These times are estimates until they're measured (`firmware/power-test`).
Waveshare suggests at most 2000 mAh, without saying why.

- **Protection:** the board has no short-circuit protection. Its boost
  converter, which raises the battery's voltage for the board, cuts out at
  about 2.7 V. So the cell needs its own protection board.
- **Mounting:** put the battery behind the board on foam tape, with
  nothing hard against the pouch. If it has a third wire (for a
  thermistor), leave that wire unconnected.
- **First charge:** charge it outside any case, with the screen off, and
  feel the board now and then. The DONE LED lights when it's full.

### Faster charging

To charge faster, replace R64, the charger's PROG resistor:

| R64 | Charging current |
|---|---|
| 1.2 kΩ (stock) | 580 mA |
| 0.75 kΩ | About 900 mA |
| 0.68 kΩ | About 1 A |

More current means more heat, and the charger then throttles itself.

### The charging symbol

Boards that can read their battery, the 7B and the 4.3C, show when it's
charging. The board doesn't tell the ESP32 chip when USB power is
connected, so the remote works it out from the battery readings. On
battery, the boost converter makes them swing. On USB, they hold steady.

About 10 seconds after you plug in USB, with the screen on, the top bar
shows the green battery with a bolt.

### A faint buzz on battery

A faint buzz on battery is coil whine from the boost converter, the CS8501
and its inductor L2. It's harmless, and you won't hear it on USB.

A dab of 5-minute two-part epoxy on L2, the 4.4 × 4.2 mm inductor next to
U4, usually quiets it:

1. While the board buzzes, press L2 gently with a toothpick. If the sound
   doesn't change, the buzz comes from a capacitor near U4 instead.
2. Unplug the board.
3. Clean the spot with isopropyl alcohol.
4. Put the epoxy on L2 only.
5. Let the epoxy cure fully before you power the board up.

## Power switch

The board has none. It runs whenever a battery is connected. **Turn off
remote** in Settings puts it into deep sleep, a low-power state that still
draws a little.

For a real off switch:

1. Get a latching switch rated 1 A or more. A latching switch stays on or
   off until you press it again. Flat push buttons such as a KAN-28 or
   P-107 fit a case.
2. Solder the switch into the battery's **red (+)** wire, between the cell
   and the plug.
3. Cover the joints with heat-shrink.
4. Check the polarity at the plug before you connect it (see
   [Battery](#battery)).

When the switch is off, the battery doesn't charge. USB still runs the
remote, but the top bar shows `--%`. To charge the battery while you're
not using the remote, leave the switch on and tap **Turn off remote**.

## The case

[hardware/enclosure](../hardware/enclosure) has a 3D-printable case for
the 7B. It closes with four M3 × 14 mm screws.

A short USB-C extension brings the board's USB port to the side of the
case, for charging and for updates over USB. If there's no room in the
case for its plug, use a short, flat extension with a 90° plug instead.
Both are in the [parts list](#parts). Either one must carry data, not only
power.

The board sits in the case upside down, so turn on
**Settings > Screen > Rotate 180°**.
