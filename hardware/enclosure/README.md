# Enclosure

The remote's 3D-printed case (197 x 115 x 32 mm), for the Waveshare
ESP32-S3-Touch-LCD-7B only: the other boards differ in size and
connectors.

| File | What it is |
|---|---|
| `enclosure-7b.stl` | The model, in millimetres, for any slicer |
| `enclosure-7b.3mf` | The same model with its parts' colors |

![Hubmote in the case](../../docs/images/remote-photo.jpg)

- **Screws:** four M3 × 14 mm.
- **USB:** a short USB-C extension brings the board's USB port to the side
  of the case (or a flat one with a 90° plug, where the straight plug
  doesn't fit).
- **IR:** the transmitter and receiver look out of the same edge:

  ![Where the IR transmitter and receiver sit](../../docs/images/enclosure-ir.jpg)

The parts used, with links, and the wiring are in
[docs/hardware.md](../../docs/hardware.md). The board sits in the case
upside down, so turn on **Settings > Screen > Rotate 180°** on the remote.

## Credit and license

This case is a remix of ["Waveshare ESP32-S3 7inch Capacitive Touch Display
Case"](https://www.printables.com/model/1306944-waveshare-esp32-s3-7inch-capacitive-touch-display)
by **3D_Project3R** on Printables, licensed under CC BY-SA 4.0. The Hubmote
authors changed it for the remote.

Like the original, these files are licensed under the Creative Commons
Attribution-ShareAlike 4.0 International License (CC BY-SA 4.0, the
`LICENSE` file here), not the project's Apache License. You may print,
share and adapt them, also commercially, if you credit 3D_Project3R and
the Hubmote authors with links, say what you changed, and share your
version under the same license.
