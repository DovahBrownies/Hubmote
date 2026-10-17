# Security

What Hubmote talks to, what it keeps, who it protects against, and what it
doesn't protect against yet. To report a problem, see
[SECURITY.md](../SECURITY.md).

## Who it protects against

Hubmote sits on a home network next to phones, laptops, smart plugs and
guests' devices. The goal is that **nothing else on that network can use
Hubmote to control the TV, type into it or read what you type**, without
someone at the TV taking part.

It does not try to stop:
- **someone who can read the network's traffic**, for example anyone who
  knows the Wi-Fi password and captures traffic (WPA2-Personal lets them
  decrypt other devices' traffic). Some links are plain HTTP (below);
- **someone who holds the remote**: the flash isn't encrypted and the USB
  port can read or replace the firmware;
- anyone with an IR remote, who can control IR devices anyway.

## Connections

| Link | Where | Protection | Notes |
|---|---|---|---|
| Google TV Streamer, Android TV Remote v2 | TV ports 6466 (control), 6467 (pairing) | TLS with a client certificate; pairing needs the 6-character code shown on the TV; the TV's key is pinned (below). | The remote's RSA-2048 key is made on the remote at its first start. |
| Companion app on the Streamer | TV port 6470 | A token from pairing, sent as `Authorization: Bearer`; limits on guessing and on requests (below). | Plain HTTP (below). |
| Updates from the releases | The project's site (GitHub Pages), HTTPS | Server certificate checked against ESP-IDF's CA bundle; the image's size and SHA-256 checked against `update.json`. | Reads `update.json` a minute after Wi-Fi comes up, then daily; downloads only when you tap Install. |
| Module web requests (Weather) | Internet | Weather uses HTTPS, with server certificates checked against ESP-IDF's CA bundle; the module platform doesn't enforce HTTPS for other modules. | Weather sends the chosen place's coordinates to Open-Meteo, and the typed place name when searching. |
| Clock | `pool.ntp.org` (SNTP) | None (SNTP has none). | A wrong clock only affects shown times, because certificate dates aren't checked at all (`MBEDTLS_HAVE_TIME_DATE` off): an expired certificate is accepted too. |
| mDNS | Local network | None. | Announces `hubmote.local` (`hubmote-2.local` and so on if another device has the name; Settings > About shows which), and finds the TV by its `_androidtvremote2._tcp` service. |

### Pairing and the TV's key

- The TV's certificate is self-signed, so it isn't verified
  (`MBEDTLS_SSL_VERIFY_NONE`, as in androidtvremote2). Instead, pairing
  stores a hash of the TV's public key (NVS `atv`/`tv_key`; the Streamer
  shows the same key on both ports), and every control connection must
  show the same key.
- With no key stored, for example after you pick a TV in Find TVs, the
  first connection stores the key of whatever answers there (trust on
  first use).
- A device at the TV's address, or answering mDNS with the TV's name after
  the TV moved, gets no key press or text. The remote looks for the TV
  elsewhere, once per address, and after three tries says something else
  answers there. The companion (and its token) is only contacted while the
  paired TV is connected.
- Once paired, a TV that no longer knows the remote makes it ask before
  pairing again, rather than pairing with whatever answers.
- At pairing, the code check covers the TV's key, but only its first byte
  (the protocol's design): something between the remote and the TV gets
  past it 1 time in 256. A wrong code isn't logged.

### The companion's link

- **Pairing:** anyone on the network can make the TV show a code (at most
  one new code every 5 s). The code has 3 tries and lasts 2 minutes. Wrong
  codes add up across codes: the 6th wrong code, and every 3rd after it,
  locks pairing for 1 minute, then 2, 4... up to 4 hours, kept across
  restarts; the count resets when a remote pairs (since 0.6). The companion
  keeps tokens for up to 8 remotes, dropping the oldest.
- **Without a token**, every request but `/status` and `/pair*` gets 401. A
  request carrying `Origin`, `Sec-Fetch-*` or a host name instead of an
  address (a web page, DNS rebinding) gets 403. At most 24 connections (8
  per address), 32 header lines and 10 s per request (since 0.6).
- **With the token** a remote can list and open apps, follow the focused
  field (its type, never its text), type into it or press the keys of an
  app's on-screen keyboard, press Enter, hide the TV's keyboard, open
  Google TV's search, read what is playing, and make the companion install
  a newer version of itself from a given address. Android only accepts an
  APK of the same app signed with the same key. That address can be any
  http(s) URL, so a token holder can also make the TV fetch a URL of its
  choice (the answer isn't returned).
- **Plain HTTP:** someone reading the network can see the token, and the
  text typed through the remote (including passwords typed into TV fields).

### Developer builds

Only in developer builds (`CONFIG_HM_DEVELOPER`, the default for local
builds; off in release builds such as the web installer's):

| Link | Port | Protection | Notes |
|---|---|---|---|
| Firmware updates (`hm-ota.ps1`, `hm_ota.py`) | 8080 | Requests signed with the update token: `X-OTA-Auth`, an HMAC-SHA256 over a one-time nonce (`X-OTA-Nonce`, from `GET /nonce`, valid 60 s), the method, the path with its query, and the body's SHA-256 (`X-OTA-Body`; `main/ota.h`). The image must match the signed hash. | The token never crosses the network, and isn't in any image (the remote makes it up). A device posing as `hubmote.local` can only pass on the request it was given, unchanged. `GET /nonce` answers anyone and the remote keeps only the last 4 nonces, so anyone on the network can spoil a pending request; each refused request holds the server for a second. The image itself goes in plain HTTP: a developer build carries the Wi-Fi details from `secrets.env`, if any. `GET /version` answers anyone. At most 3 connections; the oldest idle one makes way. |
| Screenshots and simulated touches (`devtools.py`) | 8080 | Signed the same way. | A screenshot shows whatever is on screen, including the Wi-Fi password while it is typed. The keyboard shows `*` for fields the companion reports as passwords. A gesture lasts 10 s at most. |
| Log over Wi-Fi (`hm-netlog.ps1`, `hm_netlog.py`) | 2323 | None: anyone on the network can read it. | The log leaves out network names (the Wi-Fi driver logs warnings only), the update token, and typed text (only its length). It does show the TV's name and the Weather place (often the home's town). Debug-level lines do print field text, but they aren't compiled in at the default level (`CONFIG_LOG_MAXIMUM_LEVEL` INFO): don't raise it on a shared network. The remote serves one client, the newest. The PC tools drop control characters from what they show and save. |

A **release build** opens no ports of its own besides mDNS, and makes only
the outgoing connections in the first table.

## What the remote keeps

In NVS (the `nvs` partition), not encrypted:
- the Android TV client key and certificate, the TV's address and a hash
  of its public key (`atv`);
- the Wi-Fi network name and password (`wifi`);
- the update token (`ota`; developer builds) and the companion token
  (`companion`);
- settings (`ctrl`, `hub_off`, `m_<module>`); Weather's include the chosen
  place's name and coordinates.

In the `storage` partition (LittleFS): learned IR codes, modules' files
(Weather's last forecast), and the TV's app icons (`icons/`, which shows
which apps are installed on the TV).

Anyone with the remote and a USB cable can read all of this. The ESP32-S3
supports flash encryption and secure boot; Hubmote doesn't turn them on,
because they make recovery over USB and updates much harder for a hobby
device. On the PC, `secrets.env` (gitignored) may hold the Wi-Fi details and
the update token. A developer build carries the Wi-Fi details from it as
plain text: don't share its images (`tools/web/make_site.py` refuses to
put one on a site). Release builds have none.

## Firmware you install

- **The web installer** flashes a release build made by GitHub Actions from
  the public source, served from GitHub Pages over HTTPS. The images aren't
  signed.
- **Updates from the releases** (every build, Settings > About): the
  remote downloads `update.json` and its board's image from the project's
  site over HTTPS (certificate checked against ESP-IDF's CA bundle) and
  checks the image's size and SHA-256 against `update.json`. The image's
  path may only name a file below `update.json` (letters, digits, `.`,
  `_`, `-`, and `/` between names, none starting with a dot). The hash
  comes from the same site, so it guards against a damaged download, not a
  compromised site; the image isn't signed. Developer builds also accept a
  plain-HTTP site, for testing from a PC. Nothing installs without a tap on
  Install.
- **Updates over Wi-Fi from a PC** exist only in developer builds and must
  be signed with the update token.
- A new image that never reaches Wi-Fi is rolled back on its next restart,
  which protects against broken builds, not against bad ones.

## The companion's notification access

Since 0.6 the companion has notification access (`install.py` /
`install.ps1` grant it over ADB), because Android only shows media sessions
(what is playing) to an app with an enabled notification listener. Its
listener reads no notifications; the access itself would allow it, which
is why it is listed here. What it does read, the playing app's title and
subtitle, goes to a paired remote over the same plain-HTTP link as typed
text, and the remote's log shows only the app and the state, not the title.

## The companion updating itself

- Since 0.5 the companion holds `REQUEST_INSTALL_PACKAGES` and
  `UPDATE_PACKAGES_WITHOUT_USER_ACTION`, and `install.py` / `install.ps1`
  allow it to install apps ("Install unknown apps"). It uses them only for
  `/update`: it installs an APK only if it is the same package and a
  higher version, and Android only accepts it if the same key signed it.
- The installer's answer is a broadcast only this app may send (a
  signature permission; Android 13+ also keeps it inside the app), so
  other apps can't make the companion open an activity of their choice.
- The download is checked against the size and SHA-256 the remote sends,
  which come from `update.json` over HTTPS. The companion accepts plain
  HTTP too (for testing from a PC), so it relies on that hash and on
  Android's key check, not on the connection.
- Where Android allows installs from apps, an update can happen without a
  confirmation on the TV. Where the system forbids them (some Google TV
  setups; the cause isn't known yet), nothing is installed and the remote
  doesn't offer it.

## Taking access back

- **Companion:** on the Streamer, Settings > Apps > Hubmote Companion >
  Clear data forgets every paired remote (it also switches the companion's
  accessibility service off: turn it on again in Settings > System >
  Accessibility); pair again from Settings > Remotes > Pair companion.
- **The Streamer pairing:** Pair again (Settings > Remotes) pairs once more
  with the same key, so it revokes nothing. To make the TV forget every
  paired remote (phones included), clear the data of the Streamer's Android
  TV Remote Service (Settings > Apps > See all apps > show system apps).
  For a new key on the remote, erase it with the web installer (tick Erase
  device), then set it up again.
- **Update token:** erase the board over USB (the installer's Erase device
  does it; it also forgets the pairing, Wi-Fi and learned remotes), then
  install a developer build: it makes up a new token, and the old one stops
  working. Copy the new one from Settings > About into `secrets.env`.

## Known gaps and plans

- The companion link is plain HTTP. TLS with the companion's certificate
  pinned at pairing would hide the token and typed text.
- Companion requests (typing, launching, pairing) run on the controller
  task: a device answering at the TV's address can hold it for up to about
  30 s per request (the connect, header and body deadlines are checked
  between reads, and a read can wait 5 s).
- Updates from the releases should check a signature on the image, not
  only the HTTPS download and a hash from the same site.
- Certificate dates aren't checked (no trusted clock at start-up), so an
  expired certificate is accepted.
- Developer builds' update server and log are for a developer's own
  network: use a release build on a network you share.
