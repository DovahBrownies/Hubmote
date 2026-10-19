# Guest Wi-Fi

A QR code for a guest network, with its name and password beside it.
Guests point their phone's camera at the remote's screen to join.

- **Its screen:** the code, the network's name and password, and **Edit**.
  Until a network is set, **Set up** lists the networks around the remote
  to pick from (**Other network** to type a name), then asks for the
  password on the remote's keyboard. An open network skips the password.
- **Its card:** the code and the name; at medium and large sizes the
  password too.
- **An open network:** leave the password empty.

The code uses the standard Wi-Fi QR format (WPA, or no password), which
the cameras of current phones read.

## Settings

In **Settings > Modules > Guest Wi-Fi**, kept in NVS (`m_guest_wifi`):

- **Network name** (`ssid`), 32 characters at most, picked from the
  networks around (`.wifi`, `ui_wifi_pick()`).
- **Password** (`pass`), 63 characters at most.

## Data and privacy

It uses no network. The network's name and password are kept on the
remote, unencrypted like its other settings, and shown on its screen and
card: use a guest network, not your main one. The log never shows them.

## Credits and licenses

The QR code comes from LVGL's QR code widget, which includes Project
Nayuki's QR Code generator library (MIT); the QR code and pencil icons
are Lucide's (NOTICE).

## Author

the Hubmote authors
