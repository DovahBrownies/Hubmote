# Security

This page describes what Hubmote talks to, what it keeps, who it protects
against, and what it doesn't protect against yet. It is for developers
and for anyone checking how safe Hubmote is. To report a problem, see
[SECURITY.md](../SECURITY.md).

On this page:

- [Who it protects against](#who-it-protects-against)
- [Connections](#connections)
- [What the remote keeps](#what-the-remote-keeps)
- [Firmware you install](#firmware-you-install)
- [The companion's notification access](#the-companions-notification-access)
- [The companion updating itself](#the-companion-updating-itself)
- [Taking access back](#taking-access-back)
- [Known gaps and plans](#known-gaps-and-plans)

## Who it protects against

Hubmote sits on a home network next to phones, laptops, smart plugs and
guests' devices. The goal is that **nothing else on that network can use
Hubmote to control the TV, type into it or read what you type**, without
someone at the TV taking part.

It does not try to stop:

- **Someone who can read the network's traffic.** For example, anyone who
  knows the Wi-Fi password can capture traffic, and WPA2-Personal lets
  them decrypt other devices' traffic. Some links are plain HTTP (see
  [Connections](#connections)).
- **Someone who holds the remote.** The flash isn't encrypted, and the USB
  port can read or replace the firmware.
- **Anyone with an IR remote.** They can control IR devices anyway.

## Connections

| Link | Where | Protection |
|---|---|---|
| Google TV Streamer, Android TV Remote v2 | TV ports 6466 (control), 6467 (pairing) | TLS with a client certificate, a pairing code, a pinned TV key |
| Companion app on the Streamer | TV port 6470 | A token from pairing, with limits |
| Updates from the releases | The project's site (GitHub Pages), HTTPS | Certificate, size and SHA-256 checked |
| Module web requests (Weather) | Internet | HTTPS for Weather; not enforced for others |
| Clock | `pool.ntp.org` (SNTP) | None |
| mDNS | Local network | None |

- **Google TV Streamer.** The link uses TLS with a client certificate.
  Pairing needs the 6-character code shown on the TV, and the TV's key is
  pinned (see [Pairing and the TV's key](#pairing-and-the-tvs-key)). The
  remote's RSA-2048 key is made on the remote at its first start.
- **Companion app.** The remote sends a token from pairing as
  `Authorization: Bearer`. The companion limits guessing and requests.
  The link is plain HTTP. See [The companion's link](#the-companions-link).
- **Updates from the releases.** The server certificate is checked against
  ESP-IDF's CA bundle. The image's size and SHA-256 are checked against
  `update.json`. The remote reads `update.json` a minute after Wi-Fi comes
  up, then daily. It downloads an image only when you tap **Install**.
- **Module web requests.** Weather uses HTTPS, with server certificates
  checked against ESP-IDF's CA bundle. The module platform doesn't enforce
  HTTPS for other modules. Weather sends the chosen place's coordinates to
  Open-Meteo, and the typed place name when you search.
- **Clock.** SNTP has no protection. A wrong clock only affects the times
  shown, because certificate dates aren't checked at all
  (`MBEDTLS_HAVE_TIME_DATE` is off). This means an expired certificate is
  accepted too.
- **mDNS.** The remote announces `hubmote.local`. If another device has
  that name, it uses `hubmote-2.local` and so on, and
  **Settings > About** shows which. The remote finds the TV by its
  `_androidtvremote2._tcp` service.

### Pairing and the TV's key

- The TV's certificate is self-signed, so the remote doesn't verify it
  (`MBEDTLS_SSL_VERIFY_NONE`, as in androidtvremote2). Instead, pairing
  stores a hash of the TV's public key in NVS (`atv`/`tv_key`). Every
  control connection must show the same key. The Streamer shows the same
  key on both ports.
- With no key stored, for example after you pick a TV in **Find TVs**,
  the first connection stores the key of whatever answers there. This is
  trust on first use.
- A device at the TV's address gets no key press or text. Neither does a
  device answering mDNS with the TV's name after the TV moved. The remote
  looks for the TV elsewhere, once per address. After three tries, it says
  something else answers there.
- The companion, and its token, is only contacted while the paired TV is
  connected.
- Once paired, if the TV no longer knows the remote, the remote asks
  before pairing again, rather than pairing with whatever answers.
- At pairing, the code check covers the TV's key, but only its first
  byte, by the protocol's design. Something between the remote and the TV
  gets past it 1 time in 256. A wrong code isn't logged.

### The companion's link

- **Pairing.** Anyone on the network can make the TV show a code, at most
  one new code every 5 s. A code has 3 tries and lasts 2 minutes.
- **Lockout (since 0.6).** Wrong codes add up across codes. The 6th wrong
  code, and every 3rd after it, locks pairing for 1 minute, then 2, 4 and
  so on, up to 4 hours. The lockout is kept across restarts. The count
  resets when a remote pairs.
- **Tokens.** The companion keeps tokens for up to 8 remotes, dropping the
  oldest.
- **Without a token**, every request except `/status` and `/pair*` gets
  401. A request carrying `Origin`, `Sec-Fetch-*` or a host name instead
  of an address gets 403 (signs of a web page or DNS rebinding).
- **Limits (since 0.6).** At most 24 connections (8 per address), 32
  header lines and 10 s per request.
- **With the token**, a remote can:
  - list and open apps;
  - follow the focused field (its type, never its text);
  - type into it, or press the keys of an app's on-screen keyboard;
  - press Enter, and hide the TV's keyboard;
  - open Google TV's search;
  - read what is playing;
  - make the companion install a newer version of itself from a given
    address.

  Android only accepts an APK of the same app signed with the same key.
  The address can be any http(s) URL, so a token holder can also make the
  TV fetch a URL of its choice. The answer isn't returned.
- **Plain HTTP.** Someone reading the network can see the token and the
  text typed through the remote, including passwords typed into TV
  fields.

### Developer builds

These links exist only in developer builds (`CONFIG_HM_DEVELOPER`). That
option is on by default for local builds, and off in release builds such
as the web installer's.

| Link | Port | Protection |
|---|---|---|
| Firmware updates (`hm-ota.ps1`, `hm_ota.py`) | 8080 | Signed with the update token |
| Screenshots and simulated touches (`devtools.py`) | 8080 | Signed with the update token |
| Log over Wi-Fi (`hm-netlog.ps1`, `hm_netlog.py`) | 2323 | None |

#### Firmware updates

Each request is signed with the update token (see `main/ota.h`):

- The PC gets a one-time nonce from `GET /nonce`, valid for 60 s, and
  sends it back in `X-OTA-Nonce`.
- `X-OTA-Auth` holds an HMAC-SHA256 over the nonce, the method, the path
  with its query, and the body's SHA-256.
- `X-OTA-Body` holds the body's SHA-256. The image must match the signed
  hash.

Notes:

- The token never crosses the network. It isn't in any image either,
  because the remote makes it up.
- A device posing as `hubmote.local` can only pass on the request it was
  given, unchanged.
- `GET /nonce` answers anyone, and the remote keeps only the last 4
  nonces. So anyone on the network can spoil a pending request.
- Each refused request holds the server for a second.
- The image itself goes over plain HTTP, and a developer build carries
  the Wi-Fi details from `secrets.env`, if any.
- `GET /version` answers anyone.
- The server takes at most 3 connections. The oldest idle one makes way.

#### Screenshots and simulated touches

- Requests are signed the same way as updates.
- A screenshot shows whatever is on screen, including the Wi-Fi password
  while it is typed. The keyboard shows `*` for fields the companion
  reports as passwords.
- A gesture lasts 10 s at most.

#### Log over Wi-Fi

- Anyone on the network can read it.
- The log leaves out the update token and typed text. For typed text, it
  shows only the length. The Wi-Fi driver logs only its warnings, which
  rarely name the network.
- It does show the TV's name and the Weather place, which is often the
  home's town.
- Debug-level lines do print field text, but they aren't compiled in at
  the default level (`CONFIG_LOG_MAXIMUM_LEVEL` INFO). Don't raise the
  level on a shared network.
- The remote serves one client, the newest.
- The PC tools drop control characters from what they show and save.

A **release build** opens no ports of its own besides mDNS. It makes only
the outgoing connections in the [first table](#connections).

## What the remote keeps

In NVS (the `nvs` partition), not encrypted:

| NVS namespace | What it holds |
|---|---|
| `atv` | The Android TV client key and certificate, the TV's address, a hash of the TV's public key |
| `wifi` | The Wi-Fi network name and password |
| `ota` | The update token (developer builds) |
| `companion` | The companion token |
| `ctrl`, `hub_off`, `m_<module>` | Settings; Weather's include the chosen place's name and coordinates |

In the `storage` partition (LittleFS):

- learned IR codes;
- modules' files, such as Weather's last forecast;
- the TV's app icons (`icons/`), which show which apps are installed on
  the TV.

> [!WARNING]
> Anyone with the remote and a USB cable can read all of this.

The ESP32-S3 supports flash encryption and secure boot. Hubmote doesn't
turn them on, because they make recovery over USB and updates much harder
for a hobby device.

On the PC, `secrets.env` (gitignored) may hold the Wi-Fi details and the
update token.

> [!CAUTION]
> A developer build carries the Wi-Fi details from `secrets.env` as plain
> text, so don't share its images. `tools/web/make_site.py` refuses to put
> one on a site. Release builds have none.

## Firmware you install

- **The web installer** flashes a release build made by GitHub Actions
  from the public source. It is served from GitHub Pages over HTTPS. The
  images aren't signed.
- **Updates from the releases** work in every build, from
  **Settings > About**. The remote downloads `update.json` and its board's
  image from the project's site over HTTPS, with the certificate checked
  against ESP-IDF's CA bundle. It checks the image's size and SHA-256
  against `update.json`.
  - The image's path may only name a file below `update.json`. It may use
    letters, digits, `.`, `_`, `-`, and `/` between names, and no name may
    start with a dot.
  - The hash comes from the same site. It guards against a damaged
    download, not a compromised site, and the image isn't signed.
  - Developer builds also accept a plain-HTTP site, for testing from a
    PC.
  - Nothing installs without a tap on **Install**.
- **Updates over Wi-Fi from a PC** exist only in developer builds, and
  must be signed with the update token.
- **Rollback.** A new image that never reaches Wi-Fi is rolled back on its
  next restart. This protects against broken builds, not against bad
  ones.

## The companion's notification access

Since 0.6, the companion has notification access. `install.py` and
`install.ps1` grant it over ADB. The companion needs it because Android
only shows media sessions (what is playing) to an app with an enabled
notification listener.

Its listener reads no notifications. The access itself would allow it,
which is why it is listed here.

What the companion does read is the playing app's title and subtitle.
It goes to a paired remote over the same plain-HTTP link as typed text. The
remote's log shows only the app and the state, not the title.

## The companion updating itself

- **Permissions.** Since 0.5, the companion holds
  `REQUEST_INSTALL_PACKAGES` and `UPDATE_PACKAGES_WITHOUT_USER_ACTION`.
  `install.py` and `install.ps1` also allow it to install apps ("Install
  unknown apps"). It uses these only for `/update`. It installs an APK
  only if it is the same package and a higher version, and Android only
  accepts it if the same key signed it.
- **The installer's answer** is a broadcast only this app may send. It is
  a signature permission, and Android 13+ also keeps it inside the app.
  So other apps can't make the companion open an activity of their
  choice.
- **The download** is checked against the size and SHA-256 the remote
  sends, which come from `update.json` over HTTPS. The companion accepts
  plain HTTP too, for testing from a PC. So it relies on that hash and on
  Android's key check, not on the connection.
- **Confirmation.** Where Android allows installs from apps, an update can
  happen without a confirmation on the TV. Where the system forbids them,
  nothing is installed and the remote doesn't offer the update. Some
  Google TV setups forbid them, and the cause isn't known yet.

## Taking access back

### The companion

1. On the Streamer, open **Settings > Apps > Hubmote Companion** and
   choose **Clear data**. The companion forgets every paired remote.
2. Clearing the data also switches the companion's accessibility service
   off. Turn it on again in **Settings > System > Accessibility**.
3. On the remote, pair again from **Settings > Remotes > Pair companion**.

### The Streamer pairing

**Pair again** (in **Settings > Remotes**) pairs once more with the same
key, so it revokes nothing.

To make the TV forget every paired remote, phones included, clear the
data of the Streamer's Android TV Remote Service. Find it in
**Settings > Apps > See all apps**, with system apps shown.

To get a new key on the remote, erase the remote with the web installer
(tick **Erase device**), then set it up again.

### The update token

1. Erase the board over USB. The installer's **Erase device** does this.
   It also forgets the pairing, Wi-Fi and learned remotes.
2. Install a developer build. It makes up a new token, and the old one
   stops working.
3. Copy the new token from **Settings > About** into `secrets.env`.

## Known gaps and plans

- **Plain HTTP to the companion.** TLS, with the companion's certificate
  pinned at pairing, would hide the token and typed text.
- **Companion requests can stall the controller task.** Typing, launching
  and pairing run on the controller task. A device answering at the TV's
  address can hold it for up to about 30 s per request. The connect,
  header and body deadlines are checked between reads, and a read can
  wait 5 s.
- **No signature on release images.** Updates from the releases should
  check a signature on the image, not only the HTTPS download and a hash
  from the same site.
- **Certificate dates aren't checked**, because there is no trusted clock
  at start-up. So an expired certificate is accepted.
- **Developer builds are for your own network.** Their update server and
  log are meant for a developer's own network. On a network you share,
  use a release build.
