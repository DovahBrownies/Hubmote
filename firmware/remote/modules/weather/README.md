# Weather

The Weather module shows the current weather, the next 24 hours and the
next 7 days for one place, from [Open-Meteo](https://open-meteo.com). It
needs no account or API key. This page covers what it shows, its
settings, when it refreshes, what it sends, and its code.

- **Screen:** the place, current temperature, condition, feels like,
  high/low, wind, humidity, UV and sunrise/sunset. Then the hourly
  forecast, which scrolls sideways, and 7 days with temperature ranges on
  the week's scale.
- **Home card:** now and high/low. Medium and Large (the default) add as
  many of the next 6 hours as fit; Small has no hours.
- **Glance:** an icon and the temperature, in the top bar and the
  switcher.

## Settings

Both settings are on the Weather screen:

- **Place:** tap the place name (or **Set location** the first time).
  Type a city, then pick it from the list.
- **Units:** the °C/°F switch. °F also switches wind to mph. The forecast
  is always fetched in metric and converted on screen, so switching is
  instant and needs no new request.

They are kept in NVS (`m_weather`: `name`, `region`, `lat`, `lon`,
`imperial`), so they survive restarts and firmware updates. The units are
also a module setting: **Settings > Modules > Weather > Units** changes the
same `imperial` value.

While the time zone in **Settings > Modules** is Automatic, the place's
UTC offset also sets the remote's local time (`hub_time`). The offset is
updated with each forecast, so a daylight-saving change applies at the
next refresh.

## Refresh

- While the screen is on: every 30 minutes.
- When the screen is opened or wakes: if the data is older than 10
  minutes.
- After an error: retries after 1, 2, 5, then every 15 minutes.
- Nothing while the screen is dark.

The last answer is cached in `/storage/weather/last.json`, and shown at
once after a restart. When a refresh fails, or the data is more than 2
hours old, the "Updated" time takes the accent color (`COL_ACCENT_TEXT`).

Loading and errors use the shared `ui_status` and `ui_spinner`
(`ui_kit.h`):

- The first forecast for a place shows a spinner with the place's name.
- Later refreshes keep the forecast on screen, with a small spinner next
  to "Updated".
- If the first load fails, the screen offers **Try again** and
  **Change place**.

## Data and privacy

- **Forecast:** `api.open-meteo.com` gets the place's coordinates (4
  decimals). Each answer is about 2 KB.
- **Place search:** `geocoding-api.open-meteo.com` gets the typed text.
- **Your IP address:** like any web service, Open-Meteo sees the home's
  public IP address.
- **Terms:** Open-Meteo's free API is for non-commercial use, and its data
  is under [CC BY 4.0](https://open-meteo.com/en/licence). The screen shows
  the required attribution, "Weather data by Open-Meteo.com".

## Code

| File | What it holds |
|---|---|
| `weather_data.c/.h` | URLs, JSON parsing, WMO weather codes (plain C, host-tested) |
| `weather.c/.h` | state, refresh timing, cache, settings, the module descriptor |
| `weather_ui.c` | the screen, the Home card and the glance |
| `test/` | host test with saved real answers (`run_test.py`) |

Icons are Lucide's weather icons: `ICON_W_*`, and `font_weather_96` for
the large one.

To run the host test, from `firmware/remote/` in PowerShell:

```powershell
python modules\weather\test\run_test.py --idf $env:USERPROFILE\esp\v5.4.4\esp-idf
```

`--idf` is your ESP-IDF folder.
[Testing](../../../../docs/modules.md#testing) in the module guide has
more.

## Author

The Hubmote project.
