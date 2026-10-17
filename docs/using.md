# Using the remote

Two "Homes" appear below: **the Home tab** in the top bar (module cards),
and **Google TV's Home**, the app grid the remote shows while the TV is on
its Home screen.

## The top bar

- **The remote tab** (top left) shows the remote in use: the Google TV, the
  TV over IR, or a learned remote. Tap it for the **switcher**: your
  remotes (with Add a TV and Add a remote), modules, and Settings. From the
  Home tab or Settings, a tap goes back to the remote.
- **The Home tab** shows a card for each module; tap one to open it.
- In the middle, what's on: the app, *Typing*, a module.
- On the right: a module's glance (the weather), Wi-Fi, the battery, and
  the **gear** for Settings (tap it again to go back).
- **The battery** turns green with a bolt while charging and red below 30%.
  When it falls below 20% on battery, a "Battery low" card appears. `--%`
  means no cell is connected (or its switch is off).

## The Google TV

- **The rail** (left): Power, Back, Home, Keyboard, and volume with mute.
  Volume: a tap is one step, and holding is a real key hold that stops
  when you let go (over the TV's IR, a hold sends a short burst instead).
  The mute icon shows a crossed-out speaker while the TV reports it's
  muted.
- **The middle panel** follows the app on the TV:
  - **Google TV's Home:** your apps. Under them, **Search** (types into
    Google TV's search, with the companion app), **Inputs** (an HDMI switch
    over IR) and **Settings** (the Streamer's).
  - **An app:** play/pause, skip back and forward (the D-pad's left and
    right: about 10 s in most apps; holding keeps skipping, faster and
    faster), previous and next where the app has them, the title of
    what's playing where the app shares it, **Captions** and **Exit app**.
    **Wrong app?** shows Google TV's Home if the remote guessed the app
    wrong.
  - The screensaver, the Assistant and system panels don't count: the
    panel stays on the app underneath.
- **Arranging Google TV's Home:** tap **Edit**. Tap an app to hide or show
  it; press and hold to drag it somewhere else; **Done** when finished. New
  apps appear after the ones you placed.
- **The right side:** a **D-pad** (holding an arrow is a long press) or a
  **touchpad**: each swipe sticks to one direction, slow movement steps one
  item, a flick several, and holding still at the end keeps going. Tap for
  OK.

Without the [companion app](companion.md), Google TV's Home shows five
built-in apps instead of everything installed on the TV.

## The keyboard

It opens by itself when the TV asks for a keyboard (Google TV's own fields,
YouTube's search). Elsewhere, in browsers and apps with their own keyboard,
tap the rail's **Keyboard** key; the same key, Back or Home close it. It has
QWERTY, a symbols layer (`#+=`) and a number pad. Apps take text in
different ways, and the keyboard picks the one that works:

- **A text field** (one the TV announces, or one the companion sees, such
  as in a browser): what you type replaces the field's text, **Clear**
  empties it (the TV's field too with the companion; without it, the TV's
  field keeps its text), and the **Search** key (bottom right) is the
  field's own Enter. Number fields get a large number pad; passwords show as `*`. With
  the companion, the Streamer's own on-screen keyboard is hidden meanwhile.
- **An app's own keyboard** (Plex, with the companion): the companion
  presses its letters for you, about three a second.
- **Google TV's Home** (with the companion): *Search Google TV*. Pause
  typing after three or more letters (or tap Search) and Google TV's search
  opens with your text, with results from every app.
- **Anything else:** characters go to the app as keys. YouTube takes them;
  Netflix only while its keyboard has focus (move onto its letters first);
  Prime Video ignores them, so search from Google TV's Home.

## The TV and other devices (IR)

These need the IR receiver and transmitter ([hardware](hardware.md)). On a
board without free pins for IR they don't appear. With only a transmitter
(the 4.3), Add a TV works with its built-in codes, and learning is left
out.

- **The TV:** Add a TV (switcher, or Settings > Remotes). Pick the brand,
  then try its code sets: **Send**, then **It worked** or **Next set**. Or
  learn every key from the TV's own remote. Its panel has power, HDMI
  1-4, Source, channels, Back, Home, Menu, Settings, Info, Exit and volume.
  Dimmed keys have no code yet: tap one to learn it. **Keys** lists them
  all (arrows, OK and digits too); **Change** picks another set or removes
  the TV. The D-pad or touchpad next to it drives the TV, and its volume
  follows Settings > Remotes > TV volume. More in [TVs over IR](ir-tvs.md).
- **Learned remotes** (a soundbar, a fan...): Learn a remote (Settings >
  Remotes, or Add a remote in the switcher). Pick the kind of device (it
  becomes the remote's name; rename it later with Edit) and a button, press
  the original remote's button three times at the receiver, test, save. A
  learned remote is a grid of its buttons with **Learn a button**; **Edit**
  renames or deletes the remote, or a button you select (deleting asks
  twice).
- **Inputs:** the four inputs of an HDMI switch, from **Inputs** under
  Google TV's Home or *HDMI switch* in the switcher. Tap one to switch (or
  to learn it); **Edit** renames or learns them again.

On these screens the Streamer's rail is hidden, so its keys can't be
pressed by mistake.

## Modules

The Home tab has a card for each module. **Weather:** tap the place (or
*Set location*), type a city and pick it; the °C/°F switch is next to it.
It refreshes every 30 minutes while the screen is on. Settings > Modules
switches modules on and off.

## Settings

The gear (or Settings in the switcher). The chip at the top shows the
firmware version.

- **Remotes:** the Google TV (**Pair again**: pair with it anew; **Find
  TVs**: pick another), the companion app (**Pair companion** when it
  isn't paired yet, **Update companion** when a newer one can be
  installed), the TV and learned remotes over IR, TV volume (once a TV is
  added over IR: through Google TV or the TV's IR), Learn a remote.
- **Wi-Fi:** the network in use; Scan or Other network to switch. A new
  network is kept only once it connects; if it doesn't, restart the remote
  (Turn off remote, then touch the screen) to go back to the old one.
- **Screen:** brightness, **Rotate 180°**, **Turn off after** (1, 4, 12 or
  24 hours, or never), and when the screen dims and goes dark, on battery
  and while charging.
- **Appearance:** Midnight, Daylight, Black, Dusk or Terminal. Tap one,
  then **Restart to apply** ([themes](themes.md)).
- **Modules:** each one on or off.
- **About:** the version (**Check** for updates, **Install** a new one),
  the build time and update slot, the network address and name
  (`hubmote.local`), Wi-Fi signal, uptime, free memory, and in developer
  builds the update token (Show).
- **Turn off remote** and **Done**.

Boards that can't dim, or can't read their battery, leave those settings
out.

## Sleep and turning off

- After 30 s without a touch the screen dims, after 60 s it goes dark; on
  the charger after 5 and 30 minutes. Settings > Screen changes these. The
  first tap on a dark screen only wakes it.
- **Turn off remote** (Settings), or 12 hours without a touch, puts it in
  deep sleep. Touch the screen (or press BOOT) to wake it; it restarts and
  reconnects in about 10 seconds.
- Deep sleep still draws a little from the battery; a
  [power switch](hardware.md#power-switch) is the real off.
