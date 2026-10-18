# Troubleshooting

This page helps when something doesn't work. Find what you see in the
list below. The last part, about [logs](#logs) and
[screenshots](#screenshots), is for advanced users. Problems with the
companion app are in [its own guide](companion.md#problems).

On this page:

- [Installing](#installing)
- [Wi-Fi and pairing](#wi-fi-and-pairing)
- [The Google TV and volume](#the-google-tv-and-volume)
- [Typing](#typing)
- [IR](#ir)
- [Screen, battery and power](#screen-battery-and-power)
- [Updates](#updates)
- [Logs](#logs), [useful log lines](#useful-log-lines) and
  [screenshots](#screenshots)

## Installing

### The installer can't connect to the board

1. Close the message and click **Install** again.
2. If that doesn't work, unplug the board. Hold its **BOOT** button while
   you plug it back in, let go, then click **Install** again.

If no port shows up at all, your cable probably only charges. Use a USB-C
cable that carries data.

If the port's name has **CH343** in it, the cable is in the board's UART
connector. Plug it into the connector marked **USB** instead
([USB connectors](hardware.md#usb-connectors)).

## Wi-Fi and pairing

### "Couldn't find" or "didn't accept the password" on Wi-Fi

1. Check that you picked a 2.4 GHz network. Home Wi-Fi often has two
   bands, 2.4 GHz and 5 GHz, and the remote only sees 2.4 GHz networks.
2. Retype the password. The keyboard shows it as you type.

### "No Google TV found"

1. Check that the Streamer is on.
2. Check that it's on the same network as the remote, and on the same
   Wi-Fi band.
3. Tap **Search again**.

### "Can't reach your TV"

1. Check that the TV is on, and on the same network as the remote.
2. Tap **Try again**. The remote searches the network again.

### "Not your TV"

Another device answers at the TV's old address. Tap **Try again** to see
the list of TVs, then pick yours.

### "Pair with your TV again"

The TV forgot this remote, or it was reset. Tap **Try again**, then type
the code the TV shows.

### "Pairing didn't finish"

Tap **Try again**. The TV shows a new code.

## The Google TV and volume

### Volume does nothing

The Streamer isn't passing volume on to the TV. Volume travels from the
Streamer to the TV over HDMI-CEC, a feature that lets devices control each
other through the HDMI cable. To fix it:

1. On the Streamer, set the volume control to the TV over HDMI-CEC.
2. On the TV, turn HDMI-CEC on.

[Getting started](getting-started.md#3-volume) shows where to find both
settings. In the [log](#logs), this problem shows as
`ctrl: tv volume 0/0`.

### Google TV's Home shows only 5 apps

The companion app isn't installed, paired or reachable. See the
[companion app's problems](companion.md#problems).

## Typing

### The keyboard doesn't open in an app

The keyboard opens by itself only when the TV asks for one. In other apps,
tap **Keyboard** on the rail.

### Typing in an app does nothing

- **Prime Video** can't be typed into. Search from Google TV's Home
  instead.
- **Netflix** takes typing only while its own keyboard has focus.
- **Other apps, and search on Google TV's Home,** need the companion app.
  Check that it's paired.

[The keyboard](using.md#the-keyboard) explains how each kind of app takes
text.

## IR

### The IR transmitter stays dark

1. Check that the switch SW1 is on **UART2**.
   [Hardware](hardware.md#ir) shows where it is.
2. Point a phone camera at the LED while you tap a TV key on the remote.
   The LED should flicker on the phone's screen.
3. If it's still dark, you can move the LED's signal wire to pin 4 of H3,
   the socket the transmitter plugs into. That needs your own build of the
   firmware, one that sends on that pin. For developers: set
   `HM_IR_TX_GPIO` to 43 ([building](building.md#options)).

## Screen, battery and power

### The picture is upside down

Turn **Rotate 180°** on or off in **Settings > Screen**. Touch turns with
the picture.

### The board doesn't start on battery

1. Check that the battery plug's "+" and "−" match the board's markings
   ([hardware](hardware.md#battery)).
2. Plug in USB with the battery connected. The board's charging LED
   should light.
   This also wakes a battery whose built-in protection has switched it
   off.

### The top bar shows `--%`

No battery is connected, or its power switch is off.

## Updates

### About says "update didn't work"

Tap **Check** in **Settings > About** to try again. It's safe: nothing on
the remote changes until an install completes.

To find out why it failed, look for the `update:` lines in the
[log](#logs). The usual causes are no internet, a download cut short, or a
checksum mismatch (the download didn't match the release). A release
without a valid signature isn't offered at all: the remote only installs
firmware the project signed.

## Logs

This part is for advanced users. The remote's log says what it's doing
and why something failed.

> [!IMPORTANT]
> Before you share a log, remove your network's name, addresses and
> tokens. The remote logs only the Wi-Fi driver's warnings, which rarely
> name the network, but check lines that start with `wifi:` too.

### Over USB

This works with any build.

1. Plug the board's **USB** port into your computer.
2. Open the board's serial port (on Windows, a COM port) at 115200 baud
   with any serial monitor.

Or, on Windows, use the repository's scripts. Run them in PowerShell from
the `firmware/remote` folder:

```powershell
# Show the log live:
.\scripts\hm-monitor.ps1 COM5

# Restart the remote and save its first 60 seconds to boot.log:
.\scripts\hm-capture.ps1 -Port COM5 -Seconds 60 -Out boot.log -Reset
```

Replace `COM5` with your board's port.

### Over Wi-Fi

This works with developer builds only ([building](building.md)). The
remote serves its log on TCP port 2323. A new connection gets the last
32 KB of the log first.

On Windows, run this in PowerShell from the `firmware/remote` folder:

```powershell
.\scripts\hm-netlog.ps1 -Remote hubmote.local
```

On any OS with Python, run this from the same folder:

```sh
python tools/hm_netlog.py --remote hubmote.local
```

`hubmote.local` is the remote's name on your network. If a second remote
is on the same network, it answers to `hubmote-2.local`. **Settings >
About** shows each remote's name and address.

The script reconnects by itself after the remote restarts. The remote
sends its log to one client at a time, and the newest connection wins.

### The companion's log

The companion app's log needs ADB, Android's debugging tool for PCs.
[The companion app guide](companion.md#from-a-pc) shows how to set it up.
Then run:

```sh
adb logcat -s HubmoteCompanion
```

### Useful log lines

| Line | Meaning |
|---|---|
| `net: up: 192.168.1.50` | Wi-Fi connected |
| `atv: TLS up with <ip>:6466`, `state CONNECTING -> CONNECTED` | Connected to the Streamer |
| `atv: TLS handshake ... -0x0050 ... -> certificate not accepted` | The TV doesn't accept this remote |
| `ctrl: "<name>" moved from A to B` | The TV changed address and was found again |
| `ctrl: tv volume N/M` | The Streamer's volume. `0/0` means it isn't controlling the TV's |
| `ctrl: app: <package>` | The app on the TV |
| `ctrl: key <code> <tap\|down\|up>: ESP_OK` | A key went to the Streamer |
| `companion: online`, `companion: N apps`, `companion: field: text (N)` | The companion app |
| `ctrl: text (N chars) via companion: ESP_OK` | Typing through the companion (lengths only, never the text) |
| `ir_ctl: tv <key> (...): ESP_OK` | An IR code was sent |
| `update: release 0.1.1, running 0.1.0: update available` | Updates from the releases |
| `update: release X: no valid signature for <board>, not offered` | A release that isn't the project's, or a broken site: not installed |
| `ota: new firmware confirmed (ota_1)` | A new version reached Wi-Fi, so it stays |
| `battery: on USB (...)`, `battery: on battery (...)` | The power source changed |
| `ctrl: heap ...: internal N KB free` | Memory. On the 7B, about 85 KB internal before connecting and 60 KB after is normal |
| `main: turned on by touch` | Woken from deep sleep |

When the TV doesn't accept the remote during first setup, pairing starts
by itself. Later, the remote asks you first.

## Screenshots

Developer builds can take screenshots and simulated touches over Wi-Fi.
A screenshot helps in a bug report. From the `firmware/remote` folder,
run:

```sh
python tools/devtools.py shot screen.png
```

[Building](building.md#developer-tools) explains the developer tools.
