# Contributing to Hubmote

Thanks for helping. Bug reports, tests on hardware you own (TVs, Google TV
devices, other boards), IR code sets, modules and fixes are all welcome.
Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).

## Before you start

- **Reports:** open an issue with your board, what you did, what happened,
  the firmware version (Settings > About) and, if you can, the log (over
  USB with any serial monitor, or over Wi-Fi on developer builds:
  [docs/troubleshooting.md](docs/troubleshooting.md#logs)). Leave out your
  network's name, your addresses and the update token.
- **Security problems:** not in an issue; see [SECURITY.md](SECURITY.md).
- **Bigger changes:** say what you have in mind in an issue first, so the
  approach can be agreed before you spend the time. The plan and the
  decisions so far are in [docs/PLAN.md](docs/PLAN.md).

## Building

[docs/building.md](docs/building.md) sets up the PC and builds, flashes
and updates the firmware (ESP-IDF 5.4.4). On Windows use the wrapper scripts in
`firmware/remote/scripts/`; on macOS and Linux, `idf.py` with ESP-IDF
activated, and `tools/hm_ota.py` / `tools/hm_netlog.py` for updates and logs
over Wi-Fi. The companion app builds without Gradle (`companion/build.ps1`,
pwsh on any OS).

Check before sending a pull request (paths from `firmware/remote`):
- the developer build and the release build (`hm-web.ps1`, or as in
  `.github/workflows/build.yml`) compile without warnings;
- the host tests pass: the IR codecs (`components/ir/host_test`:
  `run_host_tests.ps1` on Windows, the gcc line in the workflow
  elsewhere), `modules/weather/test/run_test.py`, and
  `tools/check_themes.py` after a palette change;
- what you changed works on a board, if it touches the device (say how you
  checked; screenshots with `tools/devtools.py` help);
- a UI change looks right at every board's screen size: the UI simulator
  draws every screen at each (`python sim/run_sim.py`,
  [sim/README.md](firmware/remote/sim/README.md)).

The GitHub workflow runs the same builds and tests on every pull request.

## How the code is written

- **Match the surrounding code:** C11 with 4-space indents, lines up to
  about 120 characters, `static` for everything a file doesn't export,
  comments that say why rather than what. Java in the companion targets
  Java 8 (`javac --release 8`).
- **Threads:** LVGL only on the LVGL task or through `hub_ui_async()`;
  module `init` and `event` run on the controller task, which must never
  block for long. Internal RAM is the scarce resource: put buffers in PSRAM
  (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) and watch the `ctrl: heap`
  log line.
- **UI:** pixel sizes through `UI_S()` (they scale with the screen),
  colors only through the theme roles (`COL_*`), corners through
  `ui_radius()`, loading and error states with `ui_status`; labels name
  things, and screens carry no explanatory hint text
  ([the UI kit](docs/modules.md#the-look-ui_kith); colors:
  [docs/themes.md](docs/themes.md)). Check a change in a light theme
  (Daylight) too.
- **Modules:** follow [docs/modules.md](docs/modules.md) and its checklist;
  `tools/new_module.py` makes the folder.
- **Other boards:** [docs/porting.md](docs/porting.md) lists the boards,
  says what a board needs and what a port involves; open an issue before
  you start. If you run Hubmote on a board marked untested, please report
  how it went in [issue #44](https://github.com/DovahBrownies/Hubmote/issues/44).
- **IR codes** come only from sources that allow reuse (SmartIR, and
  Flipper-IRDB files contributed after commit 2319685), through
  `tools/ir/import_tv_codes.py` ([docs/ir-tvs.md](docs/ir-tvs.md)). Never
  hand-edit `tv_codes.c` or copy codes from LIRC, probonopd/irdb or older
  Flipper files.
- **Never commit secrets or personal data:** `secrets.env` and `sdkconfig`
  are gitignored; keep tokens, network names and addresses out of code,
  fixtures and logs.
- **Third-party work:** add every new dependency, font, icon set, code or
  data source to [NOTICE](NOTICE) with its license.
- **Docs:** update the guide in `docs/` or the README next to the code when setup steps,
  settings, ports, stored data or behaviour change
  ([docs/security.md](docs/security.md) for anything about connections or
  stored secrets).

## License

Hubmote is under the [Apache License 2.0](LICENSE). By sending a
contribution you agree that it is under the same license.
