# Screen time

Counts how long the TV is on each day, and in which apps, and can hold it
to a daily limit.

- **Its screen:** today's time, the time left when there is a limit, and
  the apps, most watched first (up to 6; past 7 apps a day, the rest
  count as **Other apps**). App names come from the companion app's list;
  without it, the package shows.
- **Its card** on the Modules tab: today's time and the time left.
- **Near the limit,** the top bar shows the minutes left, then
  **Time's up**. That goes when the TV is turned on again, at midnight,
  with **+ 15 min**, or when the limit is raised or switched off.
- **A day** starts at local midnight (the time zone in Settings >
  Modules). Today's count survives restarts and updates: it is saved
  every 5 counted minutes and when the TV turns off.

## The limit

With **Daily limit** set:

1. **Warn before the limit** minutes before it, a message says how many
   minutes are left.
2. At the limit, a message says screen time is up for today.
3. A minute later the TV turns off, unless **+ 15 min** was tapped in
   between. Each time the TV comes back on that day, the message and the
   minute come again. If the Google TV is still on a minute after that,
   it is put to sleep again, every minute.

With **At the limit** on **Only warn**, the messages come and the TV
stays on.

Messages show on the remote and, with **Show messages on the TV** and
the companion app 0.7 or later, at the top right of the TV for 10
seconds (`hub_notify()`). When they can't reach the TV, the remote's
screen lights up to show them.

**+ 15 min** on its screen adds time for today only. Anyone with the
remote can tap it, and change the limit in Settings: it's a reminder,
not a lock.

## How it counts

At each of the remote's minute ticks, it adds a minute if:

- **Count while: TV is on** (the default): the Google TV is on
  (`hub_tv_state()`), Home screen and screensaver included. This needs
  no companion app.
- **Count while: Playing:** the companion app (0.6 or later) reports
  something playing, not paused.

It only counts while the remote is on and connected to the Google TV.
With a limit set, the remote doesn't turn itself off after idle hours
while the TV is on. Turning the remote off from Settings stops the
count until it is back on.

## Settings

In **Settings > Modules > Screen time**, kept in NVS (`m_screen_time`):

- **Daily limit** (`limit`): **Off** (the default), or 15 minutes to 3
  hours in 15-minute steps, then up to 10 hours. Developer builds also
  offer 1 minute, for quick tests.
- **At the limit** (`at_limit`): **Turn TV off** (the default) or **Only
  warn**.
- **Turn off with** (`off_ir`): **Google TV** puts the Streamer to sleep,
  and the TV turns off with it over HDMI-CEC (the default). **TV's IR**
  sends the IR TV's off key, or its power key if it has no separate one,
  and puts the Streamer to sleep too, since the count follows it.
- **Warn before the limit** (`warn`): 5 to 30 minutes, or **Off**; 10 by
  default.
- **Count while** (`count`): **TV is on** or **Playing**.
- **Show messages on the TV** (`tv_msg`): on by default.

Today's count is kept in the same namespace: `day` (yyyymmdd), `used`
and `extra` (minutes), `apps` (`package=minutes;` for each app).

## Data and privacy

It uses no network of its own. It stores today's minutes per app on the
remote, and starts over each day: no history. Its messages go to the
companion app on the TV, on your network.

## Credits and licenses

Nothing beyond the remote's own: the hourglass icon is Lucide's, in the
icon fonts (NOTICE).

## Author

the Hubmote authors
