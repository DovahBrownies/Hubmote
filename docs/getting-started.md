# Getting started

This guide takes you from a new board to a remote that controls your
Google TV. It's for anyone setting up Hubmote. You don't need to install
anything on your computer.

> [!NOTE]
> An AI coding agent can do the setup for you. Copy the prompt from the
> [README](../README.md#get-started).

## What you need

- **A board.** A Waveshare ESP32-S3-Touch-LCD-7B, or one of the other
  Waveshare boards in [the boards table](porting.md#the-boards). The
  others are supported, but nobody has tried them yet.
- **A USB-C cable that carries data**, for the first install. A cable that
  only charges won't show up on the computer. After that, updates come
  over Wi-Fi.
- **A Google TV Streamer** on your home network, over Ethernet or Wi-Fi.
  The remote finds it by the name it has in the Google Home app.
- **A 2.4 GHz Wi-Fi network.** The board has no 5 GHz radio, so it can't
  join a 5 GHz network.
- **A computer with Chrome or Edge**, for the installer. It can run
  Windows, macOS, Linux or ChromeOS.
- **Optional extras:**
  - an IR receiver and transmitter, for the TV and other devices (IR is
    infrared, the invisible light that ordinary remotes use);
  - a rechargeable LiPo battery and a power switch
    ([hardware](hardware.md));
  - the [3D-printed case](../hardware/enclosure), for the 7B.

## 1. Install the firmware

The firmware is the software that runs on the board. You install it from
a web page.

1. Open **https://dovahbrownies.github.io/Hubmote/** in Chrome or Edge.
2. Pick your board.
3. Plug the board into your computer by its **USB** port. If the board has
   two ports, use the one marked USB, not UART.
4. Click **Install** and pick the port called **USB JTAG/serial debug
   unit**. On Windows it may show as **USB Serial Device (COMn)**, where n
   is a number.

   If it can't connect, close the message and click **Install** again. If
   it still fails, unplug the board. Hold its **BOOT** button while you
   plug it back in, let go, then click **Install**.
5. Click **Install Hubmote for** your board.
6. Choose whether to erase the board:
   - For a new remote, tick **Erase device**.
   - To update a remote you already use, leave it unticked. Its pairing,
     Wi-Fi, learned remotes and settings stay.
7. Click **Next**, then **Install**. Keep the page open until it's done.
   This takes about two minutes.

## 2. Set it up on the remote

If the board sits upside down, as the 7B does in its case, turn the
screen first. Tap the gear at the top right, then **Screen** >
**Rotate 180°**, then **Done**.

1. Wait while the remote shows **Preparing secure pairing**. The remote is
   making its own key for pairing. This happens only on the first start
   and takes up to a minute.
2. On **Choose your Wi-Fi**, tap your network. Type its password on the
   remote's keyboard, then tap **Save**.
   - For a hidden network, tap **Other network**.
   - If it can't connect, the list comes back and says why.
3. On **Choose your Google TV**, tap your TV. The list shows the TVs
   found on your network.
4. The TV shows a 6-character code. Type it on the remote and tap
   **Pair**.

That's it. From now on, the remote reconnects by itself after a restart
or a power cut. This takes about 10 seconds.

Later, you can change this in **Settings > Remotes** (the gear):

- **Find TVs** switches to another TV.
- **Pair again** repeats the pairing with the same TV.

## 3. Volume

The remote sends volume to the Streamer. The Streamer passes it on to the
TV over HDMI-CEC, a feature that lets devices connected by HDMI control
each other. Turn it on in both places:

1. **On the Streamer:** open Settings > Remotes & Accessories > set up
   remote buttons > volume control, and choose the **TV, over HDMI-CEC**
   (not IR). If Display & Sound has an HDMI-CEC page, make sure HDMI-CEC
   is on there.
2. **On the TV:** turn HDMI-CEC on. Each brand has its own name for it.
   Samsung calls it Anynet+ (Settings > General > External Device
   Manager), LG calls it SIMPLINK, and Sony calls it BRAVIA Sync.

If your TV doesn't have HDMI-CEC, send volume to it over IR instead:

1. Wire up the IR transmitter ([hardware](hardware.md)).
2. Add the TV over IR. Tap the remote tab at the top left to open the
   switcher, then tap **Add a TV**.
3. Set **Settings > Remotes > TV volume** to **TV's IR**.

## 4. Next steps

- **The companion app** runs on the Streamer. It shows your installed apps
  with their icons and lets you type into any app. See
  [companion app](companion.md).
- **IR** lets the remote send the TV's own keys and control an HDMI
  switch or a soundbar. Wire a receiver and transmitter
  ([hardware](hardware.md)), then use **Add a TV** or **Learn a remote**
  ([using the remote](using.md#the-tv-and-other-devices-ir)).
- **Everything on the screen** is explained in
  [using the remote](using.md). For problems, see
  [troubleshooting](troubleshooting.md).

## Updates

The remote checks for a new release a minute after it joins Wi-Fi, and
then once a day. When a new release is out, a dot shows on the Settings
gear. To install it:

1. Open **Settings > About**.
2. Tap **Install**.

The remote downloads the update over a secure connection (HTTPS) and
checks it. Then it restarts into the new version, in about a minute. Your
pairing and settings stay.

The remote never installs an update by itself. If a new version can't
connect to Wi-Fi, the remote goes back to the old version the next time it
restarts.

You can also update from the installer page. Leave **Erase device**
unticked, and everything stays.
