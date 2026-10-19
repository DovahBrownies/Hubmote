# Releasing

This page is for the project's maintainers. It covers what the GitHub
workflow builds, the one-time setup of the repository
(github.com/DovahBrownies/Hubmote), and how a release goes out.

On this page:

- [What the workflow does](#what-the-workflow-does)
- [Once, before the first release](#once-before-the-first-release)
- [A release](#a-release)

## What the workflow does

`.github/workflows/build.yml` runs:

- on every push to `main` and every pull request, unless the change only
  touches the docs (Markdown files, `docs/`, `hardware/`);
- on version tags, always;
- by hand, from **Actions > Build > Run workflow**.

A newer push to `main` or to a pull request cancels the run it replaces.
A tag's run is never cancelled.

### The jobs

- **Host tests** run these checks without a board:
  - the IR codec test (gcc, with the undefined-behavior sanitizer);
  - the touchpad's stick (`components/ui/host_test/test_pad_joy.c`);
  - the contrast of every theme (`tools/check_themes.py`);
  - the Weather test (with cJSON 1.7.18);
  - a check that `atv_keycodes.h` and `atv_golden.h` match their
    generators. The golden vectors are made with protobuf 7.36.2 and
    grpcio-tools 1.84.0, the versions `gen_golden.py` names.
  - a check that `hub_tz.c`, the time zones, matches
    `tools/time/gen_zones.py` with tzdata 2026.5.
- **UI simulator** builds the UI for the PC (`firmware/remote/sim`), with
  LVGL at the version `dependencies.lock` names. It draws every screen at
  every board's screen size, in Midnight and Daylight, and keeps them as
  the `screens` artifact to look at. It also compiles a module made from
  `tools/module_template`, so the template keeps up with the module API.
- **Boards** and **Firmware** run one firmware job per board, that is, per
  `.defaults` file in `components/board/boards`. Each job uses ESP-IDF
  5.4.4 in Espressif's container. It makes a developer build
  (`-D HM_BOARD=<id>`), then the release build (`sdkconfig.release`, no
  secrets, in `build-web/`), which it keeps as `release-<id>`. On a tag,
  the job first sets `version.txt` from the tag (`v0.1.0` becomes
  `0.1.0`). The 7B's job also checks that the simulator's LVGL
  configuration (`sim/lv_kconfig.h`) is the firmware's.
- **Installer site** puts all the release builds into one site with
  `tools/web/make_site.py`. The site has a folder per board with its
  images and `manifest.json`, the page with its board picker, and
  `update.json`. Remotes already in use take their own board's image from
  `update.json`. It also tries the signing step with a throwaway key, so
  a broken one shows before a release.
- **Companion app** runs on every build but a tag's. It sets up JDK 17 and
  Android SDK 35, then runs `companion/build.ps1` (pwsh), signed with a
  throwaway key. `companion.json`, next to the APK, says which key signed
  it.
- **Release signing** runs on a tag only, in the `release` environment:
  the one job that can read the keys
  ([step 2](#2-repository-settings-on-github)). It builds the companion
  signed with the project's key, adds it to the installer site
  (`make_site.py --no-firmware --companion`), and signs each board's image
  in `update.json` with the update signing key
  ([step 4](#4-the-update-signing-key)). Each key exists only for its
  step and is deleted right after. It stops if the companion's certificate
  isn't the one in `companion/release-cert.txt`.
- **GitHub Pages** runs on a tag only. It publishes the site Release
  signing made at https://dovahbrownies.github.io/Hubmote/. The
  companion goes on the site in two forms:
  - `companion.apk`, to download by hand;
  - `companion-<version>.apk` with its entry in `update.json`, from which
    remotes offer it. This one is added only if the project's key signed
    the APK.

  A manual run on `main` only builds, because publishing `main` would
  offer every remote a development build.
- **Release** runs on a tag. It makes a GitHub release with notes
  generated from the commits, and these files:
  - `hubmote-<board>-<tag>.zip` for each board, with its four images and
    `manifest.json`;
  - `hubmote-companion-<tag>.apk`.

Every run keeps the site, the screenshots and the APK as run artifacts
(`installer-site`, `screens`, `companion-apk`); a tag's run also keeps
the signed site (`signed-site`).

### Code scanning

`.github/workflows/codeql.yml` runs CodeQL on pushes to `main`, on pull
requests and weekly: the firmware's C, the companion's Java, the Python
tools and the workflows, without building them. Findings show under
**Security > Code scanning**. Generated code (fonts, IR code sets, time
zones) and the vendored ESP Web Tools are left out.

### Pinned actions and Dependabot

The actions the workflow uses are pinned to commits, with their tag in a
comment. That way a moved tag can't change what runs with the signing key.
Espressif's build container is pinned the same way, by digest
(`IDF_IMAGE` in the firmware job, with how to update it). Dependabot
doesn't update the container: do it by hand when moving to a new ESP-IDF.
Dependabot (`.github/dependabot.yml`) proposes updates weekly, as pull
requests. Check what changed before you merge one. Each one runs the
whole workflow: seven firmware jobs, in about 10 minutes.

## Once, before the first release

Do these four things once, in this order.

### 1. A fresh history

This was done for this repository on 2026-10-16. Its working history held
personal data and research, so it was never pushed. The public repository
starts from one commit of the tree.

If you have a fork with history of its own to drop, check two things
first:

1. Make sure nothing personal is tracked. Search for your own details,
   such as your board's MAC, your home network's addresses, your user
   folder and your TV's model:

   ```powershell
   git grep -n -I -i -E "<pattern>|<pattern>"
   ```

   Replace each `<pattern>` with one of those details. Keep notes about
   your setup in the ignored `CLAUDE.local.md` or `private/`.
2. Check the name and email the commit will carry
   (`git config user.name`, `git config user.email`). They are public in
   every commit. To keep your email private, use GitHub's noreply address
   for this repository:

   ```powershell
   git config user.email <id>+<user>@users.noreply.github.com
   ```

   Put in the noreply address that **Settings > Emails** shows.

> [!IMPORTANT]
> Do both checks before the first push. GitHub keeps every commit it has
> seen, and a pull request's commits (Dependabot's too) stay reachable
> after a force-push.

Then run these commands in the repository's folder:

```powershell
git status                       # clean; private/ and CLAUDE.local.md are ignored
git checkout --orphan public
git commit -m "Hubmote 0.1.0"
git push origin public:main
git branch -m main local-history   # keep the old history locally, never push it
git branch -m public main
git branch -u origin/main
```

From then on, work on the new `main` and push it as usual. The old commits
aren't its ancestors, so they never go up.

### 2. Repository settings on GitHub

On a free account, Pages, rulesets and private vulnerability reporting
need a public repository. So go in this order:

1. Get CI green on `main`.
2. Make the repository public (**Settings > General > Change
   visibility**).
3. Apply the settings below.
4. Add the signing keys: the companion's
   ([step 3](#3-the-companions-signing-key)) and the update signing key
   ([step 4](#4-the-update-signing-key)). Without either secret a tag build
   fails.
5. Push the first tag.

You can apply each setting with `gh`, or on the settings page named next
to it. The `gh` commands use `$R` for the repository. Set it first:
`R=DovahBrownies/Hubmote` in a POSIX shell, or `$R = "DovahBrownies/Hubmote"`
in PowerShell, as in step 3.

- **The repository's page** (the About box, and **Settings > General >
  Features**). Give it a description, the installer page as its website,
  and topics. Turn off the Wiki and Projects tabs, because the docs live
  in the repository and anyone with an account can edit a public wiki.

  ```sh
  gh repo edit $R --description "A touchscreen remote for the Google TV Streamer, TVs and IR devices, on Waveshare ESP32-S3 touch boards." --homepage https://dovahbrownies.github.io/Hubmote/ --add-topic esp32-s3,esp-idf,lvgl,google-tv,android-tv,remote-control,infrared,waveshare --enable-wiki=false --enable-projects=false
  ```

- **Pages from Actions** (**Settings > Pages > Source: GitHub Actions**):

  ```sh
  gh api -X POST repos/$R/pages -f build_type=workflow
  ```

- **Tags may deploy to Pages** (**Settings > Environments > github-pages >
  Deployment branches and tags**). Pages creates the environment allowing
  only `main`, but releases deploy from a tag. Without this setting, the
  Pages job fails with "not allowed to deploy to github-pages".

  ```sh
  gh api -X PUT repos/$R/environments/github-pages -F "deployment_branch_policy[protected_branches]=false" -F "deployment_branch_policy[custom_branch_policies]=true"
  gh api -X POST repos/$R/environments/github-pages/deployment-branch-policies -f name='v*' -f type=tag
  ```

- **The release environment** (**Settings > Environments > New
  environment**: `release`, then **Deployment branches and tags**:
  selected, a tag rule `v*`). The signing keys of steps 3 and 4 are its
  secrets, and only workflow runs for `v*` tags can read them: a change
  to the workflow on `main` or in a pull request can't. With the tag
  ruleset below, only admins can push such a tag. Optionally add yourself
  under **Required reviewers**, so each release waits for your approval.

  ```sh
  gh api -X PUT repos/$R/environments/release -F "deployment_branch_policy[protected_branches]=false" -F "deployment_branch_policy[custom_branch_policies]=true"
  gh api -X POST repos/$R/environments/release/deployment-branch-policies -f name='v*' -f type=tag
  ```

  A repository set up before this had the keys as repository secrets.
  Add them to the environment (steps 3 and 4), push the workflow that
  uses it, then delete the repository ones:
  `gh secret delete <NAME> -R $R` for each of the four.

- **Release tags protected** (**Settings > Rules > Rulesets > New tag
  ruleset**). Only repository admins can create, move or delete `v*` tags,
  so only you can publish, move or delete a release tag. Send this JSON to
  `gh api -X POST repos/$R/rulesets --input -`, which reads it from
  standard input:

  ```json
  {"name": "Release tags", "target": "tag", "enforcement": "active",
   "conditions": {"ref_name": {"include": ["refs/tags/v*"], "exclude": []}},
   "rules": [{"type": "creation"}, {"type": "update"}, {"type": "deletion"}],
   "bypass_actors": [{"actor_id": 5, "actor_type": "RepositoryRole", "bypass_mode": "always"}]}
  ```

- **`main` protected** (**Settings > Rules > Rulesets > New branch
  ruleset**). Nobody, admins included, can force-push or delete it, so the
  history people cloned stays valid.
  - From here on, changes are new commits, never amended ones.
  - Pull requests aren't required, because only you can push and everyone
    else comes through a pull request anyway.
  - To ever rewrite `main`, switch the ruleset off first.

  Use the same `gh` command with this JSON:

  ```json
  {"name": "main", "target": "branch", "enforcement": "active",
   "conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
   "rules": [{"type": "deletion"}, {"type": "non_fast_forward"}]}
  ```

- **Private vulnerability reporting** (**Settings > Security**).
  `SECURITY.md` and the code of conduct point there.

  ```sh
  gh api -X PUT repos/$R/private-vulnerability-reporting
  ```

- **Secret scanning and push protection** (**Settings > Security >
  Advanced Security**, free on a public repository). GitHub refuses a push
  that contains a token it recognizes.

  ```sh
  gh api -X PATCH repos/$R -f "security_and_analysis[secret_scanning][status]=enabled" -f "security_and_analysis[secret_scanning_push_protection][status]=enabled"
  ```

- **Dependabot alerts** (same page), for the actions the workflow uses:

  ```sh
  gh api -X PUT repos/$R/vulnerability-alerts
  ```

- **Outside contributors' workflows wait for approval** (**Settings >
  Actions > General > Approval for running fork pull request
  workflows**):

  ```sh
  gh api -X PUT repos/$R/actions/permissions/fork-pr-contributor-approval -f approval_policy=all_external_contributors
  ```

  The rest of that page is right by default; this was checked. The
  workflow token is read-only, and the workflow raises it where it needs
  to. The token also can't approve pull requests.

### 3. The companion's signing key

Android only installs an update signed with the same key as the installed
app, so releases need one stable key. Make it once, keep it safe offline,
and give it to the workflow.

> [!WARNING]
> If you lose the key, users must uninstall the companion before they can
> install the next version.

Make the key with keytool, in PowerShell. It goes in your home folder, not
in the repository:

```powershell
cd $HOME
$kt = "$env:LOCALAPPDATA\Android\jdk-17\bin\keytool.exe"
& $kt -genkeypair -keystore hubmote-release.keystore -alias hubmote -keyalg RSA -keysize 4096 `
    -validity 10000 -dname "CN=Hubmote"
```

keytool asks for a password, which protects the key too. Keep the password
with the keystore's backup, in a password manager for example.

Put the certificate's SHA-256 in `companion/release-cert.txt` (as keytool
prints it, or in lowercase hex) and commit it. The workflow refuses a release whose
companion is signed with any other key, so a wrong keystore in the secret
stops the release before anything is published. This prints it:

```powershell
& $kt -list -v -alias hubmote -keystore hubmote-release.keystore | Select-String "SHA256:"
```

Then add the three secrets to the `release` environment
([step 2](#2-repository-settings-on-github)) with `gh`. The second command asks
for the password without showing it.

```powershell
$R = "DovahBrownies/Hubmote"
gh secret set COMPANION_KEYSTORE_BASE64 -R $R --env release -b ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Resolve-Path hubmote-release.keystore))))
gh secret set COMPANION_KEYSTORE_PASSWORD -R $R --env release
gh secret set COMPANION_KEY_ALIAS -R $R --env release -b hubmote
```

Or add them in **Settings > Environments > release > Add environment
secret**:

- `COMPANION_KEYSTORE_BASE64` is the keystore in base64. This PowerShell
  command copies it to the clipboard:

  ```powershell
  [Convert]::ToBase64String([IO.File]::ReadAllBytes((Resolve-Path hubmote-release.keystore))) | Set-Clipboard
  ```

- `COMPANION_KEYSTORE_PASSWORD` is the password.
- `COMPANION_KEY_ALIAS` is `hubmote`.

Without these secrets, a tag build fails rather than publish an APK signed
with a throwaway key.

### 4. The update signing key

Remotes install a release from the site only if its image carries the
project's signature (`main/update.c`). Then someone who could change the
site still couldn't make remotes install an image of their own. The
private key lives only in a secret of the `release` environment and
offline. The public
key, `firmware/remote/main/update_key.pem`, is in the repository and is
built into the firmware.

> [!WARNING]
> If you lose the private key, or make a new one, remotes refuse later
> releases. Each then needs a firmware with the new public key, flashed
> from the installer page or a PC. Make it once and keep a copy safe.

In PowerShell, from the repository's folder, make the key outside the
repository and give its private half to the workflow:

```powershell
cd firmware\remote
python tools\web\make_update_key.py $HOME\hubmote-update-key.pem
gh secret set UPDATE_SIGNING_KEY -R DovahBrownies/Hubmote --env release -b (Get-Content -Raw $HOME\hubmote-update-key.pem)
```

Then commit `main/update_key.pem`, the public half, and keep the private
key's file with your other backups. The script needs `openssl` (Git for
Windows has one; run it from Git Bash if PowerShell doesn't find it).

Without the secret, a tag build fails rather than publish unsigned
images. The **Firmware signatures** step also refuses a key whose public
half isn't `main/update_key.pem`.

## A release

1. Start on `main`, with the build green. If the companion changed, raise
   `companion/version.txt`. It holds `MAJOR.MINOR`, and the version code
   is MAJOR * 100 + MINOR. `firmware/remote/version.txt` holds the coming
   version with `-dev` (`0.1.0-dev` before 0.1.0). The tag sets the
   release version.
2. Tag and push the tag:

   ```powershell
   git tag v0.1.0
   git push origin v0.1.0
   ```

3. Watch **Actions > Build**. When it's done, the installer page has the
   new firmware (its footer shows the version), and the release has the
   files.
   - Remotes already in use find the release through `update.json` within
     a day, or at once with **Settings > About > Check**. They install it
     when their owner taps **Install**.
   - A newer companion shows in **Settings > Remotes**
     (**Update companion**).
   - A companion built locally with `debug.keystore` can't take that
     update. Reinstall it once from the release APK: uninstall it first,
     install the APK, then pair again.
4. Raise `firmware/remote/version.txt` to the next version with `-dev`
   (`0.1.1-dev`) and commit it. A developer build that still says
   `0.1.0-dev` counts as older than the release. Its **Settings > About**
   would then offer to replace it with the release build, which has no
   update server for the PC tools.
5. Check the release on a board. Flash it from the installer page (with
   **Erase**, for a first-run check), set it up, pair, and install the
   companion APK.

### Try the installer before tagging

Build and serve the installer on a PC first, with `hm-web.ps1 -Serve`
([Developer and release builds](building.md#developer-and-release-builds)).
It builds the same release firmware and site as a tag does, without the
companion: no `companion.apk`, and no companion entry in `update.json`.
