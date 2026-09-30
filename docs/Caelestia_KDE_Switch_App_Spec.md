# Caelestia KDE Switch App — Spec

*Revised 2026-09-30 (v2). Supersedes the earlier terminal-first spec. Working name: `caelestia-switch` (provisional).*

## Purpose

One app, reachable from both Caelestia and stock Plasma, that switches between `plasmashell` and Caelestia KDE without uninstalling either, and later handles install/update/uninstall for either source (ladybug-me upstream or the user's own fork). It must work with an **unmodified** upstream install. See the architecture doc (D5, D6, D10) for why it is a separate, mostly unprivileged app.

## Startup flow

1. **Loading screen.** The app checks the system; the user cannot interact until the checks finish. Checks are local only (`systemctl`, files); no network calls at startup, so the screen stays short. Update checks run later, asynchronously.
2. **Readings.** The checks produce the readings in the table below.
3. **UI.** Depending on the readings, one of the screens below is shown.

### Readings (replaces the old three-label status)

| Reading | Example values |
|---|---|
| Which shell is drawing the desktop | Caelestia / Plasma (derived from `ShellPackage` plus process states; exact rule to be defined) |
| plasmashell process | running / stopped |
| plasmashell unit | masked / not masked |
| `caelestia-shell.service` | active / inactive, enabled / disabled |
| Caelestia installed | yes / no |
| Source | ladybug-me / fork / unknown (see Source and version detection) |
| Version | e.g. v2.5.0 / unknown (see Source and version detection) |
| Backups present | stock side: yes/no; Caelestia side: yes/no |

An unmodified upstream install reads as: Caelestia drawing the desktop, plasmashell running headless and not masked. That is a **valid** state (Condition A in the test log), not an error.

**Inconsistent** now only covers combinations that are actually wrong: plasmashell masked but still running; both shells drawing panels; neither shell running; or the state file says a switch was in progress. The UI points the user at `repair` (below) instead of picking a side.

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
- **Caelestia is disabled, not masked** (`systemctl --user disable --now`). `caelestia-shell.service` is a regular file in `~/.config/systemd/user`, and masking would collide with it. *Not yet tested.*
- **Stop Caelestia before resetting `ShellPackage`.** Caelestia's startup wrapper (`~/.local/bin/caelestia-autostart.sh`) rewrites it to `caelestia.desktop`.
- **Pre-flight before masking plasmashell**, live, every time: `RequiredBy`, `WantedBy` and `BoundBy` of `plasma-plasmashell.service` must be empty. If not, refuse to mask.
- **Helper units** (`cliphist.service`, the update-checker timer/service): stop them with Caelestia only if a live check shows nothing depends on them; otherwise leave them running. The KWin workspace-tracker effect is not a process and is left alone.

## Order of operations for a switch

1. Show the warning dialog: applications will be closed by the logout, save your work. Do not force-kill applications.
2. Write the state file: `transitioning`, step 0.
3. Detach the app into its own systemd scope (so stopping a shell cannot kill it). *Unverified, test early.*
4. Snapshot the mode being left (automatic backup).
5. Stop the outgoing shell and, where safe, its helper units.
6. Restore the selected backup (config files) — after the outgoing shell has stopped, because plasmashell may rewrite its config on exit. *Unverified, test early.*
7. Apply unit changes (mask/unmask, enable/disable, start/stop) per the rules above.
8. Verify internally with the same readings; only continue if they match the expected state.
9. State file: `pending-logout`. Log out with `qdbus6 org.kde.Shutdown /Shutdown org.kde.Shutdown.logout`.
10. After the next login, `status` confirms the expected final state and the state file is closed out.

The state file is updated after every step, not just at the end.

## Backups

- Two kinds, both timestamped and listed in the dropdown: **stock (plasmashell) side** and **Caelestia side**.
- Snapshots are taken automatically for the mode being left; the user can also create one manually.
- Stored outside both Caelestia's and Plasma's own config, in `~/.local/share/caelestia-switch/backups/<side>/<timestamp>/`. Each backup has a manifest: the original path of every item, the Caelestia commit (`.current_commit`), the Plasma version and the date, used for the dropdown labels and to warn on a version mismatch.
- The installer's own konsave backup (`<checkout>/backups/<timestamp>/`, `~/.cache/caelestia-kde/backup-dir.txt`) is not relied on; after the 2026-09-30 reinstall only the newest one remained.
- **Contents (decided 2026-09-30; architecture doc D11):** so plasmashell returns to the way the user set it previously.
  - *Whole-file (shell-owned):* `plasma-org.kde.plasma.desktop-appletsrc`, `plasmashellrc`, `kscreenlockerrc`, `~/.config/caelestia/` (excluding `stolen-screen-edges.json` and caches).
  - *Group-level (shared files; only the groups/keys the installer or Caelestia change):* `kwinrc` (electric-border groups, `Desktops`, `Plugins` bridge/tracker keys, `org.kde.kdecoration2`), `kglobalshortcutsrc` (`kwin` group), and the look-and-feel/behavior keys in `plasmarc`, `kdeglobals`, `plasmanotifyrc`, `powerdevilrc`, `kmixrc`, `ksplashrc`. Exact key lists come from the install scripts when implementing.
  - *Not backed up:* installed content (`~/.config/quickshell/caelestia/`, the lock screen shell package, unit files) and system-level files such as `/etc/sddm.conf`.
  - Restoring the stock side means the snapshot taken when the user last left Plasma mode; older ones are in the dropdown.

## Subcommands (CLI core)

The GUI is a thin layer over a CLI core, so every action is scriptable and testable over SSH. *(Toolkit decided: C++ with Qt6 Widgets and KF6; architecture doc D16.)*

- `status` — unprivileged, read-only; prints the readings above.
- `backup` / `restore` — unprivileged; create or apply a snapshot.
- `on` / `off` — unprivileged; the switch rules above (`on` takes an option for the mask/disable-plasmashell behavior). Idempotent: switching to the mode you are already in is a no-op with exit 0.
- `repair` — unprivileged; only reachable when `status` reports Inconsistent. Reads the state file to find which switch was in progress and resumes from the last completed step. If the state file is missing or unreadable, falls back to restoring stock Plasma, the state with no fork-specific assumptions.
- `install` / `uninstall` — privileged (explicit elevation prompt). Wraps the chosen source's installer/uninstaller. The user picks the source: ladybug-me upstream or the fork. Uninstalling while in Caelestia mode must run the restore first.
- `update` — the check is unprivileged; applying it is privileged and only on explicit confirmation, never in the background. Upstream's `caelestia-check-updates` and `update.sh` hardcode the upstream repo and only accept `main`/`dev` (architecture doc D8), so the app must supply its own source choice.

## State file

Path: `~/.config/caelestia-switch/state` (exact location not critical, but outside both Caelestia's and Plasma's config). Minimum content: current mode (`caelestia` / `stock` / `transitioning` / `pending-logout`) and, if transitioning, which step was last completed. Written synchronously after each step. This is the most important piece to get right (architecture doc D7, motivated by Test 16.5).

## `.desktop` entry

One entry visible in both Caelestia's launcher and stock KDE's launcher/KRunner (a normal installed `.desktop` file; confirm during testing rather than assume). It opens the GUI. `install`/`update`/`uninstall` can also be reached from the GUI once implemented.

## Source and version detection (decided 2026-09-30; architecture doc D15)

The install records only `.current_commit` and `.update_branch`; the version lives in `.github/version.env` inside the checkout; no source is recorded. Tried in order:

1. **App-written marker** (source, version, commit, checkout path), written whenever the app installs or updates. Ignored if its commit differs from the current `.current_commit`.
2. **Checkout discovery:** a checkout (`~/caelestia-kde` or `CAELESTIA_DIR`) whose `HEAD` equals `.current_commit`. Source = its `origin` URL; version = `.github/version.env`.
3. **Later:** the fork's installer writes its own marker.
4. **Otherwise "unknown"** for source and/or version. No first-run question.

All of this is local; no network calls on the startup path.

## Open items

1. **Survive the switch:** whether the app dies when launched from Caelestia's launcher and it stops the service (D5). Test early (roadmap Phase A0).
2. **Config rewrite on shell exit:** whether restored config (the applets config, and possibly `kwinrc`) gets clobbered (D13). Test early.
3. **`disable --now` on `caelestia-shell.service`** behaves as expected across a logout/login (D12).

## Out of scope for v1

- Multi-user support (single-user machine, matching the test environment).
- Any customization of caelestia-kde (the fork track in the roadmap).
- Live switching without logout (a possible later optimization once the logout path is solid).
