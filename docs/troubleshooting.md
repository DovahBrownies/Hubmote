# Troubleshooting

## Common problems

| Symptom | What to check |
|---|---|
| The installer can't connect | Close the message and click Install again. Then unplug the board, hold BOOT while plugging it back in, and try again. No port at all: a charge-only cable. A port named CH343 is the UART connector: use the one marked USB. |
| "Couldn't find" or "didn't accept the password" on Wi-Fi | It must be a 2.4 GHz network. Retype the password (the keyboard shows it as you type). |
| "No Google TV found" | Is the Streamer on, and on the same network (and Wi-Fi band) as the remote? Tap **Search again**. |
| "Can't reach your TV" | Is the TV on, and on the same network? **Try again** searches the network again. |
| "Not your TV" | Another device answers at the TV's old address. **Try again** lists the TVs: pick yours. |
| "Pair with your TV again" | The TV forgot this remote (or was reset). Tap **Try again** and type the code the TV shows. |
| "Pairing didn't finish" | **Try again** shows a new code on the TV. |
| Volume does nothing | The Streamer isn't passing volume to the TV: set its volume control to the TV over HDMI-CEC, and turn HDMI-CEC on at the TV ([getting started](getting-started.md#3-volume)). The log shows `ctrl: tv volume 0/0`. |
| Picture upside down | Settings > Screen > **Rotate 180°**; touch turns with it. |
| Google TV's Home shows only 5 apps | The companion app isn't installed, paired or reachable ([companion app](companion.md#problems)). |
| The keyboard doesn't open in an app | It opens by itself only when the TV asks for one; tap the rail's **Keyboard** key. |
| Typing in an app does nothing | Prime Video can't be typed into: search from Google TV's Home. Netflix takes typing only while its own keyboard has focus. Elsewhere, and for search on Google TV's Home: is the companion paired? |
| The IR transmitter stays dark | SW1 must be on UART2 ([hardware](hardware.md#ir)). Check with a phone camera while tapping a TV key. Still dark: a build with the LED on H3 pin 4 (`HM_IR_TX_GPIO` 43, [building](building.md#options)). |
| The board doesn't start on battery | Check the battery plug's polarity ([hardware](hardware.md#battery)). Then plug in USB with the battery connected: the CHARGE LED should light, which also wakes a cell whose protection tripped. |
| The top bar shows `--%` | No cell is connected, or its power switch is off. |
| About says "update didn't work" | The log's `update:` lines say why (no internet, a download cut short, a checksum mismatch). Tap Check to try again; nothing changes until an install completes. |

## Logs

The remote's log says what it's doing and why something failed. Leave out
your network's name, addresses and tokens before sharing one (the remote
keeps the Wi-Fi driver's lines, which name the network, out of it).

**Over USB** (any build): plug in the board's USB port and open its serial
port at 115200 baud with any serial monitor, or in the repo
(`firmware/remote`):

```powershell
.\scripts\hm-monitor.ps1 COM5                                    # live
.\scripts\hm-capture.ps1 -Port COM5 -Seconds 60 -Out boot.log -Reset   # restart and capture the start
```

**Over Wi-Fi** (developer builds, [building](building.md)): the remote
serves its log on TCP port 2323, including the last 32 KB:

```powershell
.\scripts\hm-netlog.ps1 -Remote hubmote.local
python tools/hm_netlog.py --remote hubmote.local     # any OS
```

It reconnects by itself after the remote restarts. The remote serves one
log client at a time (the newest). A second remote on the network answers
to `hubmote-2.local`; Settings > About shows the name and address.

**The companion's log** (needs ADB, [companion app](companion.md)):
`adb logcat -s HubmoteCompanion`.

## Useful log lines

| Line | Meaning |
|---|---|
| `net: up: 192.168.1.50` | Wi-Fi connected |
| `atv: TLS up with <ip>:6466`, `state CONNECTING -> CONNECTED` | Connected to the Streamer |
| `atv: TLS handshake ... -0x0050 ... -> certificate not accepted` | The TV doesn't accept this remote: at the first setup pairing starts by itself, later the remote asks first |
| `ctrl: "<name>" moved from A to B` | The TV changed address and was found again |
| `ctrl: tv volume N/M` | The Streamer's volume; `0/0` means it isn't controlling the TV's |
| `ctrl: app: <package>` | The app on the TV |
| `ctrl: key <code> <tap\|down\|up>: ESP_OK` | A key went to the Streamer |
| `companion: online`, `companion: N apps`, `companion: field: text (N)` | The companion app |
| `ctrl: text (N chars) via companion: ESP_OK` | Typing through the companion (lengths only, never the text) |
| `ir_ctl: tv <key> (...): ESP_OK` | An IR code was sent |
| `update: release 0.1.1, running 0.1.0: update available` | Updates from the releases |
| `ota: new firmware confirmed (ota_1)` | A new version reached Wi-Fi and stays |
| `battery: on USB (...)`, `battery: on battery (...)` | The power source changed |
| `ctrl: heap ...: internal N KB free` | Memory; on the 7B, about 90-115 KB of internal RAM is normal |
| `main: turned on by touch` | Woken from deep sleep |

## Screenshots

Developer builds can take a screenshot and simulated touches over Wi-Fi,
handy for bug reports: `python tools/devtools.py shot screen.png`
([building](building.md#developer-tools)).
