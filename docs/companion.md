# The companion app

This page is for anyone who wants the companion app: what it does, how to
install and pair it, how to update it and how to fix problems.

The companion is a small, optional Android app for the Google TV
Streamer. With it, the remote:

- shows every app installed on the TV, with its own icon, and opens them;
- types into any text field, browsers included, and into apps' own
  on-screen keyboards, such as Plex's;
- searches Google TV with what you type on Google TV's Home;
- hides the Streamer's on-screen keyboard while the remote's is open;
- shows the title of what's playing, where the app shares it.

The companion has no screen of its own. It runs in the background as an
*accessibility service*, Android's way of letting a helper app work other
apps for you. It never reads what's in a text field, and it only takes
commands from a remote that has paired with it.

For developers, how it works and its API are in
[companion/README.md](../companion/README.md).

On this page:

- [Install it](#install-it), [from a PC](#from-a-pc) or
  [on the TV](#on-the-tv-without-a-pc)
- [Updates](#updates)
- [Uninstall](#uninstall)
- [Problems](#problems)

## Install it

You only install it once. There are two ways:

| | From a PC | On the TV |
|---|---|---|
| Uses | Wireless debugging | The Downloader app |
| You need | A PC on the same network | Only the TV |
| Tested | Yes, every install so far | Not tried yet |
| Works on | Any Streamer with Developer options | Only TVs that allow it |
| What's playing | Yes | Only if the TV offers notification access |

If you have a PC, use it. It always switches everything on.

### From a PC

This way uses two Android tools:

- **ADB** (Android Debug Bridge) is a program for your PC. It installs
  apps on Android devices and changes their settings.
- **Wireless debugging** is a setting on the Streamer that lets ADB reach
  it over your home network. It only works while the Streamer is on
  Wi-Fi.

You type the commands below in **PowerShell** on Windows, or in a
**terminal** on macOS or Linux. Type or paste each one, then press Enter.

Before you start, you need:

- **The PC on the same network** as the Streamer.
- **A copy of the Hubmote repository**, the project's files from
  [GitHub](https://github.com/DovahBrownies/Hubmote). Every command below
  runs from its folder, so open PowerShell or the terminal there.
- **Python 3**, on macOS or Linux.

On Windows, PowerShell blocks scripts until you allow them. Run this once:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

1. **Install ADB on the PC.**
   - On Windows, run the command below. It downloads ADB with Java and the
     Android SDK, about 750 MB in all, into `%LOCALAPPDATA%\Android`. When
     it finishes, it lists the tools it installed, each followed by
     `True`.

     ```powershell
     .\companion\setup-tools.ps1
     ```

   - On macOS or Linux, install Android's platform-tools.
2. **Download the app.** Get `companion.apk` from the
   [installer page](https://dovahbrownies.github.io/Hubmote/). Developers
   can also build it ([companion/README.md](../companion/README.md)).
3. **Turn on Developer options on the Streamer.** Developer options is a
   hidden settings menu. To show it, open **Settings > System > About**
   and press OK on **Android TV OS build** seven times.
4. **Put the Streamer on Wi-Fi.** Wireless debugging switches itself off
   when the Streamer uses Ethernet (a network cable). Unplug the cable and
   join Wi-Fi for now.
5. **Turn on Wireless debugging.** Open **Settings > System > Developer
   options > Wireless debugging** and switch it on. Its screen shows the
   TV's **IP address & port**. This is the *connect address*. Note it
   down for step 7.
6. **Pair the PC with the Streamer.** On the same screen, select **Pair
   device with pairing code**. The TV shows a code, and an IP address and
   port for pairing. The code expires, so run this on the PC straight
   away:

   ```sh
   adb pair <ip>:<pairing port> <code>
   ```

   On Windows, type ADB's full path in place of `adb`:

   ```powershell
   & "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" pair <ip>:<pairing port> <code>
   ```

   Replace `<ip>:<pairing port>` and `<code>` with what the pairing
   window shows. ADB says when the pairing worked.
7. **Install the companion and switch it on.** Use the connect address
   from step 5 here, not the pairing port. On Windows:

   ```powershell
   .\companion\install.ps1 -Device <ip>:<port> -Apk <path>
   ```

   On macOS or Linux, or on Windows with Python 3:

   ```sh
   python companion/install.py --device <ip>:<port> --apk <path>
   ```

   Replace `<ip>:<port>` with the connect address, and `<path>` with
   where you saved `companion.apk`. The script installs the app and turns
   its accessibility service on. It also gives it notification access, so
   it can show what's playing, and lets it update itself later. When it's
   done, its last line starts with `installed;`.
8. **Pair it with the remote.** On the remote, open **Settings > Remotes**
   and tap **Pair companion**. The button appears once the remote has
   found the companion. The TV shows a code for 2 minutes. Type it on the
   remote. You only need to do this once.

   Once it's paired, the line under the TV's name reads **companion app
   on**, or shows the companion's version.
9. **Tidy up.** Turn Wireless debugging off and plug the Ethernet cable
   back in. The remote follows the TV's change of address by itself.

### On the TV, without a PC

> [!NOTE]
> Nobody has tried this way yet. If you try it, please say how it went in
> [issue #17](https://github.com/DovahBrownies/Hubmote/issues/17).

This way only works on TVs that let apps install other apps, and that let
you switch on the companion's accessibility service. If a step below is
blocked, use [the PC](#from-a-pc) instead.

1. **Install Downloader.** On the Streamer, install
   [Downloader](https://play.google.com/store/apps/details?id=com.esaba.downloader)
   (by AFTVnews) from Google Play.
2. **Let Downloader install apps.** Open **Settings > Apps > Security &
   restrictions > Unknown sources** and switch **Downloader** on.

   If that page is greyed out, the TV blocks installs from apps. Use the
   PC instead.
3. **Download and install the companion.** In Downloader, open this
   address, then install the app:

   ```text
   https://dovahbrownies.github.io/Hubmote/companion.apk
   ```

4. **Switch it on.** Open **Settings > System > Accessibility > Hubmote
   companion** and switch it on.

   If Android says this is a restricted setting, first allow restricted
   settings in the app's info, at **Settings > Apps > Hubmote Companion**,
   if the TV offers that. If it doesn't, use the PC.
5. **Let it update itself later.** On the same **Unknown sources** page as
   in step 2, switch **Hubmote Companion** on too.
6. **Pair it with the remote.** On the remote, open **Settings > Remotes**,
   tap **Pair companion**, and type the code the TV shows.

Showing what's playing also needs notification access for the companion,
and the TV's settings may not offer it. Everything else works without it.

## Updates

The remote looks for a newer companion when it checks for new firmware
(the remote's own software). It checks a minute after it joins Wi-Fi,
then once a day, and when you tap **Check** in **Settings > About**.

When there is a newer companion:

1. Open **Settings > Remotes**. It shows both versions and an **Update
   companion** button.
2. Tap **Update companion**. The companion downloads the new version and
   installs it. Where the companion may install apps, it does this by
   itself.
3. Otherwise, Android shows an install dialog on the TV. Confirm it with
   the remote's D-pad (its arrow and OK keys).

Some Google TV setups don't let apps install anything. On those, the page
**Settings > Apps > Security & restrictions > Unknown sources** is greyed
out, and the **Update companion** button doesn't appear. To update there,
repeat steps 4 to 7 of [From a PC](#from-a-pc) with the new APK.

Android only accepts an update signed with the same key as the installed
app. So a companion you built yourself can't take a release update. To
switch, uninstall it, install the release APK, and pair it again.

## Uninstall

To remove the companion, use **Settings > Apps** on the Streamer. Or, with
ADB set up as in [From a PC](#from-a-pc), run this on the PC:

```sh
adb uninstall io.github.dovahbrownies.hubmote
```

To make it forget every paired remote:

1. On the Streamer, open **Settings > Apps > Hubmote Companion** and
   select **Clear data**. This also switches the companion off.
2. Switch it on again in **Settings > System > Accessibility**. Or repeat
   steps 4 to 7 of [From a PC](#from-a-pc), which also brings back what's
   playing.
3. Pair it with the remote again ([step 8](#from-a-pc)).

## Problems

Some fixes below use ADB on the PC. Set it up with steps 1 and 3 to 6 of
[From a PC](#from-a-pc), and keep Wireless debugging on. Type the `adb`
commands in PowerShell or a terminal. On Windows, replace `adb` with
ADB's full path, as in step 6.

If ADB finds no device, connect it first, with the connect address from
step 5:

```sh
adb connect <ip>:<port>
```

### Google TV's Home shows only 5 apps, or "companion app not paired"

The companion isn't installed, paired or reachable.

1. Pair it with the remote ([step 8](#from-a-pc)).
2. Check that the remote can reach it. On any device on the same network,
   open this address in a web browser:

   ```text
   http://<tv-ip>:6470/status
   ```

   Replace `<tv-ip>` with the TV's address, from the Streamer's
   **Settings > Network & Internet**. The page should show a short line of
   text. If it doesn't, see the next problem.

### The status page doesn't answer

The companion's accessibility service may be off. On the Streamer, open
**Settings > System > Accessibility** and switch **Hubmote companion** on.
Or run the install again ([step 7](#from-a-pc)), which switches it on too.

### The companion worked, then stopped answering

The companion crashed. Android may wait up to 30 minutes before it
restarts it.

1. To restart it at once, install it again ([step 7](#from-a-pc)).
2. To see why it crashed, run this on the PC:

   ```sh
   adb logcat -d -s AndroidRuntime:E
   ```

### No code shows on the TV

If a code went up moments ago and was then used or cancelled, the remote
says "Try again in a few seconds".

1. Wait a few seconds, then tap **Pair companion** again.
2. If the remote didn't say that, run this on the PC to see why:

   ```sh
   adb logcat -s HubmoteCompanion
   ```

   It keeps showing new lines until you press Ctrl+C.

### "Too many wrong codes. Try again in N min"

After too many wrong codes, the companion stops pairing for a while, so
nobody can guess a code. Each new lockout lasts longer, up to 4 hours.

1. Wait for the minutes the remote shows.
2. Tap **Pair companion** again and type the new code the TV shows.

### "The companion app didn't answer"

The remote couldn't reach the companion when it asked for a code.

1. Check that the TV is on and the companion answers
   ([The status page doesn't answer](#the-status-page-doesnt-answer)).
2. Tap **Pair companion** again.

### Wireless debugging keeps switching off

The Streamer is on Ethernet, and Wireless debugging only stays on over
Wi-Fi. Unplug the network cable and connect the Streamer to Wi-Fi for the
install ([step 4](#from-a-pc)).

### The install says `INSTALL_FAILED_UPDATE_INCOMPATIBLE`

The companion on the TV was signed with another key, for example one you
built yourself.

1. Uninstall it ([Uninstall](#uninstall)).
2. Run the install again ([step 7](#from-a-pc)).
3. Pair it with the remote again ([step 8](#from-a-pc)).

### The remote says "companion update didn't work"

It says this under the TV's name in **Settings > Remotes**. Cancelling
Android's install dialog on the TV ends up here too.

1. Tap **Update companion** to try again.
2. To see why it failed, run this on the PC:

   ```sh
   adb logcat -s HubmoteCompanion
   ```
