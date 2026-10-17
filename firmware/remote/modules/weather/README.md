# Weather

Current weather, the next 24 hours and the next 7 days for one place, from
[Open-Meteo](https://open-meteo.com). No account or API key.

- **Screen:** the place, current temperature, condition, feels like,
  high/low, wind, humidity, UV and sunrise/sunset; hourly forecast (scrolls
  sideways); 7 days with temperature ranges on the week's scale.
- **Home card:** now, high/low and the next 6 hours.
- **Glance:** an icon and the temperature in the top bar and the switcher.

## Settings

On the Weather screen:

- **Place:** tap the place name (or *Set location* the first time), type a
  city and pick it from the list.
- **Units:** the °C/°F switch. °F also switches wind to mph. The forecast
  is always fetched in metric and converted on screen, so switching is
  instant and needs no new request.

Kept in NVS (`m_weather`: `name`, `region`, `lat`, `lon`, `imperial`), so
they survive restarts and firmware updates.

The place's UTC offset (updated with each forecast, so a daylight-saving
change applies at the next refresh) also sets the remote's local time
(`hub_time`), since the remote has no time-zone setting of its own yet.

## Refresh

- While the screen is on: every 30 minutes.
- When the screen is opened or wakes: if the data is older than 10 minutes.
- After an error: retries after 1, 2, 5, then every 15 minutes.
- Nothing while the screen is dark.

The last answer is cached in `/storage/weather/last.json` and shown at once
after a restart. When a refresh fails, or the data is more than 2 hours old,
the "Updated" time takes the accent color (`COL_ACCENT_TEXT`).

While loading: the first forecast for a place shows a spinner with the
place's name; later refreshes keep the forecast on screen with a small
spinner next to "Updated". If the first load fails, the screen offers
*Try again* and *Change place*. These use the shared `ui_status` and
`ui_spinner` (`ui_kit.h`).

## Data and privacy

- **Forecast:** `api.open-meteo.com` gets the place's coordinates (4
  decimals). About 2 KB per answer.
- **Place search:** `geocoding-api.open-meteo.com` gets the typed text.
- Like any web service, Open-Meteo sees the home's public IP address.
- **Terms:** Open-Meteo's free API is for non-commercial use, and its data
  is under [CC BY 4.0](https://open-meteo.com/en/licence). The screen shows
  the required attribution, "Weather data by Open-Meteo.com".

## Code

| File | What |
|---|---|
| `weather_data.c/.h` | URLs, JSON parsing, WMO weather codes (plain C, host-tested) |
| `weather.c/.h` | state, refresh timing, cache, settings, the module descriptor |
| `weather_ui.c` | the screen, the Home card and the glance |
| `test/` | host test with saved real answers (`run_test.py`) |

Icons are Lucide's weather icons (`ICON_W_*`, `font_weather_96` for the large
one).

```powershell
python modules\weather\test\run_test.py --idf $env:USERPROFILE\esp\v5.4.4\esp-idf
```

## Author

The Hubmote project.
