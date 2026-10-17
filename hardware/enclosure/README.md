# Enclosure

This folder holds the remote's 3D-printed case, which measures
197 x 115 x 32 mm. It fits only the Waveshare ESP32-S3-Touch-LCD-7B,
because the other boards differ in size and connectors.

| File | What it is |
|---|---|
| `enclosure-7b.stl` | The model, in millimeters, for any slicer |
| `enclosure-7b.3mf` | The same model with its parts' colors |

![Hubmote in the case](../../docs/images/remote-photo.jpg)

- **Screws.** Four M3 × 14 mm screws close the case.
- **USB.** A short USB-C extension cable brings the board's USB port to
  the side of the case. Where its straight plug doesn't fit, use a flat
  one with a 90° plug.
- **IR.** The infrared (IR) transmitter and receiver look out of the same
  edge:

  ![Where the IR transmitter and receiver sit](../../docs/images/enclosure-ir.jpg)

[docs/hardware.md](../../docs/hardware.md) lists the parts used, with
links, and shows the wiring.

The board sits in the case upside down. On the remote, turn on
**Settings > Screen > Rotate 180°** so the picture is the right way up.

## Credit and license

This case is a remix of ["Waveshare ESP32-S3 7inch Capacitive Touch Display
Case"](https://www.printables.com/model/1306944-waveshare-esp32-s3-7inch-capacitive-touch-display)
by **3D_Project3R** on Printables, licensed under CC BY-SA 4.0. The Hubmote
authors changed it for the remote.

Like the original, these files are licensed under the Creative Commons
Attribution-ShareAlike 4.0 International License (CC BY-SA 4.0, the
`LICENSE` file here), not the project's Apache License. You may print,
share and adapt them, also commercially, if you:

- credit 3D_Project3R and the Hubmote authors with links;
- say what you changed;
- share your version under the same license.
