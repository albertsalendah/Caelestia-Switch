# Caelestia KDE Switch App & Fork — Architecture & Design Decisions

*Revised 2026-10-02 (D4 resolved, D11 narrowed, D15 amended, D16 build note, D17-D19 added, D1 live finding). Supersedes the earlier "fork-first" version of this document.*

## What this project is now

Two separate things, in this order:

1. **A standalone switch app** (working name `caelestia-switch`) that switches between stock KDE Plasma (`plasmashell`) and Caelestia KDE without uninstalling either. It must work with an **unmodified** `ladybug-me/caelestia-kde` install.
2. **Later, the user's own modified version of caelestia-kde** (the fork). The app will be able to install, update and uninstall either source.

The fork lives at `github.com/albertsalendah/caelestia-kde`. It is currently an unmodified copy of upstream `v2.5.0` (`be4188f3c6d2f1f5212981818f692ec53e87c1b3`, 2026-09-23; `main` == `be4188f`, tags `upstream-base`, `v2.5.0`, `v2.5.1`). Its `dev` branch mirrors upstream's `dev` and carries no changes of ours.

Upstream's own architecture (Quickshell-based shell, native KWin bridges, the KWin workspace-tracker effect, the lock-screen greeter delivered through KDE's Plasma shell-package mechanism, notification/tray/OSD modules) is used as-is. Neither the app nor the fork reimplements any of it.

## Decision log

### D1 — Masking plasmashell lives in the app, not in the installer (REVISED)

**Decision:** the app masks/unmasks `plasma-plasmashell.service` when switching. The fork's installer does **not** get a masking step.

**Why:** the original plan baked masking into the fork's installer, but the switch app's `on` step already masks, so the installer step would have done the same job twice. Keeping it in the app means an unmodified ladybug-me install works with the app, and the fork stays purely customization. The boot-time behavior that Test 1 proved (plasmashell masked before login, clean ~6–7 s KWin-to-Caelestia handoff, two independent reboots) is unchanged; only who sets the mask differs.

**Pre-flight rule (amended 2026-10-02, decided with the user):** the dependency check runs live in the app before every mask, not hardcoded from the test result. One query of `plasma-plasmashell.service` (`RequiredBy`, `RequisiteOf`, `BoundBy`, `WantedBy`), split in two:
- **Strong** = `RequiredBy`, `RequisiteOf`, `BoundBy`. Masking would break the dependent, so the app **refuses** (`PartOf=graphical-session.target` is one-directional and ignored). An answer that cannot be read or understood counts as strong.
- **Weak** = `WantedBy`. Masking is **allowed** with a warning that names the units. Reason: in a logged-in session `plasma-plasmashell.service` has `WantedBy=plasma-core.target`, and Test 1 (two masked boots) showed that this does not stop a masked plasmashell. The original strict rule (all three empty) therefore always refused (A3a live finding, 2026-10-02).

The check runs in the core (`checkMaskPlasmashell`, `src/core/switch.cpp`): in the pre-flight, again in step 6 right before the mask (the click-time answer is advisory only), and by the CLI for an early note. **GUI behaviour (Phase A4, decided 2026-10-02):** the window runs the same check at load, so the "also disable plasmashell" checkbox starts greyed out with the blocking units shown when there is a strong dependent; with only weak dependents the user gets a warning that names the units, explains why masking is still fine, and is asked whether to proceed (with a "don't ask again" option, because `WantedBy=plasma-core.target` will appear every time). The executor itself never asks anything. Only plasmashell is checked: Caelestia is disabled, never masked (D12). A KDE update could change the relations, which is why the check stays live.

### D2 — The wallpaper mirror call: leave as-is (RESOLVED by D10)

`shell/services/Wallpapers.qml` fires a fire-and-forget `qdbus6 org.kde.plasmashell` call on every wallpaper change. It fails silently and harmlessly when plasmashell is masked (confirmed in Tests 2B/13), and works when plasmashell runs (headless or stock). Since the fork is unmodified and the app owns masking, there is nothing to change. Revisit only if a later fork customization touches wallpaper handling.

### D3 — The installer's konsave backup is not relied on by the app (REVISED)

Upstream's `00-backup-themes.sh` still runs as an unconditional install step and writes `<install checkout>/backups/<timestamp>/` (a `.knsv` konsave archive plus `previous_lookandfeel.txt`; packaged installs use `~/.cache/caelestia-kde/backups/`; the latest path is recorded in `~/.cache/caelestia-kde/backup-dir.txt`; `backups/` is git-ignored). It restored the stock Plasma panels correctly on the 2026-09-30 uninstall.

The app does not depend on it, for two reasons: it only covers the stock side, and after the 2026-09-30 clean reinstall only the newest backup remained in `backups/` (whether the uninstall removed the older ones or they were deleted is unconfirmed, so the original pre-Caelestia snapshot may be gone). The app takes its own snapshots (D11).

### D4 — Screen-edge and shortcut restore: RESOLVED 2026-10-01 (see D18)

**Original claim:** `screenedges.cpp` writes `~/.config/caelestia/stolen-screen-edges.json` and nothing reads it back; `uninstall.sh` has no matching lines.

**Re-scoped 2026-09-30:** `screenedges.cpp` restores on its own (`restoreAll()` on exit, `recoverFromCrash()` on start; Test 13 logged "Crash recovery: restoring screen corner 7"). The same holds for shortcuts (`globalshortcut.cpp`, `stolen-shortcuts.json`). The open question was a clean stop, which is what the app's switch does.

**Result (2026-10-01, ASUS):** a plain `systemctl --user stop caelestia-shell.service` does **not** release anything (both `stolen-*.json` files stay, the stolen kwin shortcuts stay `none`, `BorderActivate=9` stays). That is the Test 14 breakage. A graceful quit of quickshell does release everything. So no backup of this runtime state is needed: the switch must quit Caelestia gracefully (D18), and the backups (D11) leave edges and shortcuts out.

### D5 — Separate, standalone app, independent of both shells

The app is its own binary with its own `.desktop` entry, reachable from both Caelestia's launcher and stock KDE. It cannot live inside `caelestia-shell.service`: switching off stops the whole Caelestia process tree.

**Checked 2026-09-30 (Phase A0):** KCalc launched from Caelestia's launcher ran in its own scope (`app.slice/app-KDE-kcalc-….scope`), not inside `caelestia-shell.service` (which lives in `session.slice` with `KillMode=control-group`), so stopping the service does not kill apps started from the launcher. Not yet checked from stock Plasma's launcher; do that in Phase A3 when the app runs in stock mode. Proposed safety net: at startup the app reads its own cgroup and, if it sits inside a shell's service, re-executes itself in its own scope (`systemd-run --user --scope`).

### D6 — Privilege separation

`status`, `backup`, `restore`, `on` and `off` never need elevation (`systemctl --user` and config-file operations only). `install`, `update` and `uninstall` do (package management via `pacman`, matching `setup.sh`'s `caelestia_sudo` pattern) and prompt explicitly. The toggle is used casually and often; an authentication prompt on every use would defeat the point.

### D7 — State file for idempotency and crash safety

A small state file is the source of truth for which mode the system is in, updated after each step of a switch, not only at the end (see the spec). Motivated by Test 16.5: a session-restart attempt left the login session ended but the systemd user units still running, and diagnosing it took manual investigation. A switch has the same failure shape if interrupted (mask/unmask, stop/start, config restore, logout).

### D8 — Manual-triggered update, not automatic

`update` reuses upstream's detection idea (compare `.current_commit` with the tracked branch via `git ls-remote`) but requires an explicit confirmed action to pull and reinstall. No silent background updates: an automatic pull could clobber fork customizations.

**Constraints found 2026-09-30:**
- `src/bin/caelestia-check-updates` and `shell/services/UpdateChecker.qml` hardcode `https://github.com/ladybug-me/caelestia-kde.git`, and `update.sh` and the checker only accept the branches `main` and `dev`.
- `install.sh` defaults `CAELESTIA_REPO` to the same upstream URL. Run from outside a checkout while `~/caelestia-kde` exists, it does `git pull --ff-only` from that checkout's `origin`; run from inside the checkout, it goes straight to `setup.sh` with no pull.
- Release-binary downloads in `scripts/08-build-shell.sh` and `scripts/setup.sh` also point at upstream.

The app's install/update must let the user choose the source (upstream or fork) and must not rely on these defaults.

### D9 — Fork base version: v2.5.0 (in use); v2.5.1 as a later, deliberate step

Upstream released `v2.5.1` on 2026-09-29 (149 commits past `be4188f`). All test evidence was measured on v2.5.0, so the fork and the test laptop stay there until the app exists. Moving to 2.5.1 is its own step, followed by a smoke subset: masked boot (x2), wallpaper under Condition B, lock screen and self-heal, screen edges including a clean stop, launcher, notifications.

Known 2.5.1 difference: `scripts/09-system-tweaks.sh` gains a first-install guard, so panel removal and the 5-desktop setup only run when kwinrc has no `Desktops` key.

**Status 2026-09-30:** fork `main` and the test laptop's checkout are both at `be4188f`; a clean uninstall/reinstall baseline passed (see the progress file).

### D10 — App first; unmodified upstream installs must work (NEW)

The app's job is switching and lifecycle management. It must not require any change to caelestia-kde, so it works with a plain ladybug-me install today and with the fork later. Anything the app needs from the install (for example a source marker, see the spec) is either recorded by the app itself or added to the fork later as a small, isolated change.

### D11 — Two-sided backups, snapshot on leaving a mode (NEW, contents decided 2026-09-30)

The app keeps timestamped backups for both the stock-Plasma side and the Caelestia side, offered in a dropdown. It snapshots the mode being left automatically at every switch, so restoring never overwrites newer changes. When no Caelestia backup exists, the UI first offers only a Backup button.

**Why Caelestia needs a backup at all:** switching off leaves Caelestia installed. What differs between modes is the KDE-side configuration the installer changed (panel config, `ShellPackage`, KWin edges, shortcuts, lock-screen config), so "restore Caelestia without reinstalling" means restoring that configuration.

**Contents (decided 2026-09-30, narrowed 2026-10-01 after the D18 tests):** so that plasmashell always returns to the way the user set it previously, backups cover what the installer writes *persistently*. Runtime state that Caelestia claims and releases itself (screen edges, stolen shortcuts) is not backed up, because restoring a snapshot of the claimed state would make Caelestia record that value as the "original" and never release it. Whole-file where only the shell writes the file, key-level (D11 split confirmed by the user 2026-10-01) where the file is shared.

- *Whole-file snapshots (shell-owned):* `plasma-org.kde.plasma.desktop-appletsrc` (panels, desktop wallpaper), `plasmashellrc` (`ShellPackage`), `kscreenlockerrc` (lock screen theme and wallpaper), and from `~/.config/caelestia/` only `cli.json`, `keybinds.json`, `shell.json` and `monitors` (never `stolen-screen-edges.json` or `stolen-shortcuts.json`, which are recovery files, nor caches).
- *Key-level snapshots (shared files), with "absent in the snapshot means delete" on restore:* only the groups/keys the installer changes, so unrelated settings are never touched. Found by diffing a stock reference (the 2026-09-30 konsave archive) against the live Caelestia files:
  - `kwinrc`: the `Desktops` group (`Number`, `Id_N`), the `Plugins` keys the installer sets (`kwin_workspace_trackerEnabled` and any bridge keys, exact list from the install scripts), and the `org.kde.kdecoration2` group.
  - `kwinrulesrc`: the groups `caelestia-opacity`, `caelestia-dialogs`, `caelestia-pip` and their names in `[General] rules`/`count` (the user's own rules stay).
  - `plasmarc` (`OSD` group, `Theme/name`), `kdeglobals` (widget style, color scheme, the generated `Colors:*` groups; exact list from the scripts), `plasmanotifyrc` (`Notifications/LoudnessChangedOSD`), `powerdevilrc` (`brightnessosd`), `kmixrc` (`ShowOSD`), `ksplashrc` (`KSplash/Engine`).
- *Not backed up:* `kglobalshortcutsrc` (Caelestia puts back what it took when it quits gracefully), the electric-border keys (`[Effect-overview] BorderActivate`), the auto-generated `[Tiling]` groups in `kwinrc`, Konsole profiles (not part of the shell mode), installed content (`~/.config/quickshell/caelestia/` including `.current_commit`, the lock screen shell package under `~/.local/share/plasma/shells/`, the unit files; unit enablement is handled by the switch rules) and anything system-level such as `/etc/sddm.conf` (the unprivileged app does not touch it).
- *To check in A2:* whether the installer also clears kwin `Switch to Desktop N` shortcuts (a diff after a round trip will show it); which `kdeglobals` keys really need restoring.
- *What "return to the way I set previously" means:* switching to plasmashell restores the snapshot taken when the user last left Plasma mode, not the original pre-Caelestia state. Older snapshots stay available in the dropdown.
- *Storage:* `~/.local/share/caelestia-switch/backups/<side>/<timestamp>/`, outside both shells' config. Each backup has a manifest recording the original path of every item (and, for key-level items, which keys), the Caelestia commit (`.current_commit`), the Plasma version and the date, so the dropdown has readable labels and the app can warn on a version mismatch.

### D12 — Switch semantics (NEW, confirmed 2026-09-30)

- **To plasmashell:** always stop and disable Caelestia (even with the checkbox unchecked, so two UIs never appear); unmask plasmashell if masked; restore the selected stock backup.
- **To Caelestia, checkbox checked:** start Caelestia, pre-flight then stop and mask plasmashell.
- **To Caelestia, checkbox unchecked:** start Caelestia and leave plasmashell running headless (upstream's default state); if plasmashell is masked or not running, unmask and start it.
- **The checkbox therefore only has an effect when switching to Caelestia.**
- **Caelestia is disabled, not masked** (`disable --now`). `caelestia-shell.service` is a regular file in `~/.config/systemd/user` written by `10-autostart.sh`, and masking would collide with that file. plasmashell's unit ships with the system, so masking works there (proven in testing). Checked 2026-09-30 (Phase A0): `systemctl --user disable --now caelestia-shell.service` removed the `graphical-session.target.wants` link, and the service stayed disabled and inactive across a logout and a fresh login; `enable --now` brought it back live, without a logout. Note: with Caelestia disabled and plasmashell still running headless, the desktop shows only the wallpaper and KDE's own right-click menu (plasmashell's desktop layer); the panels return only when the stock backup (panel config and `ShellPackage`) is restored.
- Stop Caelestia before resetting `ShellPackage`, because Caelestia's startup wrapper self-heals it back to `caelestia.desktop`.

### D13 — A logout is always required; graceful, with a warning (NEW, confirmed 2026-09-30)

Apply all changes first, then log out with `qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logout` (the call Caelestia's own Logout button uses; worked in Test 16.5; checked again 2026-09-30 from an SSH shell, outside both shells' cgroups, where it logged the session out to the login screen with no confirmation dialog). Do not use the ksmserver call (failed on Plasma 6.7.5) or `loginctl terminate-session` (leaves the systemd user units running, Test 16.5). The app shows a "save your work" warning and does not force-kill applications; the normal logout lets apps ask about unsaved work.

**Unverified; checked in the Phase A2/A3 backup-restore round trips rather than a separate test (decided 2026-09-30):** plasmashell may rewrite its config (for example the applets config, and possibly `kwinrc` for KWin) when it exits, which would clobber a config restore done while it was still running. The config swap must therefore happen after the outgoing shell has stopped and be verified after the next login.

**Result (2026-10-02, ASUS):** the restore path held. With plasmashell stopped first, a restore of the stock snapshot (panel config, `ShellPackage` unset, `kwinrc` `Desktops`) was still in place after starting plasmashell and after a real logout and login, and the same held for the Caelestia restore on the way back. KWin picked up the restored `kwinrc` at login (5 desktops back, 1 workspace on stock). Nothing overwrote the restored config on exit. Still to confirm in the automated switch (A3): the same under `on`/`off` including the helper units.

### D14 — Helper units follow the switch only when safe (NEW, confirmed 2026-09-30)

Caelestia-related units besides `caelestia-shell.service` include `cliphist.service` (clipboard-history watchers) and the update-checker timer/service. When switching to plasmashell, stop them only if a live check shows nothing depends on them (same pattern as the plasmashell pre-flight); otherwise leave them running. Start them again when switching to Caelestia. The KWin workspace-tracker effect is not a process and stays loaded in KWin.

### D15 — Source and version detection (NEW, decided 2026-09-30; version source amended 2026-10-01)

The install records `.current_commit` and `.update_branch` under `~/.config/quickshell/caelestia/`, and also `.current_version`, a copy of the checkout's `.github/version.env` (written by `scripts/lib/update-state.sh`; confirmed on the test laptop 2026-10-01). The earlier statement that only the commit and branch were recorded was wrong. No source repo is recorded. A commit hash alone cannot tell fork from upstream while their history is shared. The app therefore resolves the two readings separately, each in order:

**Source**
1. **App-written marker** (source, version, commit, checkout path), written whenever the app installs or updates. Ignored if its recorded commit differs from the current `.current_commit` (the install changed outside the app).
2. **Checkout discovery:** a checkout (`$CAELESTIA_DIR`, then `~/caelestia-kde`) whose `HEAD` equals `.current_commit`. The source is its `origin` URL (also where its updates come from). Works for an unmodified upstream install with no changes to it.
3. **Later:** the fork's installer writes its own marker (fork track, F0).
4. **Otherwise "unknown".** No first-run question to the user.

**Version**
1. The marker, if valid (as above).
2. `~/.config/quickshell/caelestia/.current_version`.
3. The discovered checkout's `.github/version.env`.
4. Otherwise "unknown".

So a moved or deleted checkout makes the source read "unknown" but the version stays known. Implemented and checked in Phase A1 (2026-10-01).

### D18 — Stopping Caelestia gracefully (NEW, 2026-10-01; resolves D4)

Checked on the ASUS on 2026-10-01 against the 2026-09-30 stock reference:

- `systemctl --user stop caelestia-shell.service` (SIGTERM; `TimeoutStopSec=5s`) does not run Caelestia's exit handler. Cleanup only runs on a clean Qt quit (`aboutToQuit` and destructors).
- `quickshell kill -i <id>` (id from `quickshell list --all`) quits cleanly: the service goes inactive without restarting, the kwin shortcuts are restored (`Overview`=`Meta+W`, `Log Out`, `Grid View`, `Walk Through Windows` and others), `stolen-screen-edges.json` is deleted and `BorderActivate` leaves `kwinrc`. `stolen-shortcuts.json` stays, containing an empty list; the app ignores or deletes it.
- quickshell matches instances per display. From a bare SSH shell `kill -p <shell.qml>` and `kill -i` both report "No running instances"; with `WAYLAND_DISPLAY=wayland-0` set it works. The app runs inside the session and inherits the display; tests over SSH must set it.
- After the clean quit the corner stayed dead until `qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect overview` was run (Caelestia's exit path only reloads effects that still have an edge key in `kwinrc`, and Overview has none after the restore). The switch ends in a logout, where KWin rereads its config anyway, so this call is a safety net that the app issues after the quit.
- **Handling:** switch-to-plasmashell quits Caelestia gracefully, waits until the unit is inactive (after a timeout, fall back to `systemctl stop`, then recovery as below), reloads the Overview effect, then `disable`. `repair` after a hard stop starts Caelestia and quits it gracefully once, which runs its own recovery (crash recovery proven in Test 13).
- **Still to verify in Phase A3:** the full off, logout, login result (shortcuts, corner, panels).

### D19 — The switch executor (NEW, decided 2026-10-02)

- **Separate process, survives the app (user decision):** the switch is run by `caelestia-switch run-switch` in a transient user service `caelestia-switch-run` (`systemd-run --user --collect`), in its own cgroup, so stopping either shell cannot kill it; the app and CLI only start it and watch the state file. This also replaces the D5 "re-execute in a scope" safety net. A `QLockFile` allows one switch at a time.
- **Steps (each checked, state file updated after each):** 1 pre-flight (consistent state, target backup exists, live dependency check before masking); 2 automatic snapshot of the side being left; 3 stop the shells (both directions: Caelestia is quit gracefully with an Overview-effect reload when leaving Caelestia, D18; plasmashell is always stopped, even headless, because it may rewrite its config on exit, D13); 4 helper units (`cliphist.service`, best effort, only disabled when nothing requires or is bound to it, and remembered in the `helpers` file); 5 restore the target backup; 6 unit changes (disable or enable Caelestia, mask or unmask plasmashell); 7 verify the configuration (unit file states, `ShellPackage`, mask state); 8 `pending-logout` then log out over D-Bus (`org.kde.Shutdown`). A failed step stops without logging out and leaves `mode=transitioning` with `error` and `result=failed`.
- **Mutating unit actions use `systemctl --user`** (the commands proven in the manual round trips); reads use D-Bus (D17). The executor takes the session's `WAYLAND_DISPLAY` from its environment, else from `systemctl --user show-environment`, else from the sockets in `XDG_RUNTIME_DIR` (needed by `quickshell kill`, D18).
- **After the login:** `finish` waits for the readings to match the expected final state and closes out the state file (`mode` becomes `stock` / `caelestia`, `result=done`, `unseen=true`). The automatic trigger (a small user service `WantedBy=graphical-session.target` that runs `finish` and opens the app with the "switch complete" message, plus a desktop notification as a fallback) is the next batch.
- **Helper units:** only `cliphist.service` is handled for now. The update checker is not a systemd unit in the installer scripts; `ydotoold.service` is left alone.
- **Warning text before a switch** is in the spec (user wording, 2026-10-02).
- **Snapshot side and mask-only changes (decided and implemented 2026-10-03):** found by reading the executor before the first mask test: the snapshot side followed the direction, so `on --mask` from Caelestia mode would have stored the live Caelestia config as the newest *stock* backup (a later `off` restores it and fails verification) and would have restored a backup into the running Caelestia. Now (a) the side being left comes from `Readings.provider` (Caelestia / stock; none = no snapshot, with a warning, never a guess), and (b) when Caelestia is already running and only the mask differs (`SwitchPlan::unitsOnly`), steps 2, 3 and 5 are recorded as skipped and only the unit changes, verification and logout run; no backup is needed and `--backup` is refused. Unit-tested (20 cases in `test_switch`); live test pending.
- **Live results, mask path (ASUS, 2026-10-03):** from stock mode, `on --mask --wait` printed the weak-dependent note (`WantedBy=plasma-core.target`), ran steps 1-8, logged the matching warning once, and after login plasmashell was `masked (inactive/dead)` and not running with Caelestia active (bar, wallpaper, workspaces fine; `finish` closed out the state). `off --wait` from that masked state (unmask path, step 6 shown) returned stock: panel, wallpaper, `Meta+W`, `Meta+L` (Plasma's own), plasmashell running and not masked. Not yet live: the mask-only path above, a switch interrupted by killing the app, the post-login automatic `finish`, `repair`.
- **Live results (ASUS, 2026-10-02):** `off --no-logout --wait` ran steps 1-8 from inside the transient service (the graceful quit worked there, with the session display; about 6 s for the quit and the plasmashell stop); then a full `off --wait` with the automatic logout, `finish`, and a stock session with panel, wallpaper, `Meta+W`, corner, 1 workspace and Plasma's own lock screen; then a full `on --wait` back (Caelestia bar, `Meta+W` = Caelestia's Launch Browser, corner, 5 workspaces, Caelestia lock screen, `cliphist` re-enabled from the `helpers` file). The mask path and `off` from a masked state were run live on 2026-10-03 (next bullet up); still not live: a switch interrupted by killing the app, the post-login automatic `finish`, `repair`.
- **Plasma version label:** `plasmashell --version` crashes (core dump) when run by the app, so the Plasma version is read from `/usr/lib/cmake/LibKWorkspace/LibKWorkspaceConfigVersion.cmake` (`set(PACKAGE_VERSION "6.7.5")`); plasmashell's own `metadata.json` has no version.

### D17 — Status implementation choices (NEW, Phase A1, 2026-10-01)

- **Unit state over D-Bus, not `systemctl`:** the user systemd manager is queried on the user bus with `LoadUnit` (so masked and not-installed units still answer) and `Properties.GetAll`; 3 s call timeout. If `DBUS_SESSION_BUS_ADDRESS` is unset (common over SSH), the standard `/run/user/<uid>/bus` socket is used and Qt never tries to autolaunch a bus.
- **Process scan:** `/proc/<pid>/status` (name, uid, state), own user only, zombies ignored, for `plasmashell` and `quickshell`.
- **Bar/panel provider rule:** Caelestia if `caelestia-shell.service` is active or a quickshell process exists; Plasma if plasmashell runs and `ShellPackage` is unset or `org.kde.plasma.desktop`; both is Inconsistent; otherwise none. Panel config (`appletsrc`) is not read: a headless plasmashell with `ShellPackage=caelestia.desktop` and Caelestia off reads as "none", which is valid (the panels-missing state).
- **Inconsistent** = plasmashell masked but running; Caelestia and stock panels both active; neither shell running; the state file says `transitioning` or `pending-logout`; systemd cannot be queried (reported as such, not guessed).
- **`status`** exits 0 even when Inconsistent (a reading, not a failure) and 3 if systemd cannot be queried; `--json` prints the same readings as JSON. Because the service turns `active` before `quickshell` has started, a reading taken right after `start` can show the process as stopped for a moment; the provider rule uses the service state, so it is unaffected.
- **State file:** read leniently for now (first word, optional `mode=` prefix); the real format is fixed in Phase A3.

### D16 — Toolkit: C++ with Qt6 Widgets and KF6 (NEW, decided 2026-09-30)

**Decision:** the app is written in C++ with Qt6 (Core, Widgets, DBus) and KF6 (ConfigCore), built with CMake. Structure: a core library (detection, backup/restore, switch logic), a CLI on top of it, and a thin Widgets GUI that only calls the core. The detection, backup and switch phases can therefore be built and tested over SSH before any window exists.

**Why:** Qt6 and KF6 are already installed on both laptops, so no extra runtime packages; the app is small and starts fast on the 4 GiB / A4-9125 test laptop; `KConfig` copies nested groups of `kwinrc`, `kglobalshortcutsrc` and the other files faithfully, which the group-level backups (D11) need, and it is the same library `screenedges.cpp` uses; QtDBus covers the `org.kde.Shutdown` logout call (D13); upstream's own plugins and installer are also C++.

**Rejected:** Python with PySide6 (large extra dependency, slower start and more RAM on the A4, no `KConfig` group support, so key-by-key `kwriteconfig6` calls or hand-parsing KDE's INI quirks); Qt Quick/Kirigami (heavier on the Radeon R3, more machinery for three simple screens; can be revisited later since the core is separate); Flutter (GTK-based on Linux desktop, ignores KDE theming, heavy, no `KConfig`).

**Build notes (amended 2026-10-01):** build on the ASUS itself (clone, `git pull --ff-only`, `cmake --build build -j2`; about 10 s for the skeleton, longer with the A1 code but fine). The MSI build is a compile check only; both have Qt 6.11.2 and KF6 6.30.0. Do not build with `-march=native`. Unit tests run with `ctest --test-dir build --output-on-failure` (QtTest).

**Repository (decided 2026-09-30):** the app lives in its own repository (working name `caelestia-switch`), separate from the caelestia-kde fork, because it must work with upstream and the fork alike (D10). The initial project skeleton (CMake, core library, CLI and GUI stubs) builds with Qt6.

## Open decisions

- Still unverified: config rewrite on exit (D13), checked during Phases A2/A3; and the survive-the-switch check from stock Plasma's launcher (D5), during Phase A3.

## Non-goals (for now)

- **Customization of caelestia-kde.** Deferred to the fork track in the roadmap.
- **Multi-machine support.** Everything is validated on one test machine (ASUS X441BA).
- **Upstreaming fixes.** Considered later, separately from this project's roadmap.
