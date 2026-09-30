# Caelestia KDE Switch App & Fork — Architecture & Design Decisions

*Revised 2026-09-30. Supersedes the earlier "fork-first" version of this document.*

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

**Risk carried:** the dependency pre-flight must run live in the app before every mask, not be hardcoded from the test result: `systemctl --user show plasma-plasmashell.service -p WantedBy,RequiredBy,PartOf,BoundBy` with `RequiredBy`/`WantedBy`/`BoundBy` empty (only `PartOf=graphical-session.target`, which is one-directional). A KDE update could change that. If the check fails, the app refuses to mask.

### D2 — The wallpaper mirror call: leave as-is (RESOLVED by D10)

`shell/services/Wallpapers.qml` fires a fire-and-forget `qdbus6 org.kde.plasmashell` call on every wallpaper change. It fails silently and harmlessly when plasmashell is masked (confirmed in Tests 2B/13), and works when plasmashell runs (headless or stock). Since the fork is unmodified and the app owns masking, there is nothing to change. Revisit only if a later fork customization touches wallpaper handling.

### D3 — The installer's konsave backup is not relied on by the app (REVISED)

Upstream's `00-backup-themes.sh` still runs as an unconditional install step and writes `<install checkout>/backups/<timestamp>/` (a `.knsv` konsave archive plus `previous_lookandfeel.txt`; packaged installs use `~/.cache/caelestia-kde/backups/`; the latest path is recorded in `~/.cache/caelestia-kde/backup-dir.txt`; `backups/` is git-ignored). It restored the stock Plasma panels correctly on the 2026-09-30 uninstall.

The app does not depend on it, for two reasons: it only covers the stock side, and after the 2026-09-30 clean reinstall only the newest backup remained in `backups/` (whether the uninstall removed the older ones or they were deleted is unconfirmed, so the original pre-Caelestia snapshot may be gone). The app takes its own snapshots (D11).

### D4 — Screen-edge restore: re-scoped, handled app-side

**Original claim:** `screenedges.cpp` writes `~/.config/caelestia/stolen-screen-edges.json` and nothing reads it back; `uninstall.sh` has no matching lines.

**Re-scoped 2026-09-30:** `screenedges.cpp` does restore on its own — `restoreAll()` on exit and `recoverFromCrash()` on start (Test 13 logged "Crash recovery: restoring screen corner 7"). `uninstall.sh` itself still has no logic that reads the JSON (per the earlier grep). After the 2026-09-30 clean uninstall with the konsave backup restore selected, `Meta+W` and the Overview corner worked again, so the Test 14 breakage did not reproduce on that path. Which mechanism did the restore (the konsave backup or the shell's own exit handler) is unknown. **Unverified:** a clean stop of `caelestia-shell.service` without a konsave restore, which is what the app's switch does; Test 13 only covered `kill -9`.

**Handling:** the app's backup/restore (D11) includes the KWin electric-border config, so no upstream code change is needed. Reproduce the clean-stop case first; only add dedicated edge handling if it actually breaks.

### D5 — Separate, standalone app, independent of both shells

The app is its own binary with its own `.desktop` entry, reachable from both Caelestia's launcher and stock KDE. It cannot live inside `caelestia-shell.service`: switching off stops the whole Caelestia process tree.

**Constraint (unverified, test early):** if the app is launched from Caelestia's launcher it may sit inside `caelestia-shell.service`'s cgroup, and stopping that service could kill the app mid-switch. The app must detach into its own systemd scope before stopping anything.

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

**Contents (decided 2026-09-30):** so that plasmashell always returns to the way the user set it previously, backups cover both the shell-related files and the look-and-feel/behavior keys the installer changes.

- *Whole-file snapshots (shell-owned):* `plasma-org.kde.plasma.desktop-appletsrc` (panels, desktop wallpaper), `plasmashellrc` (`ShellPackage`), `kscreenlockerrc` (lock screen theme and wallpaper), and `~/.config/caelestia/`. From the Caelestia folder, exclude `stolen-screen-edges.json` (runtime state; a restored stale copy could make crash recovery restore the wrong corner) and caches. Check the folder's size before implementing.
- *Group-level snapshots (shared files):* only the groups/keys the installer or Caelestia change, so unrelated settings in the same files are never touched. `kwinrc`: electric-border groups, `Desktops`, the `Plugins` bridge/tracker keys, the `org.kde.kdecoration2` group. `kglobalshortcutsrc`: the `kwin` group. Look-and-feel/behavior: `plasmarc` (Theme, OSD), `kdeglobals` (widgetStyle, ColorScheme, OSDEnabled), `plasmanotifyrc`, `powerdevilrc` (BrightnessControl, AC), `kmixrc` (Global ShowOSD), `ksplashrc` (KSplash Engine). The exact key lists are derived from the install scripts (`04`, `06`, `07`, `08`, `09`, `10`) when implementing; konsole profile keys are also touched by the installer and still to be checked.
- *Not backed up:* installed content (`~/.config/quickshell/caelestia/` including `.current_commit`, the lock screen shell package under `~/.local/share/plasma/shells/`, the unit files; unit enablement is handled by the switch rules) and anything system-level such as `/etc/sddm.conf` (the unprivileged app does not touch it).
- *What "return to the way I set previously" means:* switching to plasmashell restores the snapshot taken when the user last left Plasma mode, not the original pre-Caelestia state. Older snapshots stay available in the dropdown.
- *Storage:* `~/.local/share/caelestia-switch/backups/<side>/<timestamp>/`, outside both shells' config. Each backup has a manifest recording the original path of every item, the Caelestia commit (`.current_commit`), the Plasma version and the date, so the dropdown has readable labels and the app can warn on a version mismatch.

### D12 — Switch semantics (NEW, confirmed 2026-09-30)

- **To plasmashell:** always stop and disable Caelestia (even with the checkbox unchecked, so two UIs never appear); unmask plasmashell if masked; restore the selected stock backup.
- **To Caelestia, checkbox checked:** start Caelestia, pre-flight then stop and mask plasmashell.
- **To Caelestia, checkbox unchecked:** start Caelestia and leave plasmashell running headless (upstream's default state); if plasmashell is masked or not running, unmask and start it.
- **The checkbox therefore only has an effect when switching to Caelestia.**
- **Caelestia is disabled, not masked** (`disable --now`). `caelestia-shell.service` is a regular file in `~/.config/systemd/user` written by `10-autostart.sh`, and masking would collide with that file. plasmashell's unit ships with the system, so masking works there (proven in testing). The `disable` behavior for Caelestia is not yet tested.
- Stop Caelestia before resetting `ShellPackage`, because Caelestia's startup wrapper self-heals it back to `caelestia.desktop`.

### D13 — A logout is always required; graceful, with a warning (NEW, confirmed 2026-09-30)

Apply all changes first, then log out with `qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logout` (the call Caelestia's own Logout button uses; worked in Test 16.5). Do not use the ksmserver call (failed on Plasma 6.7.5) or `loginctl terminate-session` (leaves the systemd user units running, Test 16.5). The app shows a "save your work" warning and does not force-kill applications; the normal logout lets apps ask about unsaved work.

**Unverified, test early:** plasmashell may rewrite its config (for example the applets config, and possibly `kwinrc` for KWin) when it exits, which would clobber a config restore done while it was still running. The config swap must therefore happen after the outgoing shell has stopped and be verified after the next login.

### D14 — Helper units follow the switch only when safe (NEW, confirmed 2026-09-30)

Caelestia-related units besides `caelestia-shell.service` include `cliphist.service` (clipboard-history watchers) and the update-checker timer/service. When switching to plasmashell, stop them only if a live check shows nothing depends on them (same pattern as the plasmashell pre-flight); otherwise leave them running. Start them again when switching to Caelestia. The KWin workspace-tracker effect is not a process and stays loaded in KWin.

### D15 — Source and version detection (NEW, decided 2026-09-30)

The install records only `.current_commit` and `.update_branch` under `~/.config/quickshell/caelestia/`; the version number lives in `.github/version.env` inside the checkout; no source repo is recorded. A commit hash alone cannot tell fork from upstream while their history is shared. The app therefore tries, in order:

1. **App-written marker** (source, version, commit, checkout path), written whenever the app installs or updates. Ignored if its recorded commit differs from the current `.current_commit` (the install changed outside the app).
2. **Checkout discovery:** look for a checkout (`~/caelestia-kde` or `CAELESTIA_DIR`) whose `HEAD` equals `.current_commit`. If found, the source is its `origin` URL (which is also where its updates come from) and the version is `.github/version.env`. This works for an unmodified upstream install with no changes to it.
3. **Later:** the fork's installer writes its own marker (fork track, F0).
4. **Otherwise show "unknown"** for source and/or version. No first-run question to the user.

### D16 — Toolkit: C++ with Qt6 Widgets and KF6 (NEW, decided 2026-09-30)

**Decision:** the app is written in C++ with Qt6 (Core, Widgets, DBus) and KF6 (ConfigCore), built with CMake. Structure: a core library (detection, backup/restore, switch logic), a CLI on top of it, and a thin Widgets GUI that only calls the core. The detection, backup and switch phases can therefore be built and tested over SSH before any window exists.

**Why:** Qt6 and KF6 are already installed on both laptops, so no extra runtime packages; the app is small and starts fast on the 4 GiB / A4-9125 test laptop; `KConfig` copies nested groups of `kwinrc`, `kglobalshortcutsrc` and the other files faithfully, which the group-level backups (D11) need, and it is the same library `screenedges.cpp` uses; QtDBus covers the `org.kde.Shutdown` logout call (D13); upstream's own plugins and installer are also C++.

**Rejected:** Python with PySide6 (large extra dependency, slower start and more RAM on the A4, no `KConfig` group support, so key-by-key `kwriteconfig6` calls or hand-parsing KDE's INI quirks); Qt Quick/Kirigami (heavier on the Radeon R3, more machinery for three simple screens; can be revisited later since the core is separate); Flutter (GTK-based on Linux desktop, ignores KDE theming, heavy, no `KConfig`).

**Build notes:** build on the MSI (same CachyOS; check the Qt and KF6 versions match the ASUS) and copy the binary to the ASUS. Do not build with `-march=native`. Building on the ASUS itself is slow (4 GiB, dual core).

**Repository (decided 2026-09-30):** the app lives in its own repository (working name `caelestia-switch`), separate from the caelestia-kde fork, because it must work with upstream and the fork alike (D10). The initial project skeleton (CMake, core library, CLI and GUI stubs) builds with Qt6.

## Open decisions

- Whether the unverified constraints above hold: survive-the-switch (D5), config-rewrite-on-exit (D13), `disable --now` on the Caelestia unit (D12).

## Non-goals (for now)

- **Customization of caelestia-kde.** Deferred to the fork track in the roadmap.
- **Multi-machine support.** Everything is validated on one test machine (ASUS X441BA).
- **Upstreaming fixes.** Considered later, separately from this project's roadmap.
