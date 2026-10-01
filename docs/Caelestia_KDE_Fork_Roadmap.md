# Caelestia KDE Switch App & Fork — Roadmap

*Revised 2026-10-01 (A1 done). The plan is now app-first; the fork is a separate, later track.*

## Goal

1. Build a **standalone switch app** that switches between stock KDE Plasma (`plasmashell`) and Caelestia KDE without uninstalling either, working with an unmodified `ladybug-me/caelestia-kde` install (see `Caelestia_KDE_Switch_App_Spec.md`).
2. Then build **the user's own modified version** of caelestia-kde (the fork at `github.com/albertsalendah/caelestia-kde`), which the app can also install, update and uninstall.

The plasmashell-masking step is part of the app (architecture doc D1), not of any installer.

## Where this starts from (not zero)

- Full source audit of `caelestia-kde` at commit `be4188f` (2026-09-23, = upstream tag `v2.5.0`) is complete.
- All 17 planned test items (Test 1 through Test 16.6) passed on the test laptop (ASUS X441BA, CachyOS, Plasma 6.7.5, 4 GiB RAM), including two independent reboots with `plasma-plasmashell.service` masked before login.
- **2026-09-30:** clean uninstall (with the installer's konsave backup restore) returned stock Plasma including `Meta+W`; a clean reinstall from the checkout at `be4188f` passed a baseline check (Condition A: Caelestia active, plasmashell headless and not masked, `ShellPackage=caelestia.desktop`). The test laptop is currently in that state.
- **2026-10-01:** Phase A1 is done: `status` works and passed its live test matrix on the test laptop (see Phase A1).
- The fork is an unmodified copy of `v2.5.0` (`main` == `be4188f`). Upstream has since released `v2.5.1` (149 commits ahead); moving to it is a separate step (architecture doc D9).

## App track

### Phase A0 — Risk checks on the test laptop (done 2026-09-30; one item deferred)

1. **Survive the switch: PASS from Caelestia's launcher.** KCalc launched from Caelestia's launcher ran in `app.slice/app-KDE-kcalc-….scope`, not inside `caelestia-shell.service` (`session.slice`, `KillMode=control-group`). Repeat from stock Plasma's launcher in Phase A3.
2. **Config rewrite on exit: folded into Phases A2/A3.** The switch order stops the outgoing plasmashell before restoring its config, and the app verifies the restored keys after the next login; a contrived separate test would show little.
3. **`disable --now` on `caelestia-shell.service`: PASS.** It removed the `graphical-session.target.wants` link; the service stayed disabled and inactive across a logout and a fresh login; `enable --now` restored it live.
4. **Logout call: PASS.** `qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logout` run from an SSH shell logged the session out to the login screen with no confirmation dialog.

Also observed: with Caelestia disabled and plasmashell headless, the desktop shows only the wallpaper and KDE's own right-click menu (plasmashell's desktop layer). The panels return only when the stock config is restored.

**Done when:** each assumption is confirmed or the design is amended with what actually happens.

### Phase A1 — Status detection (`status`) — done 2026-10-01

The project skeleton is set up in its own repository (`caelestia-switch`: CMake, core library, CLI and GUI stubs; the GUI is filled in during Phase A4). Implement the readings from the spec. Read-only.

**Result (2026-10-01):** implemented as `caelestia-switch status [--json]` (architecture D17) with 21 QtTest cases, built on the MSI and ASUS. Live matrix on the ASUS, all as expected: Condition A (provider Caelestia, ladybug-me, v2.5.0, ok); plasmashell masked but running (Inconsistent); masked and stopped (ok); Caelestia disabled and stopped with plasmashell headless (provider none, ok); checkout moved away (source "unknown", version still v2.5.0). Finding: the installer also writes `.current_version`, so the version no longer depends on the checkout (D15 amended). The baseline was restored afterwards.

**Done when:** `status` reports correctly on the current Condition A install (including source and version, for example ladybug-me and v2.5.0), after manually masking plasmashell, after manually disabling Caelestia (stock), and flags a deliberately created conflict (for example plasmashell masked but running) as Inconsistent. Source/version detection follows the spec (marker, checkout discovery, else "unknown"); check that moving the checkout away yields "unknown" rather than a wrong answer.

### Phase A2 — Backup and restore

Timestamped two-sided snapshots, the dropdown listing, and the automatic snapshot-on-leave, with the contents decided in the architecture doc (D11): shell-owned files whole, shared files and look-and-feel keys at group level, plus a manifest per backup.

**Done when:** a backup, change, restore round trip on the test laptop brings back stock panels, wallpaper, `Meta+W`, the Overview corner, and Caelestia's own settings, verified against the Test 14 symptoms. Also verify the clean-stop screen-edge case that Test 13 did not cover (D4).

### Phase A3 — Switch core (`on` / `off`)

The switch rules, helper-unit handling, state file, `repair`, and the logout step.

**Done when:** off, on, off, on (with the checkbox both checked and unchecked, and across real logouts) leaves the system in the expected state every time: no drift, no leftover broken shortcuts, no orphaned processes, no two UIs at once. Interrupting a switch mid-way (killing the app) is recoverable via `repair`.

### Phase A4 — GUI

Loading screen, Screen A (backup needed), Screen B (switch, dropdown, checkbox), the "save your work" warning, and the `.desktop` entry reachable from both launchers. Built with C++, Qt6 Widgets and KF6 (architecture doc D16); the GUI only calls the core library.

**Done when:** the whole flow works from the GUI in both Caelestia and stock Plasma on the test laptop.

### Phase A5 — install / update / uninstall with a source picker

Privileged operations layered on the core: install from upstream or the fork, manual update, uninstall (running the restore first). Must not rely on upstream's hardcoded repo defaults (D8).

**Done when:** each source installs cleanly from stock, switches, and uninstalls back to stock Plasma.

### Phase A6 — Reliability soak test

Repeated switches across real reboots, deliberate mid-switch interruption, and a memory check across several cycles (Test 16 showed memory after a shell restart did not return to the pre-interruption baseline).

**Done when:** confident enough to use the app daily without checking `journalctl` after every switch.

## Fork track (later)

### Phase F0 — Housekeeping

- README fork notice with the pin note (`v2.5.0`, `be4188f`).
- Branch strategy. Proposed, not confirmed: one branch per phase merged into `main`; the fork's `dev` mirrors upstream's `dev` and the update tooling only accepts `main`/`dev`.
- Repoint the test laptop checkout's `origin` from `ladybug-me` to the fork when the fork first differs.
- A source marker that the app can read, once the fork exists as more than a copy (spec, Open item 2).
- Decide whether the design docs live in the fork (`docs/fork/` proposed, to avoid clashing with upstream's `docs/architecture/`).

### Phase F1 — Customization

Open-ended. Natural split: visual, functional/feature-trimming, and hardware-tuned (for example the animation stutter and higher memory footprint seen in Test 15). Fixes for the issues logged in the progress file (`Notification.qml` null errors, the `bar/popouts/Content.qml` binding loop, Share Region recording) are candidates.

## Open risks

- **Single test machine.** Every phase depends on the ASUS X441BA; no parallel hardware to catch machine-specific assumptions.
- **Upstream drift.** Upstream is at v2.5.1, 149 commits past the pin, and keeps moving. The app's detection and backup assumptions may need re-checking after each upstream update.
- **Detection ambiguity.** Source and version are not recorded by the installer, so "unknown" must be a first-class result.
- **Unverified assumptions** listed in Phase A0.
