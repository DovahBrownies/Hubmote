# Themes

Every color on the remote comes from the active theme, by role: "a panel",
"secondary text", "a filled button", never a fixed color. Changing the
theme changes the whole remote, modules included.

Pick one in **Settings > Appearance**: tap a theme's preview, then
**Restart to apply**. The remote restarts (about 10 s back to the TV) and
opens Appearance again. The choice is kept in NVS (`ctrl`, key `theme`).

## Built-in themes

| Theme | Look |
|---|---|
| Midnight | The original: near-black panels, amber for the Google TV, teal for IR |
| Daylight | Light: warm off-white and white panels, dark text. Bright amber for filled buttons, a darker amber for accent text |
| Black | High contrast: pure black, white text, brighter accents |
| Dusk | Indigo panels with a coral accent and sky-blue IR |
| Terminal | Green phosphor on near-black, yellow accent, square corners |

They're defined in `firmware/remote/components/ui/ui_themes.c`.

## Roles

Code uses these names (from `ui_kit.h`); each reads the active theme.

| Name | Role |
|---|---|
| `COL_BG` | Behind everything |
| `COL_SURFACE1` | Panels |
| `COL_SURFACE2` | Buttons and tiles on panels |
| `COL_SURFACE3` | Pressed or selected; tracks |
| `COL_LINE`, `COL_LINE2` | Dividers; outlines |
| `COL_TEXT`, `COL_TEXT2`, `COL_TEXT3` | Main, secondary and faint text and icons |
| `COL_ACCENT` | Fills: Done and other main buttons, Play, sliders, switches |
| `COL_ON_ACCENT` | Text and icons on `COL_ACCENT` |
| `COL_ACCENT_TEXT` | The accent as text, an icon, an outline or a spinner on a surface |
| `COL_IR`, `COL_ON_IR`, `COL_IR_LINE` | IR devices: icons and tags, text on an IR fill, IR outlines |
| `COL_GOOD`, `COL_BAD` | Charging; errors, low battery, Power |
| `COL_SUN`, `COL_SUN_SOFT`, `COL_RAIN`, `COL_STORM`, `COL_COOL`, `COL_WARM`, `COL_MOON` | Data colors (weather icons, temperature bars) |

Rules for screens and modules:

- **Use `COL_ACCENT` for fills and `COL_ACCENT_TEXT` for everything else
  in the accent color.** They're the same in dark themes; Daylight's
  bright amber is unreadable as text on white, so its `COL_ACCENT_TEXT` is
  darker. Text on a `COL_ACCENT` fill uses `COL_ON_ACCENT`.
- **Corners:** pass every radius through `ui_radius(r)` (`ui_panel` and
  `ui_btn` already do). Square themes shrink it; `LV_RADIUS_CIRCLE` stays
  round.
- **Pressed shades:** `ui_btn` handles them; elsewhere use `ui_pressed(bg)`
  (lighter in dark themes, darker in light ones).
- No `lv_color_hex()` in screens: a color that isn't a role won't follow
  the theme. If a module needs a color no role covers, propose a role.
- Colors are read when a screen is built, which is why a new theme applies
  with a restart.

## Adding a theme

1. Add an entry to `s_themes` in `ui_themes.c`, laid out like the others
   (the contrast check reads that layout): an `id` (lowercase, at most 15
   characters, stable: it's what NVS stores; a longer one silently falls
   back to Midnight), a `name` for Settings, `light` (dark text on light
   surfaces), `square` (small corners), and a color for every role.
2. Run the contrast check; fix anything it flags:

   ```powershell
   python firmware\remote\tools\check_themes.py
   ```

   It wants 4.5:1 for main and secondary text on every surface and for text
   on accent and IR fills, and 3:1 for faint text, accent text, IR and
   error colors.
3. Look at every screen at every board's size in the simulator, from
   `firmware/remote`: `python sim/run_sim.py --theme <id>`
   ([sim/README.md](../firmware/remote/sim/README.md)). Then build, install,
   pick it in Settings > Appearance, and check it on the remote:
   `tools/devtools.py shot` takes the screenshots.

Up to 8 themes show in Settings.

## Limits and plans

- **Fonts are the same in every theme.** An all-monospace Terminal would
  need IBM Plex Mono at every size the UI uses
  (roughly 0.5 MB of flash), so for now it keeps Sora.
- **Custom themes** as files on the remote (LittleFS or SD card) are
  planned; the colors and radius would come from a JSON file with the same
  roles.
- **Switching without a restart** would need every screen rebuilt and
  refilled with the remote's current state; a restart does exactly that.
