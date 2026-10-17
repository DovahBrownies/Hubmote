# Themes

This page is for developers and contributors. It covers the built-in
themes, the color roles that screens and modules use, and how to add a
theme.

Every color on the remote comes from the active theme, by role: "a panel",
"secondary text", "a filled button", never a fixed color. Changing the
theme changes the whole remote, modules included.

To pick a theme, open **Settings > Appearance**, tap a theme's preview,
then tap **Restart to apply**. The remote restarts, which takes about 10 s
to get back to the TV, and opens Appearance again. The choice is kept in
NVS (namespace `ctrl`, key `theme`).

## Built-in themes

| Theme | Look |
|---|---|
| Midnight | The original. Near-black panels, amber for the Google TV, teal for IR |
| Daylight | Light. Warm off-white and white panels, dark text, amber accents |
| Black | High contrast. Pure black, white text, brighter accents |
| Dusk | Indigo panels, a coral accent, sky-blue IR |
| Terminal | Green phosphor on near-black, a yellow accent, square corners |

They're defined in `firmware/remote/components/ui/ui_themes.c`.

## Roles

Code uses these names, from `ui_kit.h`. Each one reads the active theme.

| Name | Role |
|---|---|
| `COL_BG` | Behind everything |
| `COL_SURFACE1` | Panels |
| `COL_SURFACE2` | Buttons and tiles on panels |
| `COL_SURFACE3` | Pressed or selected items, and tracks |
| `COL_LINE` | Dividers |
| `COL_LINE2` | Outlines |
| `COL_TEXT` | Main text and icons |
| `COL_TEXT2` | Secondary text and icons |
| `COL_TEXT3` | Faint text and icons |
| `COL_ACCENT` | Fills: Done and other main buttons, Play, sliders, switches |
| `COL_ON_ACCENT` | Text and icons on `COL_ACCENT` |
| `COL_ACCENT_TEXT` | The accent as text, an icon, an outline or a spinner on a surface |
| `COL_IR` | IR devices' icons and tags |
| `COL_ON_IR` | Text on an IR fill |
| `COL_IR_LINE` | IR outlines |
| `COL_GOOD` | Charging |
| `COL_BAD` | Errors, low battery, Power |
| `COL_SUN`, `COL_SUN_SOFT`, `COL_RAIN`, `COL_STORM`, `COL_COOL`, `COL_WARM`, `COL_MOON` | Data colors (weather icons, temperature bars) |

Rules for screens and modules:

- **Use `COL_ACCENT` for fills and `COL_ACCENT_TEXT` for everything else
  in the accent color.** The two are the same in dark themes. Daylight's
  bright amber is unreadable as text on white, so its `COL_ACCENT_TEXT` is
  a darker amber. Text on a `COL_ACCENT` fill uses `COL_ON_ACCENT`.
- **Corners:** pass every radius through `ui_radius(r)`. `ui_panel` and
  `ui_btn` already do. Square themes shrink the radius;
  `LV_RADIUS_CIRCLE` stays round.
- **Pressed shades:** `ui_btn` handles them. Elsewhere, use
  `ui_pressed(bg)`, which is lighter in dark themes and darker in light
  ones.
- **No fixed colors:** don't use `lv_color_hex()` in screens, because a
  color that isn't a role won't follow the theme. If a module needs a
  color no role covers, propose a new role.
- **Colors are read when a screen is built.** That's why a new theme
  applies with a restart.

## Adding a theme

1. Add an entry to `s_themes` in `ui_themes.c`. Lay it out like the
   others, because the contrast check reads that layout. The entry has:

   - `id`: lowercase and at most 15 characters. Keep it stable, because
     it's what NVS stores. A longer id silently falls back to Midnight.
   - `name`: the name shown in Settings.
   - `light`: dark text on light surfaces.
   - `square`: small corners.
   - A color for every role.

2. Run the contrast check from the repository's root folder, and fix
   anything it flags:

   ```powershell
   python firmware\remote\tools\check_themes.py
   ```

   It wants a contrast of at least 4.5:1 for main and secondary text on
   every surface, and for text on accent and IR fills. Faint text, accent
   text, IR and error colors need at least 3:1.

3. Look at every screen, at every board's size, in the simulator. Run it
   from `firmware/remote`:

   ```powershell
   python sim/run_sim.py --theme <id>
   ```

   `<id>` is your theme's id. [sim/README.md](../firmware/remote/sim/README.md)
   explains the simulator.

4. Build and install the firmware, pick the theme in
   **Settings > Appearance**, and check it on the remote.
   `tools/devtools.py shot` takes the screenshots.

Settings shows up to 8 themes.

## Limits and plans

- **Fonts are the same in every theme.** An all-monospace Terminal would
  need IBM Plex Mono at every size the UI uses, roughly 0.5 MB of flash.
  So for now Terminal keeps Sora.
- **Custom themes** as files on the remote (LittleFS or SD card) are
  planned. The colors and radius would come from a JSON file with the same
  roles.
- **Switching without a restart** would need every screen rebuilt and
  refilled with the remote's current state. A restart does exactly that.
