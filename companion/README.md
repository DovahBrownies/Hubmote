# Hubmote companion (Google TV Streamer)

A small Android TV app for the Streamer. It tells the remote which apps
are installed, with icons ready for its screen, and opens any of them. It
reports whether the focused field is text, a password or a number, and
types into that field. It hides the TV's on-screen keyboard while the
remote's is open. It never reads a field's contents.

It is a single accessibility service with no screen of its own. The
system starts it at boot and keeps it running, and accessibility services
may open apps from the background.

**Installing, pairing and updating it** are in
[docs/companion.md](../docs/companion.md). This file is about how it
works: its versions, files, API and build.

## Versions

0.6 is the first release. The API notes below say which version added
what.

- **0.1 to 0.3:** the app list with icons, launching, the focused field's
  type, typing into fields, hiding the TV's keyboard.
- **0.4:** [pairing](#pairing), so only remotes that paired may use the
  API; typing into apps' own on-screen keyboards; `/search` for Google
  TV's search.
- **0.5:** [updates itself](#updating-itself) when the remote asks
  (`/update`); `/status` adds the version name and whether updates can
  work.
- **0.6:** tells the remote what is playing (`/media`, from Android's
  media sessions). This needs notification access, which `install.ps1`
  and `install.py` grant. Also a pairing lockout after wrong codes, and
  limits on requests.
- **0.7:** shows the remote's messages at the top right of the TV
  (`/notify`), such as the Sleep timer's and Screen time's warnings.

## Files

| File | What it is |
|---|---|
| `src/io/github/dovahbrownies/hubmote/CompanionService.java` | The accessibility service: the focused field, typing, the TV's keyboard, launching apps; it owns the server |
| `src/.../Server.java` | The HTTP server (one thread per connection) and the event stream |
| `src/.../Pairing.java` | Pairing codes and the paired remotes' tokens |
| `src/.../Message.java` | A message at the top right of the TV, with a ring that empties until it goes (`/notify`) |
| `src/.../OnScreenKeyboard.java` | Finding an app's own on-screen keyboard and its keys |
| `src/.../Apps.java` | The TV app list, and icons rendered to RGB565A8 |
| `src/.../Updater.java` | Updating itself: download, checks, Android's package installer |
| `src/.../NowPlaying.java`, `src/.../MediaListener.java` | What is playing, from Android's media sessions |
| `AndroidManifest.xml`, `res/` | Manifest (package `io.github.dovahbrownies.hubmote`), accessibility config, strings |
| `setup-tools.ps1` | Installs JDK 17 and the Android SDK on the PC, once ([below](#set-up-the-pc-once)) |
| `build.ps1`, `version.txt` | Builds the APK without Gradle ([below](#build)); the version comes from `version.txt` |
| `install.ps1`, `install.py` | Install the APK over ADB and switch everything on ([below](#ways-to-install-and-update-it)); `install.py` runs on any OS |
| `debug.keystore` | The signing key, created by the first build. **Keep it.** |

`MediaListener` only unlocks the media sessions; it reads no
notifications.

## Ways to install and update it

| Way | State |
|---|---|
| Over ADB from a PC ([the steps](../docs/companion.md#from-a-pc)) | Works; every version so far went on this way |
| From the remote: **Update companion** (0.5+) | Built; not yet seen on a Streamer that allows installs |
| Downloader on the TV, no PC ([the steps](../docs/companion.md#on-the-tv-without-a-pc)) | Not tried yet |
| A phone app with Wireless debugging (e.g. Bugjaeger) | Not tried; the fallback |
| The remote installing it over Wireless debugging | Planned, not built |
| Google Play | Not published there |

- **Over ADB:** `install.ps1` or `install.py` installs the APK and enables
  the accessibility service. It also allows the companion to install apps
  (for updating itself), and grants its media listener notification
  access (for what's playing). Updating this way keeps the pairing.
- **From the remote:** it is meant to install by itself where the
  companion may install apps, which the install scripts allow. Otherwise
  Android asks on the TV. See [Updating itself](#updating-itself).
- [Issue #17](https://github.com/DovahBrownies/Hubmote/issues/17) tracks trying the Downloader route.

### TVs that block installs from apps

Some Google TV setups have the `no_install_unknown_sources` restriction,
or `no_install_unknown_sources_globally`. The cause isn't known yet;
`adb shell dumpsys user` lists the restriction for the main user. On these
TVs:

- Unknown sources is greyed out for every app.
- Android's installer closes without showing anything.
- The companion reports `"updates":false`, and the remote shows the newer
  version without the button.
- The Downloader route can't work, and updates go over ADB.

### Signing keys

Android only installs an update signed with the same key as the installed
app. Release APKs use the project's key; your own builds use
`debug.keystore`. Moving between them means uninstalling, installing and
pairing again, once.

## API

The API is plain HTTP on port **6470** of the Streamer. The server's
limits:

- Requests a web page could make get `403`. These are requests with an
  `Origin` or `Sec-Fetch-*` header, or a `Host` that is a name rather than
  an address.
- More than 32 header lines get `431`.
- A request that isn't complete within 10 s, or is silent for 5 s, is
  closed without an answer.
- At most 24 connections are open at once, 8 per address.

### Pairing

Since 0.4, every request except `/status` and `/pair*` needs
`Authorization: Bearer <token>`. Without one, it gets
`401 {"error":"pairing needed"}`. A remote gets its token once, with these
requests:

- **`POST /pair/start`** shows a 6-character code (0-9, A-F) over whatever
  is on the TV, for 2 minutes. If a code is already showing, it shows that
  one; otherwise a new one.
  - `429` if no code is showing and the last one went up under 5 s ago.
  - `423 {"error":"locked","retry":S}` after too many wrong codes (0.6+).
    Wrong codes count across codes. The 6th locks pairing for 1 minute.
    After that, every 3rd wrong code locks it for twice as long as the
    time before (2, 4, 8... minutes, at most 4 hours), until a remote
    pairs. The lock survives restarts.
- **`POST /pair?code=C`** answers `{"token":"<32 hex>"}` if C is the code
  on the TV, and the code then disappears.
  - `403 {"error":"wrong code","left":N}` if not. After 3 wrong codes, the
    code is taken off the TV.
  - `410` if no code is showing.
- **`POST /pair/cancel`** takes the code off the TV.

How pairing works:

- The code is drawn as an accessibility overlay
  (`TYPE_ACCESSIBILITY_OVERLAY`), which needs no extra permission.
- Tokens are kept in the app's private storage, for up to 8 remotes; the
  oldest goes first. Updating the app keeps them.
- **Clear data** in **Settings > Apps** forgets them all. It also switches
  the accessibility service off. Turn it on again in **Settings > System >
  Accessibility**.
- Tokens and codes are compared in constant time.

### Requests

These need a token, except `/status`.

- **`GET /status`** answers:

  ```json
  {"version":6,"name":"0.6","updates":true,"media":true,"paired":true,"field":"none","id":0}
  ```

  - Without a token, it answers the same up to `paired` (false).
  - `version` is the app's version code. 0.4 sent 4, without `name`,
    `updates` and `media`.
  - `updates` is false where the TV doesn't let apps install
    ([above](#tvs-that-block-installs-from-apps)).
  - `media` is false without notification access.
- **`GET /media`** says what is playing:

  ```json
  {"pkg":"com.spotify.tv.android","title":"...","subtitle":"...","state":"playing","duration":215000,"position":41603}
  ```

  - With nothing to show, it answers `{"state":"none"}`.
  - `state` is playing, paused, buffering, stopped, or none for any other
    state. Times are in ms.
  - The session shown is the first one playing, else the first with a
    title.
  - Titles lose their accents, because the remote's fonts are ASCII, and
    stop at 90 characters.
  - Apps decide what they share. On a Streamer (2026-10-16), YouTube sends
    the video and channel, and Spotify the track. Netflix's session has no
    metadata, Prime Video's title is only "PrimeVideo", and Plex publishes
    no session.
- **`GET /apps`** answers the apps, sorted by label:

  ```text
  {"apps":[{"pkg":"com.netflix.ninja","label":"Netflix","updated":1759999999999}, ...]}
  ```

  `updated` changes when the app updates, so the remote can cache icons.
- **`GET /icon?pkg=P&size=N`** answers the icon, N × N (16–256), as LVGL 9
  RGB565A8 bytes: RGB565 little-endian pixels, then one alpha byte per
  pixel. Corners are rounded like the remote's Home tiles (26 px at
  112 px).
- **`POST /launch?pkg=P`** opens the app's TV launch activity.
- **`POST /text?value=T`** replaces the focused field's text with T
  (percent-encoded), using accessibility `ACTION_SET_TEXT`. This works
  where the TV's input method never reaches the remote protocol, such as
  in browsers.
  - Without a text field, it types T into the app's own on-screen keyboard
    ([below](#on-screen-keyboards)).
  - `404` if there is neither.
- **`POST /search?q=Q`** opens Google TV's own search with Q
  (`SearchManager.INTENT_ACTION_GLOBAL_SEARCH`). That shows Gemini's
  answer or a title's page, with results from every app. The remote uses
  it for typing on Home.
- **`POST /enter`** presses the focused field's Enter / Go / Search
  (`ACTION_IME_ENTER`, Android 11+), or the on-screen keyboard's Search /
  Enter key. If that keyboard has no such key, it presses nothing and
  answers 200, because such keyboards search as you type.
- **`POST /keyboard?hidden=1|0`** hides (1) or allows (0) the TV's
  on-screen keyboard, with the soft keyboard controller's
  `SHOW_MODE_HIDDEN`, then `SHOW_MODE_AUTO`. The keyboard is allowed again
  by itself when no remote is connected to `/events`.
- **`POST /notify?text=T&seconds=S`** shows T (percent-encoded, 160
  characters at most) at the top right of the TV for S seconds (3 to 30,
  10 if left out), over whatever is on screen, with a ring that empties
  until the message goes. A new message replaces the one showing. `400`
  without text or with too much (0.7+).
- **`POST /update?url=U&size=N&sha256=H`** updates the companion from the
  APK at U (http or https, N bytes, that SHA-256 in hex), in the
  background ([below](#updating-itself)). `400` if the request is unusable
  or the TV doesn't let apps install.
- **`GET /events`** stays open and sends one JSON line per change,
  starting with the current state:
  - `{"field":"none|text|password|number","id":N}`. `id` changes whenever
    a different field gets focus.
  - While updating: `{"update":"downloading|installing|confirm|failed"}`.
  - When the title or playback state changes: `{"media":{...}}`, as
    `/media` without the position.
  - An empty line every 15 s.

### Apps, icons and fields

- **Apps listed:** those with a TV launcher entry
  (`CATEGORY_LEANBACK_LAUNCHER`).
- **Icons:** adaptive icons are drawn full-bleed and then rounded; other
  icons are drawn on white.
- **Field type** comes from the input-focused node's `inputType`. Number
  or phone gives `number`; the password variations give `password`.
- **The focused field** is found across all windows (`findFocus`, with
  `flagRetrieveInteractiveWindows`), because the TV's keyboard window is
  often the active one.

### On-screen keyboards

`OnScreenKeyboard.java`, since 0.4. Some apps, such as Plex, draw a grid
of letter keys instead of a text field.

- **Finding the keys.** There are no rules for particular apps. A key is
  anything actionable labelled with one letter or digit, or named space,
  backspace, clear or search/enter by its label or view id
  (`flagReportViewIds`). A screen with keys for 20 or more letters counts
  as a keyboard, and is reported as a `text` field.
- **Typing.** `/text` works out the difference to what it typed before:
  Backspace for the end, Clear when nothing stays. It starts each new
  keyboard with Clear.
- **Pressing keys.** It presses keys the way a TV user does: focus on the
  key, then the D-pad's OK (`GLOBAL_ACTION_DPAD_CENTER`, Android 13+).
  Plex ignores an accessibility click on its keys. Keys that can't take
  focus, and every key before Android 13, get an accessibility click.
- **Timing.** 120 ms from focus to OK, 150 ms between keys, and 500 ms
  before the same key again (Plex drops quicker repeats, even from a real
  remote). Requests that pile up skip to the newest text.
- **Apps without keys.** Apps that draw their whole interface as one
  surface (Netflix, YouTube, Prime Video) expose no keys; the remote sends
  those key presses.
- **Checked** on the Streamer (2026-10-15) with Plex: typing, editing with
  Backspace, repeated letters, Clear. The Play Store and Spotify have real
  text fields.

### Updating itself

`Updater.java`, since 0.5:

1. The remote reads the release's companion from `update.json`.
2. On **Settings > Remotes > Update companion**, the remote sends the
   APK's address, size and SHA-256 to `/update`.
3. The companion downloads it into its cache. It checks the size, the
   hash, that it is this app and that it is a newer version.
4. It installs it with a `PackageInstaller` session. Android only accepts
   it if the same key signed it, so the companion can't install anything
   else.
5. Android stops the app and starts the new version. The remote sees it
   in `/status`.

Whether Android asks first:

- **Without asking** (Android 12+, `UPDATE_PACKAGES_WITHOUT_USER_ACTION`):
  only while the companion may also "install unknown apps". `install.ps1`
  and `install.py` allow that over ADB
  (`appops set ... REQUEST_INSTALL_PACKAGES allow`).
- **Otherwise** Android shows its install dialog on the TV, and the remote
  shows "confirm". Confirm it with the remote's D-pad. Tapping **Update
  companion** again shows the dialog again.
- **Not at all** where the system forbids installs from apps
  ([above](#tvs-that-block-installs-from-apps)).

Cleartext is allowed (`usesCleartextTraffic`), so an update can come from
a PC on the home network while testing. The hash and Android's key check
guard it.

## Security

Only paired remotes can call it, and pairing needs someone to read the
code off the TV screen. The connection is still plain HTTP, so a device
that can read the home network's traffic could see the token and the text
typed through the remote. A paired remote can make it install an update
of itself, never another app. More in
[docs/security.md](../docs/security.md).

## Rules for changes

- **Never do network I/O in `onAccessibilityEvent`**, or anywhere else on
  the main thread. Event lines go through a background sender.
- **Testing over ADB:** `uiautomator dump` briefly unbinds every
  accessibility service. The companion restarts after each dump and
  forgets its keyboard session, so use screenshots while testing typing.

## Set up the PC (once)

On Windows, from this folder:

```powershell
.\setup-tools.ps1
```

This installs Microsoft's OpenJDK 17 and the Android command-line SDK
(platform 35, build-tools 35.0.0, platform-tools with `adb`) under
`%LOCALAPPDATA%\Android`, about 750 MB.

- Downloads are fixed versions, checked against SHA-256 values pinned in
  the script.
- It accepts the Android SDK licenses.
- It leaves PATH alone.

To update the tools, edit the URLs and hashes in the script.

## Build

From this folder:

```powershell
.\build.ps1                     # the version in version.txt (0.6 -> versionCode 6)
.\build.ps1 -VersionName 0.7      # another version (-VersionCode to set the code)
```

It writes `build\companion.apk` and `build\companion.json`. The JSON holds
the version, the version code, and which key signed it. The script runs on
Windows, and on Linux in the GitHub workflow.

It runs `aapt2` (resources and manifest), then `javac --release 8` against
`android.jar`, then `d8`, `zipalign` and `apksigner`.

- `minSdk` is 26 and `targetSdk` is 34.
- **Versions.** Raise `version.txt` for every build the remote should
  offer as an update, because `/update` only installs a higher version
  code. Its format is `MAJOR.MINOR`, and the version code is
  MAJOR * 100 + MINOR. `adb install -r` takes the same code and keeps the
  app's data.
- **Tools.** On Windows they come from `%LOCALAPPDATA%\Android`
  (`setup-tools.ps1`); elsewhere from `JAVA_HOME` and `ANDROID_HOME`.
- **Signing.** With `HM_KEYSTORE`, `HM_KEYSTORE_PASS` and `HM_KEY_ALIAS`
  set, the build signs with that key. Without them, it uses
  `debug.keystore`, which the first build creates. Keep it. With a new
  key, Android refuses the update until the old app is uninstalled.
- **Signing in the workflow.** It sets those variables for `v*` tag builds
  only, from the repository secrets
  ([docs/releasing.md](../docs/releasing.md)). Its other builds sign with
  a throwaway key that `companion.json` marks as debug.

## Trying an update from the remote

Before a release, you can try an update from a site on the PC. Run these
from `firmware\remote`.

1. Build a test firmware that reads the site. It is a developer build in
   a scratch folder, without your secrets, because `make_site.py` refuses
   images that carry `secrets.env` values. In menuconfig, set
   **Hubmote > Updates** to `http://<pc-ip>:8000/`, where `<pc-ip>` is the
   PC's address:

   ```powershell
   .\scripts\hm-idf.ps1 -B build-test -D HM_NO_SECRETS=1 -D SDKCONFIG=build-test/sdkconfig menuconfig
   .\scripts\hm-idf.ps1 -B build-test -D HM_NO_SECRETS=1 -D SDKCONFIG=build-test/sdkconfig build
   ```

2. Build the companion under test as the installed version code, so ADB
   keeps its data, and the next one for the site.
3. Make a site with the firmware and the companion, and serve it to the
   network. `--any-key` lets it offer a debug-signed APK. Don't use
   `hm-web.ps1 -Serve` here, because it only answers this PC.

   ```powershell
   python tools\web\make_site.py --build build-test --out build-test\site --companion ..\..\companion\build --any-key
   python -m http.server 8000 --bind 0.0.0.0 --directory build-test\site
   ```

4. Put the test firmware on a remote:

   ```powershell
   .\scripts\hm-ota.ps1 -Image build-test\hubmote.bin
   ```

5. Afterwards, put the remote's normal build back.

> [!WARNING]
> Never leave a test companion installed above the real version.
