# Caelestia Switch — Session Handover

*As of 2026-10-01 (after Phase A1). Written so a fresh conversation can continue without re-deriving anything. Read this first, then the design docs (Section 10).*

## 1. The project in one paragraph

Build **`caelestia-switch`**, a standalone C++/Qt6 Widgets/KF6 app that switches between stock KDE Plasma (`plasmashell`) and **Caelestia KDE** (`ladybug-me/caelestia-kde`, a Quickshell-based shell) without uninstalling either. It must work with an **unmodified** upstream install. Masking plasmashell lives in the app, not in any installer. Later, the user builds his own modified fork of caelestia-kde, and the app's install/update/uninstall can pick upstream or the fork. The earlier "fork-first" plan was replaced on 2026-09-30.

## 2. Where things live

| Thing | Location |
|---|---|
| App repo (source of truth for code and current docs) | `https://github.com/albertsalendah/Caelestia-Switch.git`, `main` at `e92e54c` (Phase A2 code) plus the doc/cleanup commit that follows it |
| Fork of caelestia-kde (unmodified, for later) | `https://github.com/albertsalendah/caelestia-kde.git`, `main` == `be4188f` (= upstream `v2.5.0`); its `dev` mirrors upstream `dev`, no changes of ours |
| Upstream | `https://github.com/ladybug-me/caelestia-kde.git`; `v2.5.1` released 2026-09-29 (149 commits past `be4188f`); the fork and tests deliberately stay on v2.5.0 |
| Main dev laptop | MSI GL63 8RE, CachyOS + KDE, 16 GiB. Project folder `/mnt/MainData/AppProjects/caelestia-switch`. Commits and pushes happen here (SSH + `gh` set up; git identity is set per repo) |
| Test laptop | ASUS X441BA, CachyOS, Plasma 6.7.5, KF 6.30.0, Qt 6.11.2, 4 GiB RAM, AMD A4-9125, **fish shell**, user `asus`. The user works on it over SSH from the MSI |
| App clone on the ASUS | `~/Caelestia-Switch` (https clone, **pull only**: `git pull --ff-only`, then build). Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2` (~10 s) |
| Caelestia install on the ASUS | installed from the checkout `~/caelestia-kde` (origin still `ladybug-me`, `HEAD` `be4188f`) |

## 3. Current state (what is true right now)

- **ASUS:** Caelestia is installed and running: `caelestia-shell.service` enabled and active; plasmashell running headless (no panels), **not masked**; `ShellPackage=caelestia.desktop`; lock screen greeter installed. A clean uninstall (unmask plasmashell, then `uninstall.sh` with the backup-restore option) and reinstall were done on 2026-09-30, and the baseline check passed.
- `~/caelestia-kde` has one local uncommitted edit: `installer/data/menu.json` ("Custom lockscreen" default set to `false`; deliberate, local only, not in the fork). Its `backups/` folder now holds only `20260930_115125`; the original pre-Caelestia backup may be gone (cause unconfirmed).
- **App repo:** Phases A1 and A2 are implemented. `src/core` (`readings`, `systemd`, `plasmaconfig`, `install`, `consistency`, `backup`, `fsutil.h`, `version`), `src/cli` (`caelestia-switch status [--json]`, `backup [--side stock|caelestia]`, `backups`, `restore <side/id>`; every other command exits 2 "not implemented"), `src/gui` (still the stub window), `tests/test_core.cpp` and `tests/test_backup.cpp` (run with `ctest --test-dir build --output-on-failure`), `data/*.desktop`, `docs/`. KF6 ConfigCore is linked. Builds on the MSI and ASUS (Qt 6.11.2, KF6 6.30.0). **MSI note:** after a forced shutdown `/mnt/MainData` came back as a FUSE (fuseblk) mount without execute permission, so build outputs there will not run; build in `~/cs-build` instead (`cmake -S . -B ~/cs-build ...`), and git needed `git config --global --add safe.directory <repo>`. The drive deserves a filesystem check.
- **ASUS backups present:** `caelestia/20261002_073602` (live Caelestia state), `stock/20261002_073839` (built from the 2026-09-30 reference files, with `XDG_CONFIG_HOME` pointed at a scratch copy), `stock/20261002_075632` (real stock state before the return trip). The ASUS is back in Caelestia mode (Condition A).
- `docs/` holds the three design docs, the test progress log (append-only) and this handover. The two historical docs were removed from the repo.

## 4. Done so far

- Source audit and all 17 original tests of caelestia-kde at `be4188f` passed (Conditions A and B, two masked-boot reboots).
- Design docs rewritten app-first; decisions D1–D16 recorded (Section 5).
- **Phase A0 (risk checks) done 2026-09-30:** see Section 6.
- Repo created, skeleton pushed, built on both laptops.
- **Phase A2 (backup/restore) done 2026-10-02:** code, tests and the live round trip both ways (Section 6).
- **Phase A1 (status) done 2026-10-01:** implemented and passed the live matrix on the ASUS (Section 6). Docs updated: D15 amended, D16 build note, D17 added, spec and roadmap updated.

## 5. Key decisions (full text in the architecture doc)

- **D1/D10:** masking is done by the app, not an installer; the app must work with unmodified upstream installs. Live pre-flight before every mask: `RequiredBy`, `WantedBy`, `BoundBy` of `plasma-plasmashell.service` must be empty.
- **Switch rules (D12):** to plasmashell: unmask it if masked, **always** stop and **disable** Caelestia (`disable --now`, not mask: the unit is a regular file in `~/.config/systemd/user`), restore the stock backup. To Caelestia: start it, restore the Caelestia backup; checkbox checked = stop and mask plasmashell, unchecked = leave plasmashell running headless (unmask and start it if needed). The checkbox only matters when switching to Caelestia.
- **Logout (D13):** always required, done last via `qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logout`; show a "save your work" warning; never force-kill apps. Do the config swap after the outgoing shell has stopped.
- **Helper units (D14):** stop `cliphist.service` and the update-checker units with Caelestia only if a live check shows nothing depends on them.
- **Backups (D11, narrowed 2026-10-01):** two-sided, timestamped, snapshot of the mode being left taken automatically; stored in `~/.local/share/caelestia-switch/backups/<side>/<timestamp>/` with a manifest. Only what the installer writes persistently is backed up. Whole-file: `plasma-org.kde.plasma.desktop-appletsrc`, `plasmashellrc`, `kscreenlockerrc`, and `cli.json`/`keybinds.json`/`shell.json`/`monitors` from `~/.config/caelestia/`. Key-level with "absent in the snapshot means delete" on restore: `kwinrc` (`Desktops`, `Plugins` keys the installer sets, `org.kde.kdecoration2`), `kwinrulesrc` (the three `caelestia-*` rule groups and their `[General]` entries), `plasmarc`, `kdeglobals`, `plasmanotifyrc`, `powerdevilrc`, `kmixrc`, `ksplashrc`. **Not backed up:** `kglobalshortcutsrc`, electric-border keys, `stolen-*.json`, `[Tiling]` groups, Konsole. The installer's own konsave backup is not relied on.
- **Detection (D15, amended 2026-10-01):** *source* from an app-written marker (`~/.config/caelestia-switch/install.json`, only if its commit == `.current_commit`), else checkout discovery (`$CAELESTIA_DIR` or `~/caelestia-kde`, `HEAD` == `.current_commit`; source = `origin` URL), else "unknown". *Version* from the marker, else `~/.config/quickshell/caelestia/.current_version`, else the checkout's `.github/version.env`, else "unknown". No first-run question.
- **Status rules (D17):** unit state over the user D-Bus (`LoadUnit` + `GetAll`), process scan of `/proc`, provider rule, the Inconsistent list, exit codes (0 normally, 3 if systemd unreachable).
- **Toolkit (D16):** C++ with Qt6 Widgets and KF6; core library + CLI + thin GUI. Build on the ASUS (clone, pull, build `-j2`); the MSI build is a compile check. Never use `-march=native`.
- **D4 resolved / D18 (2026-10-01):** a plain `systemctl stop` of Caelestia leaves its stolen shortcuts and screen corner in place; a graceful `quickshell kill -i <id>` (with `WAYLAND_DISPLAY` set) makes Caelestia release them itself. The switch quits Caelestia gracefully, waits for the unit to go inactive, then reloads the KWin Overview effect (`qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect overview`), then disables the unit.
- **D8/update:** `update` is manual only. Upstream's update checker and `update.sh` hardcode `ladybug-me/caelestia-kde` and accept only branches `main`/`dev`.

## 6. Verified facts (don't re-check)

- KCalc opened from Caelestia's launcher runs in its own scope (`app.slice/app-KDE-kcalc-….scope`), not in `caelestia-shell.service` (`session.slice`, `KillMode=control-group`). Not yet checked from stock Plasma's launcher.
- `systemctl --user disable --now caelestia-shell.service` removes the `graphical-session.target.wants` link; it stayed off across logout and a fresh login; `enable --now` restores it live.
- The logout call above works from an SSH shell and logs out with no confirmation dialog.
- With Caelestia off and plasmashell headless, the screen shows the wallpaper and KDE's own right-click menu: a headless plasmashell owns the desktop layer; panels come back only when the stock config is restored. When Caelestia runs, its right-click menu takes over.
- The install records only `.current_commit` and `.update_branch` under `~/.config/quickshell/caelestia/`. The version lives in `.github/version.env` in the checkout. No source repo is recorded.
- `caelestia-shell.service` is written by `10-autostart.sh` (`WantedBy=graphical-session.target`), starts via `~/.local/bin/caelestia-autostart.sh`, which rewrites `ShellPackage` to `caelestia.desktop`: stop Caelestia before resetting `ShellPackage`.
- `install.sh` run from inside a checkout goes straight to setup with no pull; run from outside with an existing `~/caelestia-kde`, it does `git pull --ff-only` from that checkout's `origin`.
- The installer also writes `~/.config/quickshell/caelestia/.current_version` (a copy of `.github/version.env`, `VERSION=v2.5.0`); the earlier "only `.current_commit` and `.update_branch`" note was wrong.
- **A1 live matrix (ASUS, all as expected):** Condition A reads provider Caelestia / ladybug-me (from checkout) / v2.5.0 / ok; plasmashell masked but running reads Inconsistent; masked and stopped reads ok; Caelestia disabled and stopped with plasmashell headless reads provider none / ok (screen shows wallpaper, a user-placed desktop icon and KDE's right-click menu); checkout moved away reads source unknown with the version still v2.5.0; a `status` taken right after `enable --now` can show quickshell "stopped" for a moment (service turns active before the process starts), harmless.
- **Clean-stop tests (ASUS, 2026-10-01, stock reference = the 2026-09-30 konsave archive `caelestia-preinstall.knsv`, unzipped to `/tmp/ref`):** `systemctl --user stop` leaves both `stolen-*.json` files, `Overview=none` (and `Log Out`, `Grid View`, `Walk Through Windows`, task-manager entries) in `kglobalshortcutsrc`, and `BorderActivate=9` in `kwinrc`; `Meta+W` and the corner stay dead. `quickshell kill -i <id>` from an SSH shell fails ("No running instances") unless `WAYLAND_DISPLAY=wayland-0` is set; with it, the quit is clean, shortcuts are restored, `stolen-screen-edges.json` is deleted, `stolen-shortcuts.json` stays as `[]`, the service ends inactive with no restart. The corner stayed dead until `reconfigureEffect overview` was run, then both worked. While Caelestia runs, `Meta+W` is Caelestia's "Launch Browser" (expected). `diff` of live config vs the reference shows the installer-written keys listed under D11.
- **A2 live round trip (ASUS, 2026-10-02):** see the roadmap A2 result. Commands that worked, in order: `env WAYLAND_DISPLAY=wayland-0 quickshell kill -p /home/asus/.config/quickshell/caelestia/shell.qml`, `systemctl --user disable --now caelestia-shell.service`, `systemctl --user stop plasma-plasmashell.service`, `qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect overview`, `caelestia-switch restore stock/<id>`, `systemctl --user start plasma-plasmashell.service`; logout via `qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logout`. Return: `caelestia-switch backup --side stock`, stop plasmashell, `restore caelestia/<id>`, `systemctl --user enable caelestia-shell.service`, logout. A `grep -c` with 0 matches exits 1 and broke an `and` chain, so the logout did not run and the new session sat on a black desktop (both shells inactive) until the logout was run by hand: the switch core must check each step explicitly. `plasmashell --version` aborts over SSH (needs a display); the app now runs it with `QT_QPA_PLATFORM=offscreen`. The stock desktop switcher in the stock panel (between the launcher and pinned apps) is a stock panel widget, not Caelestia's.
- In Plasma 6.7.5 on the ASUS: `loginctl terminate-session` does not stop the systemd user units; the ksmserver logout call fails; `loginctl lock-session` doesn't visibly lock (`Meta+L` does).

## 7. Next steps, in order

1. **Discuss the Phase A3 approach (switch core: `on` / `off`) before any code.** The user is open to changing the plan if discussed first. Points to settle: the step list and state-file format (spec "Order of operations", steps are checked one by one and the state file is written after each); the graceful quit (`quickshell kill` needs the session's `WAYLAND_DISPLAY`; find the instance by path or via `quickshell list`); the KWin Overview effect reload; the live dependency pre-flight before masking plasmashell (D1); helper units (D14); where the automatic snapshot-on-leave happens; what the CLI does when run from inside the outgoing shell (survive-the-switch, D5); the logout call and the "save your work" warning; `repair`. Checks to include: the lock screen in stock mode, plasmashell started without panels in Caelestia mode, `off, on, off, on` across real logouts, and a switch interrupted by killing the app.
2. Cleanup already applied in this commit: `plasmashell --version` offscreen lookup (fixes the "plasma unknown" label and the slow backup tests on the ASUS).
3. Then A4 (GUI), A5 (install/update/uninstall with source picker), A6 (soak). Fork track (F0, F1) comes after.

## 8. Open or unverified

- Restored config being overwritten on exit: **not observed** in the manual A2 round trip (2026-10-02); re-check under the automated switch in A3.
- Whether a restored `kwinrc` is picked up by KWin at the next login in every case (the Overview corner needed an effect reload after the clean quit); checked in A3.
- Survive-the-switch from stock Plasma's launcher (Phase A3); proposed safety net: the app checks its own cgroup at startup and re-executes itself in its own scope if needed.
- Fork track F0 is not started: README pin note, branch strategy (proposed: one branch per phase merged into `main`), repoint `~/caelestia-kde` `origin` to the fork when the fork first differs, source marker, whether design docs also go in the fork under `docs/fork/`.
- Known non-blocking issues from the original tests (Notification.qml null errors, binding loop in `bar/popouts/Content.qml`, broken native Share Region recording, `Meta+V`/`Meta+Ctrl+S` not toggling closed, higher memory than stock Plasma) are in the test progress log.

## 9. How the user wants to work

- **The plan is not fixed:** he doesn't mind changing the architecture or plan when there is a good reason, but wants it discussed first and then decided together.
- **One instruction at a time when output is needed:** give the instruction, then stop and wait for his output before the next step.
- Before changing code, **check the latest repo state** (clone/fetch); if a file or repo can't be accessed, say so explicitly and never proceed silently without it.
- For complex issues, **discuss the approach before writing code.**
- Output a file **only if it is new or modified.** Several changes to the same file in one response: one consolidated pass (full file or full modified section). A single isolated fix under 10 lines: show the snippet inline with the file path, no file output.
- When describing tasks: say what and why, not how. List only files to touch, functions affected, and non-obvious gotchas. No full code blocks (at most a one-line pseudocode hint). Point to existing patterns instead of new boilerplate. File-level steps, under 10 lines per task unless necessary.
- He pastes raw terminal output. On the ASUS the shell is **fish** (bash heredocs fail; `$(...)` works). Commands run over SSH from the MSI. Only one question per reply where possible.

## 10. Document inventory

| File | Status |
|---|---|
| `docs/Caelestia_KDE_Fork_Architecture.md` | Current (decision log D1–D18; D4 resolved, D11 narrowed) |
| `docs/Caelestia_KDE_Fork_Roadmap.md` | Current (app track A0–A6, fork track F0–F1; A0 and A1 marked done) |
| `docs/Caelestia_KDE_Switch_App_Spec.md` | Current (readings, switch rules, order of operations, backups, detection) |
| `docs/Caelestia_KDE_Testing_Progress_Handover_updated.md` | Test log of the original caelestia-kde tests; append-only |

**The Project's attached copies may be older than the repo.** The repo's `docs/` folder holds the latest; clone the repo at the start of the next session (or upload the three design docs plus this file to the Project).

## 11. Notes on Claude's sandbox

- It can clone the public repos (github.com is reachable) but cannot push; it has no access to the user's machines.
- It has Ubuntu's Qt 6.4.2 for compile checks (install `cmake`, `qt6-base-dev`, `ninja-build`; move the `nodesource` apt source aside first, it is blocked). There is no KF6 dev package: a stub `KF6::ConfigCore` (fake `KConfig`/`KConfigGroup` headers plus a fake `KF6ConfigConfig.cmake` via `CMAKE_PREFIX_PATH`) lets the rest compile and the unit tests run, but the ASUS build is the real check. It has no systemd user bus, so `status` there reports "cannot query systemd" (exit 3).
- It should start the next session by cloning `Caelestia-Switch`, reading `docs/`, and confirming the repo's `main` commit matches Section 2.
- It delivers new/modified files as a `.tar.gz` that keeps repo paths; the user unpacks it into the repo on the MSI with `tar xzf <file> -C <repo>`, builds as a compile check, commits and pushes; then on the ASUS: `git pull --ff-only` and build.
