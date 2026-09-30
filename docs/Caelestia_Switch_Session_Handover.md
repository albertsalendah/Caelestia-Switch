# Caelestia Switch — Session Handover

*As of 2026-10-01. Written so a fresh conversation can continue without re-deriving anything. Read this first, then the design docs (Section 10).*

## 1. The project in one paragraph

Build **`caelestia-switch`**, a standalone C++/Qt6 Widgets/KF6 app that switches between stock KDE Plasma (`plasmashell`) and **Caelestia KDE** (`ladybug-me/caelestia-kde`, a Quickshell-based shell) without uninstalling either. It must work with an **unmodified** upstream install. Masking plasmashell lives in the app, not in any installer. Later, the user builds his own modified fork of caelestia-kde, and the app's install/update/uninstall can pick upstream or the fork. The earlier "fork-first" plan was replaced on 2026-09-30.

## 2. Where things live

| Thing | Location |
|---|---|
| App repo (source of truth for code and current docs) | `https://github.com/albertsalendah/Caelestia-Switch.git`, `main` at `96a9ce1` (skeleton + updated docs) |
| Fork of caelestia-kde (unmodified, for later) | `https://github.com/albertsalendah/caelestia-kde.git`, `main` == `be4188f` (= upstream `v2.5.0`); its `dev` mirrors upstream `dev`, no changes of ours |
| Upstream | `https://github.com/ladybug-me/caelestia-kde.git`; `v2.5.1` released 2026-09-29 (149 commits past `be4188f`); the fork and tests deliberately stay on v2.5.0 |
| Main dev laptop | MSI GL63 8RE, CachyOS + KDE, 16 GiB. Project folder `/mnt/MainData/AppProjects/caelestia-switch`. Commits and pushes happen here (SSH + `gh` set up; git identity is set per repo) |
| Test laptop | ASUS X441BA, CachyOS, Plasma 6.7.5, KF 6.30.0, Qt 6.11.2, 4 GiB RAM, AMD A4-9125, **fish shell**, user `asus`. The user works on it over SSH from the MSI |
| App clone on the ASUS | `~/Caelestia-Switch` (https clone, **pull only**: `git pull --ff-only`, then build). Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2` (~10 s) |
| Caelestia install on the ASUS | installed from the checkout `~/caelestia-kde` (origin still `ladybug-me`, `HEAD` `be4188f`) |

## 3. Current state (what is true right now)

- **ASUS:** Caelestia is installed and running: `caelestia-shell.service` enabled and active; plasmashell running headless (no panels), **not masked**; `ShellPackage=caelestia.desktop`; lock screen greeter installed. A clean uninstall (unmask plasmashell, then `uninstall.sh` with the backup-restore option) and reinstall were done on 2026-09-30, and the baseline check passed.
- `~/caelestia-kde` has one local uncommitted edit: `installer/data/menu.json` ("Custom lockscreen" default set to `false`; deliberate, local only, not in the fork). Its `backups/` folder now holds only `20260930_115125`; the original pre-Caelestia backup may be gone (cause unconfirmed).
- **App repo:** a working skeleton. CMake project with `src/core` (version stub only), `src/cli` (`caelestia-switch`: prints version/help; any other command exits 2 with "not implemented"), `src/gui` (`caelestia-switch-gui`: a window showing the version), `data/*.desktop`, `docs/`. Builds on the MSI (Qt 6.11) and ASUS; the GUI window opened on the MSI (not run on the ASUS). KF6 is **not** linked yet.
- `docs/` in the repo holds six docs: the three current design docs plus the three original test docs. Check which copy of the progress log is there (the Phase 0 baseline section was added in a later copy).

## 4. Done so far

- Source audit and all 17 original tests of caelestia-kde at `be4188f` passed (Conditions A and B, two masked-boot reboots).
- Design docs rewritten app-first; decisions D1–D16 recorded (Section 5).
- **Phase A0 (risk checks) done 2026-09-30:** see Section 6.
- Repo created, skeleton pushed, built on both laptops.

## 5. Key decisions (full text in the architecture doc)

- **D1/D10:** masking is done by the app, not an installer; the app must work with unmodified upstream installs. Live pre-flight before every mask: `RequiredBy`, `WantedBy`, `BoundBy` of `plasma-plasmashell.service` must be empty.
- **Switch rules (D12):** to plasmashell: unmask it if masked, **always** stop and **disable** Caelestia (`disable --now`, not mask: the unit is a regular file in `~/.config/systemd/user`), restore the stock backup. To Caelestia: start it, restore the Caelestia backup; checkbox checked = stop and mask plasmashell, unchecked = leave plasmashell running headless (unmask and start it if needed). The checkbox only matters when switching to Caelestia.
- **Logout (D13):** always required, done last via `qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logout`; show a "save your work" warning; never force-kill apps. Do the config swap after the outgoing shell has stopped.
- **Helper units (D14):** stop `cliphist.service` and the update-checker units with Caelestia only if a live check shows nothing depends on them.
- **Backups (D11):** two-sided, timestamped, snapshot of the mode being left taken automatically; stored in `~/.local/share/caelestia-switch/backups/<side>/<timestamp>/` with a manifest. Whole-file: `plasma-org.kde.plasma.desktop-appletsrc`, `plasmashellrc`, `kscreenlockerrc`, `~/.config/caelestia/` (exclude `stolen-screen-edges.json`, caches). Group-level (only keys the installer/Caelestia change): `kwinrc`, `kglobalshortcutsrc`, and look-and-feel keys in `plasmarc`, `kdeglobals`, `plasmanotifyrc`, `powerdevilrc`, `kmixrc`, `ksplashrc`. The installer's own konsave backup is not relied on.
- **Detection (D15):** source/version from an app-written marker, else checkout discovery (`~/caelestia-kde` or `CAELESTIA_DIR`, `HEAD` == `.current_commit`; source = `origin` URL; version = `.github/version.env`), else show "unknown" (no first-run question).
- **Toolkit (D16):** C++ with Qt6 Widgets and KF6; core library + CLI + thin GUI. The D16 build note still says "build on the MSI and copy the binary"; it should now say "build on the ASUS (clone, pull, build), keep the MSI build as a compile check". Never use `-march=native`.
- **D4:** `screenedges.cpp` already restores edges on exit and crash recovery; the clean-stop case (what `off` does) is unverified. Re-test it, don't assume a gap.
- **D8/update:** `update` is manual only. Upstream's update checker and `update.sh` hardcode `ladybug-me/caelestia-kde` and accept only branches `main`/`dev`.

## 6. Verified facts (don't re-check)

- KCalc opened from Caelestia's launcher runs in its own scope (`app.slice/app-KDE-kcalc-….scope`), not in `caelestia-shell.service` (`session.slice`, `KillMode=control-group`). Not yet checked from stock Plasma's launcher.
- `systemctl --user disable --now caelestia-shell.service` removes the `graphical-session.target.wants` link; it stayed off across logout and a fresh login; `enable --now` restores it live.
- The logout call above works from an SSH shell and logs out with no confirmation dialog.
- With Caelestia off and plasmashell headless, the screen shows the wallpaper and KDE's own right-click menu: a headless plasmashell owns the desktop layer; panels come back only when the stock config is restored. When Caelestia runs, its right-click menu takes over.
- The install records only `.current_commit` and `.update_branch` under `~/.config/quickshell/caelestia/`. The version lives in `.github/version.env` in the checkout. No source repo is recorded.
- `caelestia-shell.service` is written by `10-autostart.sh` (`WantedBy=graphical-session.target`), starts via `~/.local/bin/caelestia-autostart.sh`, which rewrites `ShellPackage` to `caelestia.desktop`: stop Caelestia before resetting `ShellPackage`.
- `install.sh` run from inside a checkout goes straight to setup with no pull; run from outside with an existing `~/caelestia-kde`, it does `git pull --ff-only` from that checkout's `origin`.
- In Plasma 6.7.5 on the ASUS: `loginctl terminate-session` does not stop the systemd user units; the ksmserver logout call fails; `loginctl lock-session` doesn't visibly lock (`Meta+L` does).

## 7. Next steps, in order

1. **Get a yes/no from the user on the Phase A1 plan** (it was proposed and is awaiting his go-ahead). Plan, by file: `src/core/readings.h/.cpp` (readings struct + inconsistent reasons); `systemd.cpp` (unit active/unit-file state for `plasma-plasmashell.service` and `caelestia-shell.service` over the user systemd D-Bus, plus a `/proc` scan for `plasmashell` and `quickshell`); `plasmaconfig.cpp` (read `ShellPackage` from `plasmashellrc` with KF6 ConfigCore; bar/panel provider = Caelestia if its service is active, Plasma if plasmashell runs with the stock package, else none); `install.cpp` (installed = `shell.qml` and unit file exist; marker, then checkout discovery, then unknown; table mapping origin URLs to "ladybug-me"/"fork"); `consistency.cpp` (masked-but-running, Caelestia plus Plasma panels at once, neither shell running, state file says a switch is in progress); `src/cli/main.cpp` (`status`, with `--json`); `tests/` (QtTest for the `version.env` parser and the URL mapping). Defaults proposed: systemd over D-Bus (not spawning `systemctl`), `--json` option, KF6 added now (instead of Phase A2).
2. **Check KF6 Config dev files** on the ASUS and MSI (`pacman -Q kconfig`, and that `find_package(KF6Config)` resolves). Claude's sandbox cannot build against KF6.
3. Claude writes the A1 files and delivers only new/modified files; the user copies them into the repo on the MSI, commits and pushes; on the ASUS: `git pull --ff-only`, build with `-j2`.
4. **A1 test matrix on the ASUS:** current state (Caelestia active, plasmashell headless, not masked = valid); plasmashell stopped and masked (valid); masked but still running (must read Inconsistent); Caelestia disabled and stopped (stock-like, with the panels-missing caveat); checkout moved away (source must read "unknown", not a wrong answer). Restore the baseline afterwards (`systemctl --user unmask …`, `enable --now caelestia-shell.service`).
5. Doc touch-ups: D16 build note; spec readings if A1 changes them.
6. Then roadmap Phases A2 (backup/restore), A3 (switch core, plus the stock-launcher survive-the-switch check), A4 (GUI), A5 (install/update/uninstall with source picker), A6 (soak). Fork track (F0 README/pin/branches/source marker, F1 customization) comes after.

## 8. Open or unverified

- Whether plasmashell or KWin overwrite restored config on exit: checked in the A2/A3 backup-restore round trips.
- Survive-the-switch from stock Plasma's launcher (Phase A3); proposed safety net: the app checks its own cgroup at startup and re-executes itself in its own scope if needed.
- How to define "bar/panel provider" precisely (plan in step 1 above is a proposal).
- Fork track F0 is not started: README pin note, branch strategy (proposed: one branch per phase merged into `main`), repoint `~/caelestia-kde` `origin` to the fork when the fork first differs, source marker, whether design docs also go in the fork under `docs/fork/`.
- Known non-blocking issues from the original tests (Notification.qml null errors, binding loop in `bar/popouts/Content.qml`, broken native Share Region recording, `Meta+V`/`Meta+Ctrl+S` not toggling closed, higher memory than stock Plasma) are in the test progress log.

## 9. How the user wants to work

- **One instruction at a time when output is needed:** give the instruction, then stop and wait for his output before the next step.
- Before changing code, **check the latest repo state** (clone/fetch); if a file or repo can't be accessed, say so explicitly and never proceed silently without it.
- For complex issues, **discuss the approach before writing code.**
- Output a file **only if it is new or modified.** Several changes to the same file in one response: one consolidated pass (full file or full modified section). A single isolated fix under 10 lines: show the snippet inline with the file path, no file output.
- When describing tasks: say what and why, not how. List only files to touch, functions affected, and non-obvious gotchas. No full code blocks (at most a one-line pseudocode hint). Point to existing patterns instead of new boilerplate. File-level steps, under 10 lines per task unless necessary.
- He pastes raw terminal output. On the ASUS the shell is **fish** (bash heredocs fail; `$(...)` works). Commands run over SSH from the MSI. Only one question per reply where possible.

## 10. Document inventory

| File | Status |
|---|---|
| `docs/Caelestia_KDE_Fork_Architecture.md` | Current (decision log D1–D16). D16 build note needs the "build on the ASUS" update |
| `docs/Caelestia_KDE_Fork_Roadmap.md` | Current (app track A0–A6, fork track F0–F1; A0 marked done) |
| `docs/Caelestia_KDE_Switch_App_Spec.md` | Current (readings, switch rules, order of operations, backups, detection) |
| `docs/Caelestia_KDE_Testing_Progress_Handover_updated.md` | Test log of the original caelestia-kde tests; append-only; the Phase 0 baseline section exists in the latest copy |
| `docs/Caelestia_KDE_Testing_Handover.md`, `docs/Caelestia_KDE_Plasma_Handover.md` | Historical; unchanged |

**The Project's attached copies of these files are the old versions.** The repo's `docs/` folder holds the latest; clone the repo at the start of the next session (or upload the three current design docs plus this file to the Project).

## 11. Notes on Claude's sandbox

- It can clone the public repos (github.com is reachable) but cannot push; it has no access to the user's machines.
- It has Ubuntu's Qt 6.4.2 for Qt-only compile checks (install `cmake` and `qt6-base-dev`; move the `nodesource` apt source aside first, it is blocked). There is no KF6 dev package, so KF6 code can't be compile-checked there: the ASUS build is the real check.
- It should start the next session by cloning `Caelestia-Switch`, reading `docs/`, and confirming the repo's `main` commit matches Section 2.
