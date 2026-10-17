# Contributing to Hubmote

Thanks for helping. This page is for anyone who wants to report a problem
or send a change. Bug reports, tests on hardware you own (TVs, Google TV
devices, other boards), IR code sets, modules and fixes are all welcome.
Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).

## Before you start

- **Reports.** Open an issue. Give your board, what you did, what
  happened, and the firmware version from **Settings > About**. Add the
  log if you can. You can get it over USB with any serial monitor, or
  over Wi-Fi on developer builds (see
  [docs/troubleshooting.md](docs/troubleshooting.md#logs)). Leave out your
  network's name, your addresses and the update token.
- **Security problems.** Don't report them in an issue. See
  [SECURITY.md](SECURITY.md).
- **Bigger changes.** Describe what you have in mind in an issue first,
  so the approach can be agreed on before you spend the time. Planned
  work is in the [issues](https://github.com/DovahBrownies/Hubmote/issues).

## Building

[docs/building.md](docs/building.md) explains how to set up the PC, and
how to build, flash and update the firmware (ESP-IDF 5.4.4).

- **On Windows**, use the wrapper scripts in `firmware/remote/scripts/`.
- **On macOS and Linux**, use `idf.py` with ESP-IDF activated. For updates
  and logs over Wi-Fi, use `tools/hm_ota.py` and `tools/hm_netlog.py`.
- **The companion app** builds without Gradle, with `companion/build.ps1`
  (in `pwsh`, on any OS).

### Before you send a pull request

Check these. Paths are relative to `firmware/remote`.

- **Builds.** The developer build and the release build compile without
  warnings. Build the release build with `hm-web.ps1`, or as in
  [the workflow](.github/workflows/build.yml).
- **Host tests.** These pass:
  - The IR codec tests in `components/ir/host_test`. On Windows, run
    `run_host_tests.ps1`. Elsewhere, run the gcc line from the workflow.
  - The Weather test, `modules/weather/test/run_test.py`.
  - `tools/check_themes.py`, after a palette change.
- **On a board.** If your change touches the device, check that it works
  on a board, and say how you checked. Screenshots with
  `tools/devtools.py` help.
- **Every screen size.** A UI change looks right at every board's screen
  size. The UI simulator draws every screen at each size: run
  `python sim/run_sim.py` (see
  [sim/README.md](firmware/remote/sim/README.md)).

The GitHub workflow runs the same builds and tests on every pull request
except one that only changes Markdown files, `docs/` or `hardware/`.

## How the code is written

- **Match the surrounding code.** That means C11 with 4-space indents,
  lines up to about 120 characters, `static` for everything a file
  doesn't export, and comments that say why rather than what. Java in the
  companion targets Java 8 (`javac --release 8`).
- **Threads.** Use LVGL only on the LVGL task or through
  `hub_ui_async()`. Module `init` and `event` run on the controller task,
  which must never block for long.
- **Memory.** Internal RAM is the scarce resource. Put buffers in PSRAM
  (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) and watch the `ctrl: heap`
  log line.
- **UI.** Give pixel sizes through `UI_S()`, so they scale with the
  screen. Use colors only through the theme roles (`COL_*`), corners
  through `ui_radius()`, and `ui_status` for loading and error states.
  Labels name things, and screens carry no explanatory hint text. See
  [the UI kit](docs/modules.md#the-look-ui_kith), and
  [docs/themes.md](docs/themes.md) for colors. Check a change in a light
  theme (Daylight) too.
- **Modules.** Follow [docs/modules.md](docs/modules.md) and its
  checklist. `tools/new_module.py` makes the folder.
- **Other boards.** [docs/porting.md](docs/porting.md) lists the boards,
  and says what a board needs and what a port involves. Open an issue
  before you start. If you run Hubmote on a board marked untested, please
  report how it went in
  [issue #44](https://github.com/DovahBrownies/Hubmote/issues/44).
- **IR codes** come only from sources that allow reuse: SmartIR, and
  Flipper-IRDB files contributed after commit 2319685. Import them with
  `tools/ir/import_tv_codes.py` (see [docs/ir-tvs.md](docs/ir-tvs.md)).
  Never hand-edit `tv_codes.c`, and never copy codes from LIRC,
  probonopd/irdb or older Flipper files.
- **Never commit secrets or personal data.** `secrets.env` and
  `sdkconfig` are gitignored. Keep tokens, network names and addresses
  out of code, fixtures and logs.
- **Third-party work.** Add every new dependency, font, icon set, code
  source or data source to [NOTICE](NOTICE), with its license.
- **Docs.** When setup steps, settings, ports, stored data or behavior
  change, update the guide in `docs/` or the README next to the code.
  Anything about connections or stored secrets goes in
  [docs/security.md](docs/security.md).

## License

Hubmote is under the [Apache License 2.0](LICENSE). By sending a
contribution, you agree that it is under the same license.
