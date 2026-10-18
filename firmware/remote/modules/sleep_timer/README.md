# Sleep timer

Turns the TV off after 15 to 120 minutes.

- **Its screen:** tap **15 min** to **120 min** to start it. While it runs,
  **+ 15 min** adds time and **Cancel** stops it. When the clock is set,
  it shows the time the TV turns off.
- **While it runs,** the top bar counts down, and the remote doesn't turn
  itself off after idle hours (Settings > Screen), so the timer can finish.
  Turning the remote off from Settings stops it.
- **A minute before,** a message says the TV is about to turn off.

## Settings

In **Settings > Modules > Sleep timer**, kept in NVS (`m_sleep_timer`):

- **Turn off with** (`off_ir`): **Google TV** puts the Streamer to sleep,
  and the TV turns off with it over HDMI-CEC (the default). **TV's IR**
  sends the IR TV's off key, or its power key if it has no separate one;
  that one turns a TV that is already off back on. Without an IR TV, the
  Streamer is used.
- **Warn a minute before** (`warn`): on by default.

## Data and privacy

It uses no network of its own and stores only these two settings.

## Credits and licenses

Nothing beyond the remote's own: the moon icon is Lucide's, in the icon
fonts (NOTICE).

## Author

the Hubmote authors
