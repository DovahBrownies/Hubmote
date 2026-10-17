# Hubmote

Hubmote turns a Waveshare ESP32-S3 touchscreen board into a remote for the
**[Google TV Streamer](https://store.google.com/product/google_tv_streamer)**.
It talks to the Streamer over Wi-Fi, the same way Google's phone app does,
and its screen follows whatever app is on the TV. It also controls the TV
and other devices over infrared (IR), like an ordinary remote.

<p align="center">
  <img src="docs/images/remote-photo.jpg" alt="Hubmote in its 3D-printed case" width="49%">
  <img src="docs/images/remote.png" alt="Hubmote's Home screen: the apps on the TV, the rail and the D-pad" width="49%">
</p>

- **It follows the TV.** It shows the app grid on Google TV's Home, then
  each app's playback controls, and a keyboard when the TV asks for one.
- **It types into most apps** with the optional companion app: browsers,
  apps' own on-screen keyboards and Google TV's search. It also shows the
  title of what's playing, where the app shares it.
- **It controls the TV over IR.** It has built-in codes for 18 brands, or
  it learns them from the TV's own remote. It can learn other IR devices
  too, such as an HDMI switch or a soundbar.
- **Modules, themes and battery power.** A Home tab holds modules, small
  add-ons that start with Weather. You can pick one of five themes. The
  remote can run on a battery, with a deep sleep to save power.
- **No build tools needed.** You install it from a web page in Chrome or
  Edge. Later updates come from the remote itself.

## What you need

- **A Waveshare ESP32-S3 touch board.** Hubmote is made on the
  **ESP32-S3-Touch-LCD-7B** (7"). The 7, 5B, 5, 4.3, 4.3B and 4.3C are
  supported but not tried yet
  ([what each can do](docs/porting.md#the-boards)).
- **A [Google TV Streamer](https://store.google.com/product/google_tv_streamer).**
- **A 2.4 GHz Wi-Fi network.** The board can't join a 5 GHz one.
- **Optional extras:** an IR receiver and transmitter, a rechargeable LiPo
  battery, and the [3D-printed case](hardware/enclosure).

## Get started

1. **Install the firmware**, the software that runs on the board. Open
   **https://dovahbrownies.github.io/Hubmote/** in Chrome or Edge and
   connect the board with a USB-C cable.
2. **Set it up on the remote.** Pick your Wi-Fi, pick your Google TV, and
   type the code the TV shows.
3. **Add the extras you want.** Install the
   [companion app](docs/companion.md) on the Streamer, and
   [wire up IR](docs/hardware.md). Both are optional.

[Getting started](docs/getting-started.md) has every step in detail.

> [!NOTE]
> **Or ask your AI agent to set it up for you.** Paste this into Claude
> Code, Codex, Cursor or another coding agent on the computer the board is
> plugged into:
>
> ```text
> Set up Hubmote for me: https://github.com/DovahBrownies/Hubmote
>
> Clone the repository and read its CLAUDE.md and the guides in docs/
> first. Then:
> 1. Ask me which board I have and tell me what it can and can't do
>    (docs/porting.md). Find the USB port it's on (docs/hardware.md says
>    how it shows up) or ask me, and don't flash anything until I've
>    confirmed the port.
> 2. Install what this computer needs (docs/building.md), build the
>    firmware for my board and flash it over USB. If I'd rather not
>    install build tools, guide me through the web installer instead.
> 3. Walk me through the first start on the remote (Wi-Fi, picking my
>    Google TV, pairing), the HDMI-CEC settings for volume, and Rotate
>    180° if the board is in the 3D-printed case. I'll type my Wi-Fi
>    password on the remote myself.
> 4. Offer to install the companion app on my Google TV Streamer
>    (docs/companion.md): from this computer over Wireless debugging, or
>    on the TV with the Downloader app. Explain the difference, ask me
>    which I prefer, then pair it with the remote.
> 5. If I have IR parts, a battery, a power switch or the case, help me
>    with them (docs/hardware.md), and add my TV on the remote.
> 6. At the end, check with me that it all works: a key, volume, opening
>    an app, typing into a search field, and an IR key if wired. Fix what
>    doesn't (docs/troubleshooting.md), and show me how updates work.
>
> Before each step, tell me what it does and roughly how long it takes.
> Ask before changing anything on my TV or Streamer. Never print my Wi-Fi
> password or the remote's update token. If a step needs the token, I'll
> put it in secrets.env myself.
> ```

## Guides

For everyone:

- **[Getting started](docs/getting-started.md):** installing, the first
  start, pairing, volume and updates.
- **[Using the remote](docs/using.md):** the screens and the settings.
- **[Companion app](docs/companion.md):** installing and pairing it.
- **[Hardware](docs/hardware.md):** IR wiring, the battery, a power switch
  and the case.
- **[Troubleshooting](docs/troubleshooting.md):** logs and common problems.

For developers:

- **[Building from source](docs/building.md):** building, flashing and
  updating your own firmware.
- **[Boards](docs/porting.md):** the supported boards, and adding one.
- **Extending it:** [modules](docs/modules.md), [themes](docs/themes.md)
  and [TVs over IR](docs/ir-tvs.md).
- **The project:** [security](docs/security.md) (what it protects),
  [releasing](docs/releasing.md) (how releases go out) and the
  [issues](https://github.com/DovahBrownies/Hubmote/issues) (what's next).
- **The code:** the [firmware](firmware/remote/README.md) and the
  [companion](companion/README.md).

## Status

Hubmote is tested on the 7B with a Google TV Streamer. These aren't
checked yet:

- the other boards ([#44](https://github.com/DovahBrownies/Hubmote/issues/44));
- the built-in TV code sets, on real TVs
  ([#12](https://github.com/DovahBrownies/Hubmote/issues/12));
- battery life ([#18](https://github.com/DovahBrownies/Hubmote/issues/18)).

Netflix, Prime Video and Plex don't share what's playing. Prime Video
can't be typed into, so search from Google TV's Home instead. Open work is
in the [issues](https://github.com/DovahBrownies/Hubmote/issues).

## License

The code is licensed under the [Apache License 2.0](LICENSE). The case's
3D models, a remix of 3D_Project3R's case, are licensed under
[CC BY-SA 4.0](hardware/enclosure/LICENSE). [NOTICE](NOTICE) lists the
work Hubmote builds on and its licenses.

[Contributing](CONTRIBUTING.md) · [Code of conduct](CODE_OF_CONDUCT.md) ·
[Reporting a security problem](SECURITY.md)
