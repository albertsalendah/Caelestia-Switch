# Caelestia KDE Switch App — Spec

*Revised 2026-10-01 (v2.1: status implemented, version detection amended). Supersedes the earlier terminal-first spec. Working name: `caelestia-switch` (provisional).*

## Purpose

One app, reachable from both Caelestia and stock Plasma, that switches between `plasmashell` and Caelestia KDE without uninstalling either, and later handles install/update/uninstall for either source (ladybug-me upstream or the user's own fork). It must work with an **unmodified** upstream install. See the architecture doc (D5, D6, D10) for why it is a separate, mostly unprivileged app.

## Startup flow

1. **Loading screen.** The app checks the system; the user cannot interact until the checks finish. Checks are local only (`systemctl`, files); no network calls at startup, so the screen stays short. Update checks run later, asynchronously.
2. **Readings.** The checks produce the readings in the table below.
3. **UI.** Depending on the readings, one of the screens below is shown.

### Readings (replaces the old three-label status)

| Reading | Example values |
|---|---|
| Bar/panel provider | Caelestia / Plasma / none, or "both" (Inconsistent). Rule (defined in Phase A1, architecture D17): Caelestia if its service is active or a quickshell process runs; Plasma if plasmashell runs with the stock `ShellPackage` (unset or `org.kde.plasma.desktop`); otherwise none. Panel config is not read. |
| plasmashell process | running / stopped |
| plasmashell unit | masked / not masked |
| `caelestia-shell.service` | active / inactive, enabled / disabled |
| Caelestia installed | yes / no |
| Source | ladybug-me / fork / unknown (see Source and version detection) |
| Version | e.g. v2.5.0 / unknown (see Source and version detection) |
| Backups present | stock side: yes/no; Caelestia side: yes/no |

An unmodified upstream install reads as: Caelestia providing the bar and panels, plasmashell running headless and not masked. That is a **valid** state (Condition A in the test log), not an error. A headless plasmashell still owns the desktop layer (wallpaper and the KDE right-click menu) whenever Caelestia is not running (checked 2026-09-30), so the desktop is never blank of a shell even without Caelestia (it shows the wallpaper, any desktop icons and KDE's right-click menu); only the panels are missing until the stock config is restored. That state reads as provider "none" and is valid.

**Inconsistent** now only covers combinations that are actually wrong: plasmashell masked but still running; both shells drawing panels; neither shell running; the state file says a switch was in progress; or systemd cannot be queried (reported as such, not guessed). The UI points the user at `repair` (below) instead of picking a side.

### Screen A — Backup needed

Shown when no Caelestia-side backup exists. Only a **Backup** button is available; no switching until it is created. After the backup completes, the app moves to Screen B.

### Screen B — Switch

Shows: plasmashell status, the source of the installed Caelestia, and its version. Includes:
- a dropdown of available backups (both sides) to restore on switch;
- the direction of the switch (from the current mode to the other);
- a checkbox (see rules below);
- a **Switch** button.

## Switch rules (confirmed 2026-09-30)

| Direction | Checkbox | What happens |
|---|---|---|
| To plasmashell | (no effect) | Unmask plasmashell if masked. Stop and **disable** Caelestia (always, so two UIs never appear). Stop helper units if safe. Restore the selected stock backup. |
| To Caelestia | checked ("also disable plasmashell") | Start Caelestia, restore the selected Caelestia backup, run the live dependency pre-flight, then stop and **mask** plasmashell. Start helper units. |
| To Caelestia | unchecked | Start Caelestia, restore the selected Caelestia backup. Leave plasmashell running headless (upstream's default state); if plasmashell is masked or not running, unmask and start it. Start helper units. |

So the checkbox only appears/has an effect when switching to Caelestia.

Additional rules:
- **Caelestia is disabled, not masked** (`systemctl --user disable --now`). `caelestia-shell.service` is a regular file in `~/.config/systemd/user`, and masking would collide with it. *Checked 2026-09-30: `disable --now` holds across a logout and login, and `enable --now` restores it live.*
- **Quit Caelestia gracefully, not with a plain `systemctl stop`** (checked 2026-10-01; architecture D18): a plain stop leaves its stolen shortcuts and screen corner in place, which is the Test 14 breakage.
- **Stop Caelestia before resetting `ShellPackage`.** Caelestia's startup wrapper (`~/.local/bin/caelestia-autostart.sh`) rewrites it to `caelestia.desktop`.
- **Pre-flight before masking plasmashell**, live, every time: `RequiredBy`, `WantedBy` and `BoundBy` of `plasma-plasmashell.service` must be empty. If not, refuse to mask.
- **Helper units** (`cliphist.service`, the update-checker timer/service): stop them with Caelestia only if a live check shows nothing depends on them; otherwise leave them running. The KWin workspace-tracker effect is not a process and is left alone.

## Order of operations for a switch

1. Show the warning dialog: applications will be closed by the logout, save your work. Do not force-kill applications.
2. Write the state file: `transitioning`, step 0.
3. Check the app's own cgroup; if it sits inside a shell's service, re-execute it in its own systemd scope. (It does not when launched from Caelestia's launcher, checked 2026-09-30; not yet checked from stock Plasma's launcher.)
4. Snapshot the mode being left (automatic backup).
5. Stop the outgoing shell and, where safe, its helper units. Caelestia is quit **gracefully** (`quickshell kill -i <id>`, with the session's `WAYLAND_DISPLAY` set), not with a plain `systemctl stop`, so it releases its stolen shortcuts and screen corner; then the KWin Overview effect is reloaded (architecture D18).
6. Restore the selected backup (config files) — after the outgoing shell has stopped, because plasmashell may rewrite its config on exit. *Unverified; checked in the Phase A2/A3 round trips.*
7. Apply unit changes (mask/unmask, enable/disable, start/stop) per the rules above.
8. Verify internally with the same readings; only continue if they match the expected state.
9. State file: `pending-logout`. Log out with `qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logout` (checked 2026-09-30: works from a shell outside both shells' cgroups, with no confirmation dialog).
10. After the next login, `status` confirms the expected final state and the state file is closed out.

The state file is updated after every step, not just at the end.

## Backups

- Two kinds, both timestamped and listed in the dropdown: **stock (plasmashell) side** and **Caelestia side**.
- Snapshots are taken automatically for the mode being left; the user can also create one manually.
- Stored outside both Caelestia's and Plasma's own config, in `~/.local/share/caelestia-switch/backups/<side>/<timestamp>/`. Each backup has a manifest: the original path of every item, the Caelestia commit (`.current_commit`), the Plasma version and the date, used for the dropdown labels and to warn on a version mismatch.
- The installer's own konsave backup (`<checkout>/backups/<timestamp>/`, `~/.cache/caelestia-kde/backup-dir.txt`) is not relied on; after the 2026-09-30 reinstall only the newest one remained.
- **Contents (decided 2026-09-30, narrowed 2026-10-01; architecture doc D11):** so plasmashell returns to the way the user set it previously. Only what the installer writes persistently is backed up; runtime state Caelestia manages itself (screen edges, stolen shortcuts) is not (D18).
  - *Whole-file (shell-owned):* `plasma-org.kde.plasma.desktop-appletsrc`, `plasmashellrc`, `kscreenlockerrc`, and from `~/.config/caelestia/` only `cli.json`, `keybinds.json`, `shell.json`, `monitors` (never the `stolen-*.json` recovery files or caches).
  - *Key-level (shared files; restoring writes the saved value, and a key or group absent from the snapshot is deleted):* `kwinrc` (`Desktops`, the `Plugins` keys the installer sets, `org.kde.kdecoration2`), `kwinrulesrc` (the three `caelestia-*` rule groups and their entries in `[General]`; the user's own rules stay), `plasmarc` (`OSD`, `Theme/name`), `kdeglobals` (widget style, color scheme, generated `Colors:*` groups), `plasmanotifyrc`, `powerdevilrc`, `kmixrc`, `ksplashrc`. Exact key lists come from the install scripts when implementing.
  - *Not backed up:* `kglobalshortcutsrc`, the electric-border keys, the generated `[Tiling]` groups, Konsole profiles, installed content (`~/.config/quickshell/caelestia/`, the lock screen shell package, unit files) and system-level files such as `/etc/sddm.conf`.
  - Restoring the stock side means the snapshot taken when the user last left Plasma mode; older ones are in the dropdown.

## Subcommands (CLI core)

The GUI is a thin layer over a CLI core, so every action is scriptable and testable over SSH. *(Toolkit decided: C++ with Qt6 Widgets and KF6; architecture doc D16.)*

- `status` — unprivileged, read-only; prints the readings above (**implemented in Phase A1, 2026-10-01**). `--json` prints them as JSON. Exit code 0 even when Inconsistent, 3 if systemd cannot be queried. The "Backups present" reading is added in Phase A2.
- `backup` / `restore` — unprivileged; create or apply a snapshot.
- `on` / `off` — unprivileged; the switch rules above (`on` takes an option for the mask/disable-plasmashell behavior). Idempotent: switching to the mode you are already in is a no-op with exit 0.
- `repair` — unprivileged; only reachable when `status` reports Inconsistent. Reads the state file to find which switch was in progress and resumes from the last completed step. If the state file is missing or unreadable, falls back to restoring stock Plasma, the state with no fork-specific assumptions.
- `install` / `uninstall` — privileged (explicit elevation prompt). Wraps the chosen source's installer/uninstaller. The user picks the source: ladybug-me upstream or the fork. Uninstalling while in Caelestia mode must run the restore first.
- `update` — the check is unprivileged; applying it is privileged and only on explicit confirmation, never in the background. Upstream's `caelestia-check-updates` and `update.sh` hardcode the upstream repo and only accept `main`/`dev` (architecture doc D8), so the app must supply its own source choice.

## State file

Path: `~/.config/caelestia-switch/state` (exact location not critical, but outside both Caelestia's and Plasma's config). Minimum content: current mode (`caelestia` / `stock` / `transitioning` / `pending-logout`) and, if transitioning, which step was last completed. Written synchronously after each step. This is the most important piece to get right (architecture doc D7, motivated by Test 16.5).

## `.desktop` entry

One entry visible in both Caelestia's launcher and stock KDE's launcher/KRunner (a normal installed `.desktop` file; confirm during testing rather than assume). It opens the GUI. `install`/`update`/`uninstall` can also be reached from the GUI once implemented.

## Source and version detection (decided 2026-09-30; version source amended 2026-10-01; architecture doc D15)

The install records `.current_commit` and `.update_branch`, and also `.current_version` (a copy of the checkout's `.github/version.env`), all under `~/.config/quickshell/caelestia/`; no source repo is recorded. Source and version are resolved separately, each in order.

**Source:**
1. **App-written marker** (source, version, commit, checkout path), written whenever the app installs or updates. Read from `~/.config/caelestia-switch/install.json`. Ignored if its commit differs from the current `.current_commit`.
2. **Checkout discovery:** a checkout (`$CAELESTIA_DIR`, then `~/caelestia-kde`) whose `HEAD` equals `.current_commit`. Source = its `origin` URL (`github.com/ladybug-me/caelestia-kde` reads as "ladybug-me", `github.com/albertsalendah/caelestia-kde` as "fork", anything else "unknown").
3. **Later:** the fork's installer writes its own marker.
4. **Otherwise "unknown".** No first-run question.

**Version:** the marker, else `.current_version`, else the discovered checkout's `.github/version.env`, else "unknown". A moved checkout therefore leaves the version known and the source "unknown".

All of this is local; no network calls on the startup path. The `status` output also shows where each answer came from (marker / checkout / `.current_version`).

## Open items

1. **Survive the switch, from stock Plasma's launcher.** Checked from Caelestia's launcher (fine); repeat from stock Plasma's launcher in roadmap Phase A3.
2. **Backup details to check in Phase A2:** whether the installer also clears kwin `Switch to Desktop N` shortcuts, and which `kdeglobals` keys really need restoring.
3. **Config rewrite on shell exit:** whether restored config (the applets config, and possibly `kwinrc`) gets clobbered (D13). Checked in the Phase A2/A3 round trips.

## Out of scope for v1

- Multi-user support (single-user machine, matching the test environment).
- Any customization of caelestia-kde (the fork track in the roadmap).
- Live switching without logout (a possible later optimization once the logout path is solid).
