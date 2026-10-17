# Releasing

How Hubmote gets to GitHub (github.com/DovahBrownies/Hubmote), what the
workflow builds, and how a release goes out.

## What the workflow does

`.github/workflows/build.yml` runs on every push to `main` and every pull
request that changes more than the docs (Markdown files, `docs/`,
`hardware/`), on version tags (always) and by hand (Actions > Build > Run
workflow). A newer push to `main` or to a pull request cancels the run it
replaces; a tag's run is never cancelled.

| Job | What it does |
|---|---|
| Host tests | The IR codec test (gcc, with the undefined-behavior sanitizer), the contrast of every theme (`tools/check_themes.py`), the Weather test (with cJSON 1.7.18), and a check that `atv_keycodes.h` and `atv_golden.h` match their generators (the golden vectors with protobuf 7.36.2 and grpcio-tools 1.84.0, as `gen_golden.py` names). |
| UI simulator | The UI built for the PC (`firmware/remote/sim`, with LVGL at the version `dependencies.lock` names), every screen at every board's screen size in Midnight and Daylight, kept as the `screens` artifact to look at. |
| Boards, Firmware | One firmware job per board (a `.defaults` file in `components/board/boards`): a developer build (`-D HM_BOARD=<id>`), then the release build (`sdkconfig.release`, no secrets, in `build-web/`) with ESP-IDF 5.4.4 in Espressif's container, kept as `release-<id>`. On a tag, `version.txt` is set from the tag first (`v0.1.0` becomes `0.1.0`). |
| Installer site | All the release builds into one site (`tools/web/make_site.py`): a folder per board with its images and `manifest.json`, the page with its board picker, and `update.json`, from which remotes already in use take their own board's image. |
| Companion app | JDK 17 and Android SDK 35, then `companion/build.ps1` (pwsh). Tag builds are signed with the project's key, which exists only for that build and is deleted right after; all others with a throwaway key. `companion.json` next to the APK says which. |
| GitHub Pages | On a tag only: adds the companion to the installer site (`companion.apk` to download by hand and, only if the project's key signed it, `companion-<version>.apk` with its entry in `update.json`, from which remotes offer it; `make_site.py --no-firmware --companion`), then publishes the site at https://dovahbrownies.github.io/Hubmote/. A manual run on `main` only builds: publishing `main` would offer every remote a development build. |
| Release | On a tag: a GitHub release with `hubmote-<board>-<tag>.zip` for each board (its four images and `manifest.json`) and `hubmote-companion-<tag>.apk`, with notes generated from the commits. |

The site, the screenshots and the APK of every run are kept as run
artifacts (`installer-site`, `screens`, `companion-apk`).

The actions the workflow uses are pinned to commits, with their tag in a
comment, so a moved tag can't change what runs with the signing key.
Dependabot (`.github/dependabot.yml`) proposes updates weekly as pull
requests; check what changed before merging one. Each pull request runs
the whole workflow (seven firmware jobs, about 15 minutes).

## Once, before the first release

**1. A fresh history** (done for this repository on 2026-10-16). Its
working history held personal data and research, so it was never pushed:
the public repository starts from one commit of the tree. For a fork with
history of its own to drop, first make sure nothing personal is tracked:
search for your own details (your board's MAC, your home network's
addresses, your user folder, your TV's model) with `git grep -n -I -i -E
"<pattern>|<pattern>"`, and keep notes about your setup in the ignored
`CLAUDE.local.md` or `private/`. Check the name and email the commit
will carry (`git config user.name`, `git config user.email`): they are
public in every commit. To keep your email private, use GitHub's noreply
address (Settings > Emails) for this repository with `git config
user.email <id>+<user>@users.noreply.github.com`. Do this before the first
push: GitHub keeps every commit it has seen, and a pull request's commits
(Dependabot's too) stay reachable after a force-push. Then:

```powershell
git status                       # clean; private/ and CLAUDE.local.md are ignored
git checkout --orphan public
git commit -m "Hubmote 0.1.0"
git push origin public:main
git branch -m main local-history   # keep the old history locally, never push it
git branch -m public main
git branch -u origin/main
```

From then on, work on the new `main` and push it as usual: the old commits
aren't its ancestors, so they never go up.

**2. Repository settings on GitHub.** On a free account, Pages, rulesets
and private vulnerability reporting need a public repository, so the order
is: CI green on `main`, make the repository public (Settings > General >
Change visibility), then these settings, then the signing key (3), then
the first tag. Each setting with `gh` (`R=DovahBrownies/Hubmote`), or in
the web settings named in brackets:

- **The repository's page** (the About box, Settings > General >
  Features): a description, the installer page as its website, topics,
  and no Wiki or Projects tabs (the docs live in the repository, and anyone
  with an account can edit a public wiki):
  ```
  gh repo edit $R --description "A touchscreen remote for the Google TV Streamer, TVs and IR devices, on Waveshare ESP32-S3 touch boards." --homepage https://dovahbrownies.github.io/Hubmote/ --add-topic esp32-s3,esp-idf,lvgl,google-tv,android-tv,remote-control,infrared,waveshare --enable-wiki=false --enable-projects=false
  ```
- **Pages from Actions** (Settings > Pages > Source: GitHub Actions):
  `gh api -X POST repos/$R/pages -f build_type=workflow`
- **Tags may deploy to Pages** (Settings > Environments > github-pages >
  Deployment branches and tags): Pages creates the environment allowing
  only `main`, and releases deploy from a tag, so without this the Pages
  job fails ("not allowed to deploy to github-pages"):
  ```
  gh api -X PUT repos/$R/environments/github-pages -F "deployment_branch_policy[protected_branches]=false" -F "deployment_branch_policy[custom_branch_policies]=true"
  gh api -X POST repos/$R/environments/github-pages/deployment-branch-policies -f name='v*' -f type=tag
  ```
- **Release tags protected** (Settings > Rules > Rulesets > New tag
  ruleset): `v*` tags can't be created, moved or deleted except by
  repository admins, so only you can publish, move or delete a release
  tag. With `gh api -X POST repos/$R/rulesets --input -` and:
  ```json
  {"name": "Release tags", "target": "tag", "enforcement": "active",
   "conditions": {"ref_name": {"include": ["refs/tags/v*"], "exclude": []}},
   "rules": [{"type": "creation"}, {"type": "update"}, {"type": "deletion"}],
   "bypass_actors": [{"actor_id": 5, "actor_type": "RepositoryRole", "bypass_mode": "always"}]}
  ```
- **`main` protected** (Settings > Rules > Rulesets > New branch ruleset):
  nobody, admins included, can force-push or delete it, so the history
  people cloned stays valid. From here on, changes are new commits, never
  an amended one. Pull requests aren't required: only you can push, and
  everyone else comes through a pull request anyway. To ever rewrite
  `main`, switch the ruleset off first. With the same `gh` command and:
  ```json
  {"name": "main", "target": "branch", "enforcement": "active",
   "conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
   "rules": [{"type": "deletion"}, {"type": "non_fast_forward"}]}
  ```
- **Private vulnerability reporting** (Settings > Security; `SECURITY.md`
  and the code of conduct point there):
  `gh api -X PUT repos/$R/private-vulnerability-reporting`
- **Secret scanning and push protection** (Settings > Security > Advanced
  Security; free on a public repository): GitHub refuses a push that
  contains a token it recognises.
  `gh api -X PATCH repos/$R -f "security_and_analysis[secret_scanning][status]=enabled" -f "security_and_analysis[secret_scanning_push_protection][status]=enabled"`
- **Dependabot alerts** (same page), for the actions the workflow uses:
  `gh api -X PUT repos/$R/vulnerability-alerts`
- **Outside contributors' workflows wait for approval** (Settings >
  Actions > General > Approval for running fork pull request workflows):
  `gh api -X PUT repos/$R/actions/permissions/fork-pr-contributor-approval -f approval_policy=all_external_contributors`
  The rest of that page is right by default (checked): a read-only
  workflow token, which the workflow raises where it needs to, and that
  can't approve pull requests.

**3. The companion's signing key.** Android only installs an update signed
with the same key as the installed app, so releases need one stable key.
Make it once, keep it safe offline (losing it means users must uninstall
before the next version), and give it to the workflow:

```powershell
$kt = "$env:LOCALAPPDATA\Android\jdk-17\bin\keytool.exe"
& $kt -genkeypair -keystore hubmote-release.keystore -alias hubmote -keyalg RSA -keysize 4096 `
    -validity 10000 -dname "CN=Hubmote"
```

keytool asks for a password, which protects the key too: keep it with the
keystore's backup (a password manager, say). Then the three repository
secrets, with `gh` (the second command asks for the password without
showing it):

```powershell
$R = "DovahBrownies/Hubmote"
gh secret set COMPANION_KEYSTORE_BASE64 -R $R -b ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Resolve-Path hubmote-release.keystore))))
gh secret set COMPANION_KEYSTORE_PASSWORD -R $R
gh secret set COMPANION_KEY_ALIAS -R $R -b hubmote
```

Or in Settings > Secrets and variables > Actions > New repository secret:
`COMPANION_KEYSTORE_BASE64` (the keystore in base64:
`[Convert]::ToBase64String([IO.File]::ReadAllBytes((Resolve-Path
hubmote-release.keystore))) | Set-Clipboard` copies it),
`COMPANION_KEYSTORE_PASSWORD` (the password) and `COMPANION_KEY_ALIAS`
(`hubmote`).

A tag build fails without them, rather than publishing an APK signed with a
throwaway key.

## A release

1. On `main`, with the build green: if the companion changed, raise
   `companion/version.txt` (`MAJOR.MINOR`; the version code is MAJOR * 100
   + MINOR). `firmware/remote/version.txt` holds the coming version with
   `-dev` (`0.1.0-dev` before 0.1.0): the tag sets the release version.
2. Tag and push the tag:
   ```powershell
   git tag v0.1.0
   git push origin v0.1.0
   ```
3. Watch Actions > Build. When it's done, the installer page has the new
   firmware (its footer shows the version) and the release has the files.
   Remotes already in use find it through `update.json` within a day (or
   at once with Settings > About > Check) and install it when their owner
   taps Install. A newer companion shows in Settings > Remotes (Update
   companion). A companion built locally with `debug.keystore` can't take
   it: reinstall it once from the release APK (uninstall first, then pair
   again).
4. Raise `firmware/remote/version.txt` to the next version with `-dev`
   (`0.1.1-dev`) and commit it. A developer build still saying `0.1.0-dev`
   counts as older than the release, so its Settings > About would offer to
   replace it with the release build (which has no update server for the
   PC tools).
5. Check the release on a board: flash it from the installer page (Erase,
   for a first-run check), set it up, pair, and install the companion APK.

**Try the installer before tagging:** `firmware\remote\scripts\hm-web.ps1
-Serve` builds the same release firmware (every board; `-Board <id>` for
fewer) and site on a PC (http://localhost:8000), without the companion (no `companion.apk` and no
companion entry in `update.json`).
