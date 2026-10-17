# Hubmote companion (Google TV Streamer)

A small Android TV app for the Streamer. It tells the remote which apps are
installed (with icons ready for its screen), opens any of them, reports
whether the focused field is text, a password or a number, types into that
field, and hides the TV's on-screen keyboard while the remote's is open. It
never reads a field's contents.

It is a single accessibility service with no screen of its own. The system
starts it at boot and keeps it running, and accessibility services may open
apps from the background.

**Installing, pairing and updating it:** [docs/companion.md](../docs/companion.md).
This file is about how it works: its versions, files, API and build.

Versions (0.6 is the first release; the API notes below say which
version added what):
- **0.1 to 0.3:** the app list with icons, launching, the focused field's
  type, typing into fields, hiding the TV's keyboard.
- **0.4:** pairing: only remotes that paired may use the API (see below);
  typing into apps' own on-screen keyboards; `/search` for Google TV's
  search.
- **0.5:** updates itself when the remote asks (`/update`, see below);
  `/status` adds the version name and whether updates can work.
- **0.6:** tells the remote what is playing (`/media`, from Android's media
  sessions; needs notification access, which `install.ps1` / `install.py`
  grant); a
  pairing lockout after wrong codes, and limits on requests.

| File | What it is |
|---|---|
| `src/io/github/dovahbrownies/hubmote/CompanionService.java` | The accessibility service: tracks the focused field, types into it, hides the TV's keyboard, launches apps, owns the server |
| `src/.../Server.java` | The HTTP server (one thread per connection) and the event stream |
| `src/.../Pairing.java` | Pairing codes and the paired remotes' tokens |
| `src/.../OnScreenKeyboard.java` | Finding an app's own on-screen keyboard and its keys |
| `src/.../Apps.java` | The TV app list, and icons rendered to RGB565A8 |
| `src/.../Updater.java` | Updating itself: download, checks, Android's package installer |
| `src/.../NowPlaying.java`, `src/.../MediaListener.java` | What is playing, from Android's media sessions; the listener only unlocks them (it reads no notifications) |
| `AndroidManifest.xml`, `res/` | Manifest (package `io.github.dovahbrownies.hubmote`), accessibility config, strings |
| `setup-tools.ps1` | Installs JDK 17 and the Android SDK on the PC (once) |
| `build.ps1`, `version.txt` | Builds `build\companion.apk` and `build\companion.json` (version, version code, which key signed it) without Gradle, on Windows or (in the GitHub workflow) Linux; the version comes from `version.txt` |
| `install.ps1`, `install.py` | Installs over ADB, enables the accessibility service, allows it to install apps (for updating itself) and grants its media listener notification access (what's playing); the Python one runs on any OS |
| `debug.keystore` | The signing key, created by the first build. **Keep it.** |

## Ways to install and update it

| Way | State |
|---|---|
| **Over ADB from a PC** (`install.ps1` / `install.py`, [docs/companion.md](../docs/companion.md)) | Works; every version so far went on this way. Updating this way keeps the pairing. |
| **From the remote:** Settings > Remotes > **Update companion** (0.5+) | Built; not yet seen on a Streamer that allows installs. Meant to install by itself where the companion may install apps (the install scripts allow it), otherwise Android asks on the TV. |
| **Downloader on the TV** (no PC, [the steps](../docs/companion.md#on-the-tv-without-a-pc)) | Not tried yet ([PLAN 9.3](../docs/PLAN.md#93-installing-the-companion-app)). |
| **A phone app** with Wireless debugging (e.g. Bugjaeger) | Not tried; the fallback in PLAN 9.3. |
| **The remote installing it** over Wireless debugging | Planned (PLAN 9.3), not built. |
| Google Play | Not published there. |

**TVs that block installs from apps.** Some Google TV setups have the
`no_install_unknown_sources` (or `no_install_unknown_sources_globally`)
restriction (the cause isn't known yet; `adb shell dumpsys user` lists it
for the main user): Unknown sources is greyed
out for every app, and Android's installer closes without showing
anything. There the companion reports `"updates":false`, the remote shows
the newer version without the button, the Downloader route can't work, and
updates go over ADB.

**Signing keys.** Android only installs an update signed with the same key
as the installed app. Release APKs use the project's key; your own builds
use `debug.keystore`. Moving between them means uninstalling, installing
and pairing again, once.

## API

Plain HTTP on port **6470** of the Streamer. Requests a web page could
make (with an `Origin` or `Sec-Fetch-*` header, or a `Host` that is a name
rather than an address) get `403`. More than 32 header lines get `431`. A
request not complete within 10 s, or silent for 5 s, is closed without an
answer. At most 24 connections are open at once, 8 per address.

**Pairing (0.4).** Every request except `/status` and `/pair*` needs
`Authorization: Bearer <token>`, or gets `401 {"error":"pairing needed"}`.
A remote gets its token once:

| Request | Answer |
|---|---|
| `POST /pair/start` | Shows a 6-character code (0-9, A-F) over whatever is on the TV, for 2 minutes: the one already showing, or a new one. `429` if no code is showing and the last one went up under 5 s ago. `423 {"error":"locked","retry":S}` after too many wrong codes (0.6+): wrong codes count across codes; the 6th locks pairing for 1 minute, and every 3rd after it for twice as long (2, 4, 8... minutes, at most 4 hours), until a remote pairs. The lock survives restarts. |
| `POST /pair?code=C` | `{"token":"<32 hex>"}` if C is the code on the TV (the code then disappears). `403 {"error":"wrong code","left":N}` if not; after 3 wrong codes it is taken off the TV. `410` if no code is showing. |
| `POST /pair/cancel` | Takes the code off the TV. |

- The code is drawn as an accessibility overlay
  (`TYPE_ACCESSIBILITY_OVERLAY`), which needs no extra permission.
- Tokens are kept in the app's private storage (up to 8 remotes; the oldest
  goes first). Updating the app keeps them; **Clear data** in Settings →
  Apps forgets them all, and also switches the accessibility service off
  (turn it on again in Settings → System → Accessibility).
- Tokens and codes are compared in constant time.

With a token:

| Request | Answer |
|---|---|
| `GET /status` | `{"version":6,"name":"0.6","updates":true,"media":true,"paired":true,"field":"none","id":0}`; without a token the same up to `paired` (false). `version` is the app's version code (0.4 sent 4, without `name`, `updates` and `media`); `updates` is false where the TV doesn't let apps install (below); `media` is false without notification access. |
| `GET /media` | What is playing: `{"pkg":"com.spotify.tv.android","title":"...","subtitle":"...","state":"playing","duration":215000,"position":41603}` (`state`: playing, paused, buffering, stopped, or none for any other state; times in ms), or `{"state":"none"}`. The session shown is the first one playing, else the first with a title. Titles lose their accents (the remote's fonts are ASCII) and stop at 90 characters. Apps decide what they share; on a Streamer (2026-10-16) YouTube sends the video and channel and Spotify the track, Netflix's session has no metadata, Prime Video's title is only "PrimeVideo", and Plex publishes no session. |
| `GET /apps` | `{"apps":[{"pkg":"com.netflix.ninja","label":"Netflix","updated":1759999999999}, ...]}`, sorted by label. `updated` changes when the app updates, so the remote can cache icons. |
| `GET /icon?pkg=P&size=N` | The icon, N × N (16–256), as LVGL 9 RGB565A8 bytes: RGB565 little-endian pixels, then one alpha byte per pixel. Corners are rounded like the remote's Home tiles (26 px at 112 px). |
| `POST /launch?pkg=P` | Opens the app's TV launch activity. |
| `POST /text?value=T` | Replaces the focused field's text with T (percent-encoded), using accessibility `ACTION_SET_TEXT`. Works where the TV's input method never reaches the remote protocol, such as in browsers. Without a text field, types T into the app's own on-screen keyboard (below). 404 if there is neither. |
| `POST /search?q=Q` | Opens Google TV's own search with Q (`SearchManager.INTENT_ACTION_GLOBAL_SEARCH`): Gemini's answer or a title's page, with results from every app. The remote uses it for typing on Home. |
| `POST /enter` | The focused field's Enter / Go / Search (`ACTION_IME_ENTER`, Android 11+), or the on-screen keyboard's Search / Enter key (without one it presses nothing and answers 200: such keyboards search as you type). |
| `POST /keyboard?hidden=1\|0` | Hides (1) or allows (0) the TV's on-screen keyboard (the soft keyboard controller's `SHOW_MODE_HIDDEN`, then `SHOW_MODE_AUTO`). It is allowed again by itself when no remote is connected to `/events`. |
| `POST /update?url=U&size=N&sha256=H` | Updates the companion from the APK at U (http or https, N bytes, that SHA-256 in hex), in the background (below). `400` if the request is unusable or the TV doesn't let apps install. |
| `GET /events` | Kept open. One JSON line per change, `{"field":"none\|text\|password\|number","id":N}`, starting with the current state, and an empty line every 15 s. `id` changes whenever a different field gets focus. While updating: `{"update":"downloading\|installing\|confirm\|failed"}`. When the title or playback state changes: `{"media":{...}}`, as `/media` without the position. |

- **Apps listed:** those with a TV launcher entry
  (`CATEGORY_LEANBACK_LAUNCHER`).
- **Icons:** adaptive icons are drawn full-bleed and then rounded; other
  icons are drawn on white.
- **Field type** comes from the input-focused node's `inputType`: number or
  phone gives `number`; the password variations give `password`.
- **The focused field** is found across all windows (`findFocus`, with
  `flagRetrieveInteractiveWindows`), because the TV's keyboard window is often
  the active one.
- **On-screen keyboards** (`OnScreenKeyboard.java`, 0.4+). Some apps draw a
  grid of letter keys instead of a text field (Plex). With no rules for
  particular apps, a key is anything actionable labelled with one letter or
  digit, or named space, backspace, clear or search/enter by its label or
  view id (`flagReportViewIds`); a screen with keys for 20 or more letters
  counts as a keyboard, and is reported as a `text` field.
  - `/text` works out the difference to what it typed before (Backspace for
    the end, Clear when nothing stays), starts each new keyboard with Clear,
    and presses keys the way a TV user does: focus on the key, then the
    D-pad's OK (`GLOBAL_ACTION_DPAD_CENTER`, Android 13+). Plex ignores an
    accessibility click on its keys; keys that can't take focus, and every
    key before Android 13, get an accessibility click.
  - Timing: 120 ms from focus to OK, 150 ms between keys, 500 ms before the
    same key again (Plex drops quicker repeats, even from a real remote).
    Requests that pile up skip to the newest text.
  - Apps that draw their whole interface as one surface (Netflix, YouTube,
    Prime Video) expose no keys; the remote sends those key presses.
  - Checked on the Streamer (2026-10-15): Plex (type, edit with Backspace,
    repeated letters, Clear). The Play Store and Spotify have real text
    fields.
- **Updating itself** (`Updater.java`, 0.5+). The remote reads the
  release's companion from `update.json` and, on Settings > Remotes >
  **Update companion**, sends its address, size and SHA-256 to `/update`.
  The companion downloads it (into its cache), checks the size, the hash,
  that it is this app and a newer version, and installs it with a
  `PackageInstaller` session. Android only accepts it if the same key signed
  it, so the companion can't install anything else. Then Android stops the
  app and starts the new version; the remote sees it in `/status`.
  - **Without asking** (Android 12+, `UPDATE_PACKAGES_WITHOUT_USER_ACTION`):
    only while the companion may also "install unknown apps"; `install.ps1`
    allows it over ADB (`appops set ... REQUEST_INSTALL_PACKAGES allow`).
  - **Otherwise** Android shows its install dialog on the TV ("confirm"
    on the remote); confirm it with the remote's D-pad. Tapping Update
    companion again shows it again.
  - **Not at all** where the system forbids installs from apps
    (`no_install_unknown_sources`, above). The companion then reports
    `"updates":false`, the remote shows the newer version without the
    button, and updates go over ADB (`install.ps1`).
  - Cleartext is allowed (`usesCleartextTraffic`) so an update can come from
    a PC on the home network while testing; the hash and Android's key
    check guard it.
- **Testing over ADB:** `uiautomator dump` briefly unbinds every
  accessibility service, so the companion restarts after each dump (and
  forgets its keyboard session); use screenshots while testing typing.
- **Never do network I/O in `onAccessibilityEvent`** or anywhere else on the
  main thread. Event lines go through a background sender.

**Security.** Only paired remotes can call it, and pairing needs someone to
read the code off the TV screen. The connection is still plain HTTP: a
device that can read the home network's traffic could see the token and the
text typed through the remote. A paired remote can make it install an
update of itself, never another app. More in
[docs/security.md](../docs/security.md).

## Set up the PC (once)

```powershell
.\setup-tools.ps1
```

This installs Microsoft's OpenJDK 17 and the Android command-line SDK
(platform 35, build-tools 35.0.0, platform-tools with `adb`) under
`%LOCALAPPDATA%\Android`, about 750 MB:
- downloads are fixed versions, checked against SHA-256 values pinned in
  the script;
- the Android SDK licences are accepted;
- PATH is left alone.

To update the tools, edit the URLs and hashes in the script.

## Build

```powershell
.\build.ps1                     # the version in version.txt (0.6 -> versionCode 6)
.\build.ps1 -VersionName 0.7      # another version (-VersionCode to set the code)
```

It runs `aapt2` (resources and manifest), then `javac --release 8` against
`android.jar`, then `d8`, `zipalign` and `apksigner`.
- `minSdk` is 26 and `targetSdk` is 34.
- Raise `version.txt` (`MAJOR.MINOR`; the version code is MAJOR * 100 +
  MINOR) for every build the remote should offer as an update (`/update`
  only installs a higher version code); `adb install -r` takes the same
  code and keeps the app's data.
- Tools: on Windows from `%LOCALAPPDATA%\Android` (`setup-tools.ps1`);
  elsewhere from `JAVA_HOME` and `ANDROID_HOME`.
- Signing: `HM_KEYSTORE`, `HM_KEYSTORE_PASS` and `HM_KEY_ALIAS` (the
  workflow sets them for `v*` tag builds only, from the repository
  secrets, [docs/releasing.md](../docs/releasing.md); its other builds
  sign with a throwaway key that `companion.json` marks as debug);
  otherwise `debug.keystore`, which the first build creates. Keep it: with a
  new key, Android refuses the update until the old app is uninstalled.

## Trying an update from the remote

Before a release, from `firmware\remote`. A test firmware that reads a
site on the PC: a developer build in a scratch folder, without your
secrets (`make_site.py` refuses images that carry `secrets.env` values),
with menuconfig > Hubmote > Updates set to `http://<pc-ip>:8000/`:

```powershell
.\scripts\hm-idf.ps1 -B build-test -D HM_NO_SECRETS=1 -D SDKCONFIG=build-test/sdkconfig menuconfig
.\scripts\hm-idf.ps1 -B build-test -D HM_NO_SECRETS=1 -D SDKCONFIG=build-test/sdkconfig build
```

Then a site with it and the companion (`--any-key` offers a debug-signed
APK), served to the network (`hm-web.ps1 -Serve` only answers this PC):

```powershell
python tools\web\make_site.py --build build-test --out build-test\site --companion ..\..\companion\build --any-key
python -m http.server 8000 --bind 0.0.0.0 --directory build-test\site
```

Put the test firmware on a remote (`hm-ota.ps1 -Image
build-test\hubmote.bin`), and its normal build back afterwards. Build the
companion under test as the installed version code (so ADB keeps its data)
and the next one for the site; never leave a test companion installed
above the real version.
