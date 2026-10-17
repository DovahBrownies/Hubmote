# Getting started

From a new board to a remote that controls your Google TV, without
installing anything on your PC.

> [!NOTE]
> An AI coding agent can do the setup for you: copy the prompt in the
> [README](../README.md#get-started).

## What you need

- **A board:** a Waveshare ESP32-S3-Touch-LCD-7B, or one of the other
  Waveshare boards in [the boards table](porting.md#the-boards) (supported,
  but nobody has tried them yet).
- **A USB-C data cable** for the first install (a charge-only cable won't
  show up on the PC). Updates go over Wi-Fi after that.
- **A Google TV Streamer** on your home network (Ethernet or Wi-Fi). The
  remote finds it by the name it has in the Google Home app.
- **A 2.4 GHz Wi-Fi network:** the board has no 5 GHz radio.
- **A computer with Chrome or Edge** (Windows, macOS, Linux or ChromeOS) for
  the installer.
- Optional: an IR receiver and transmitter for the TV and other devices,
  a LiPo battery and a power switch ([hardware](hardware.md)), and the
  [3D-printed case](../hardware/enclosure) for the 7B.

## 1. Install the firmware

1. Open **https://dovahbrownies.github.io/Hubmote/** in Chrome or Edge and
   pick your board.
2. Plug the board in by its **USB** port (on boards with two, the one
   marked USB, not UART).
3. Click **Install** and pick the port called **USB JTAG/serial debug
   unit** (on Windows it may show as **USB Serial Device (COMn)**), then
   **Install Hubmote for** your board.
   - It can't connect: close the message and click Install again. Still
     failing: unplug the board, hold its **BOOT** button while plugging it
     back in, let go, then Install.
4. For a new remote, tick **Erase device**. To update one you already use,
   leave it unticked: its pairing, Wi-Fi, learned remotes and settings
   stay. Then **Next** and **Install**, and keep the page open until it's
   done, about two minutes.

## 2. Set it up on the remote

If the board sits upside down (the 7B does in its case), first tap the gear
(top right) > **Screen** > **Rotate 180°**, then **Done**.

1. **"Preparing secure pairing"** (the first start only, up to a minute):
   the remote makes its own key for pairing.
2. **"Choose your Wi-Fi":** tap your network and type its password on the
   remote's keyboard, then Save. A hidden network: **Other network**. If it
   can't connect, the list comes back saying why.
3. **"Choose your Google TV":** the TVs found on your network. Tap yours.
4. **Pairing:** the TV shows a 6-character code. Type it on the remote and
   tap **Pair**.

That's it. From now on the remote reconnects by itself after a restart or a
power cut, in about 10 seconds. Later, in Settings (the gear) > Remotes:
**Find TVs** to switch to another TV, **Pair again** to pair with the same
one anew.

## 3. Volume

The remote sends volume to the Streamer, which passes it on to the TV over
HDMI-CEC. Both need it on:

1. **On the Streamer:** Settings > Remotes & Accessories > set up remote
   buttons > volume control: the **TV, over HDMI-CEC** (not IR). If Display
   & Sound has an HDMI-CEC page, make sure it's on.
2. **On the TV:** HDMI-CEC on. Samsung calls it Anynet+ (Settings > General
   > External Device Manager), LG SIMPLINK, Sony BRAVIA Sync.

A TV without HDMI-CEC: add it over IR (Add a TV in the remote's switcher;
it needs the IR transmitter, [hardware](hardware.md)), then set
**Settings > Remotes > TV volume** to **TV's IR**.

## 4. More

- **The companion app** on the Streamer shows your installed apps with
  their icons and lets you type into any app: [companion app](companion.md).
- **IR** for the TV's own keys, an HDMI switch or a soundbar: wire a
  receiver and transmitter ([hardware](hardware.md)), then Add a TV or
  Learn a remote ([using the remote](using.md#the-tv-and-other-devices-ir)).
- **Everything on the screen** is explained in
  [using the remote](using.md); problems in
  [troubleshooting](troubleshooting.md).

## Updates

The remote checks for a new release a minute after it joins Wi-Fi, then
once a day. When one is out, a dot shows on the Settings gear: open
**Settings > About** and tap **Install**. It downloads over HTTPS, checks
the image, and restarts into it in about a minute, keeping your pairing and
settings. It never installs by itself, and a new version that can't reach
Wi-Fi goes back to the old one the next time it restarts.

The installer page is also an update: don't erase, and everything stays.
