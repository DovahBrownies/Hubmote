# The companion app

A small, optional Android app for the Google TV Streamer. With it the
remote:

- shows every app installed on the TV, with its own icon, and opens them;
- types into any text field, browsers included, and into apps' own
  on-screen keyboards (Plex);
- searches Google TV with what you type on Google TV's Home;
- hides the Streamer's on-screen keyboard while the remote's is open;
- shows the title of what's playing, where the app shares it.

It runs in the background as an accessibility service, with no screen of
its own. It never reads what's in a field, and only takes commands from a
remote that has paired with it. How it works and its API:
[companion/README.md](../companion/README.md).

## Install it

Two ways, once:

| | From a PC (Wireless debugging) | On the TV (Downloader) |
|---|---|---|
| Needs | A PC on the same network with Python 3 and `adb`; the Streamer on Wi-Fi during the install | Only the TV and the Downloader app |
| State | Tested: every install so far | Not tried yet |
| Works on | Any Streamer with Developer options | Only TVs that let apps install other apps, and let you switch on the companion's accessibility service |
| What's playing | Yes (the install grants notification access) | Only if the TV's settings offer notification access |

If you have a PC, use it: it always switches everything on.

### From a PC

The commands run from the repository's folder. On Windows, PowerShell
blocks scripts until you allow them once:
`Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

1. **Get the PC tools.** On Windows, `.\companion\setup-tools.ps1` installs
   `adb` (with Java and the Android SDK, about 750 MB, under
   `%LOCALAPPDATA%\Android`). On macOS or Linux, install Android's
   platform-tools.
2. **Get the app:** `companion.apk` from the
   [installer page](https://dovahbrownies.github.io/Hubmote/) (or build it:
   [companion/README.md](../companion/README.md)).
3. **Developer options on the Streamer:** Settings > System > About, press
   OK on **Android TV OS build** seven times.
4. **The Streamer on Wi-Fi:** Wireless debugging switches itself off on
   Ethernet, so unplug the cable and join Wi-Fi for now.
5. **Wireless debugging:** Settings > System > Developer options >
   **Wireless debugging**: switch it on. Its screen shows the TV's
   **IP address & port** (the connect address). Then **Pair device with
   pairing code**, and straight away on the PC (the code expires):
   ```
   adb pair <ip>:<pairing port> <code>
   ```
   On Windows, `adb` is
   `& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"`.
6. **Install and switch it on**, with the connect address from step 5:
   ```
   python companion/install.py --device <ip>:<port> --apk <path to companion.apk>
   ```
   This installs the app, turns its accessibility service on, lets it see
   what's playing, and lets it update itself later.
   (`.\companion\install.ps1 -Device <ip>:<port> -Apk <path>` does the
   same on Windows.)
7. **Pair it with the remote:** Settings > Remotes > **Pair companion** (the
   button appears once the remote has found the companion). The TV shows a
   code for 2 minutes; type it on the remote. Needed once.
8. **Tidy up:** turn Wireless debugging off and plug the Ethernet back in.
   The remote follows the TV's address change by itself.

### On the TV, without a PC

Not tried yet: if you try it, please say how it went in
[issue #17](https://github.com/DovahBrownies/Hubmote/issues/17).

1. **Install [Downloader](https://play.google.com/store/apps/details?id=com.esaba.downloader)**
   (by AFTVnews) from Google Play on the Streamer.
2. **Let it install apps:** Settings > Apps > Security & restrictions >
   Unknown sources > **Downloader** on. If that page is greyed out, the TV
   blocks installs from apps: use the PC.
3. **Download and install the companion:** in Downloader, open
   `https://dovahbrownies.github.io/Hubmote/companion.apk` and install it.
4. **Switch it on:** Settings > System > Accessibility > **Hubmote
   companion**. If Android calls this a restricted setting, allow
   restricted settings in the app's info first (Settings > Apps > Hubmote
   Companion), where the TV offers it; if it doesn't, use the PC.
5. **Let it update itself later:** on the same Unknown sources page,
   switch **Hubmote Companion** on too.
6. **Pair it with the remote:** Settings > Remotes > **Pair companion**,
   and type the code the TV shows.

The title of what's playing also needs notification access for the
companion, which the TV's settings may not offer; everything else works
without it.

## Updates

The remote looks for a newer companion with its firmware check (a minute
after it joins Wi-Fi, then daily, or **Check** in Settings > About). When
there is one, Settings > Remotes shows both versions and **Update
companion**: the companion downloads it and installs it, by itself where it
may install apps, otherwise after you confirm Android's dialog on the TV
with the D-pad. Some Google TV setups forbid apps from installing anything
(Settings > Apps > Security & restrictions > Unknown sources is greyed
out); there the button doesn't appear, and you update by repeating steps
4-6 above with the new APK.

Android only accepts an update signed like the installed app. A companion
you built yourself can't take a release update: uninstall it, install the
release APK, and pair again.

## Uninstall

`adb uninstall io.github.dovahbrownies.hubmote`, or Settings > Apps on the
Streamer. To make it forget every paired remote: Settings > Apps > Hubmote
Companion > **Clear data**, then switch it on again (Settings > System >
Accessibility, or steps 4-6 above, which also restore what's playing), and
pair again.

## Problems

| Symptom | Check |
|---|---|
| Google TV's Home shows only 5 apps, or "companion app not paired" | Pair it (step 7). Is it reachable? `http://<tv-ip>:6470/status` in a browser should answer (the TV's address is in its Settings > Network & Internet). |
| `/status` doesn't answer | Is the service on? Settings > System > Accessibility, or run step 6 again. |
| It worked, then stopped answering | It crashed: `adb logcat -d -s AndroidRuntime:E`. Android may wait up to 30 minutes before restarting it; installing it again restarts it at once. |
| No code on the TV | The remote says "Try again in a few seconds" when a code went up moments ago and was used or cancelled: wait, then tap Pair companion again. Otherwise `adb logcat -s HubmoteCompanion` says why. |
| Wireless debugging keeps switching off | The Streamer is on Ethernet: switch to Wi-Fi for the install. |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | It was signed with another key: uninstall it first. |
| "companion update didn't work" | `adb logcat -s HubmoteCompanion` says why; cancelling Android's install dialog ends up here too. |
