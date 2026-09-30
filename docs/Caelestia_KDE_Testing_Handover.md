# Caelestia KDE Testing Handover

## Purpose

This handover is only for testing.  
The goal is to verify whether Caelestia KDE can run as a practical desktop shell on the test laptop with the smallest possible dependency on `plasmashell`, while keeping the test plan simple and repeatable.

## Test machine

Primary test machine:

- ASUS X441BA
- CachyOS Linux
- KDE Plasma 6.7.5
- KDE Frameworks 6.30.0
- Qt 6.11.2
- Kernel 7.2.7-1-cachyos (64-bit)
- Wayland
- AMD A4-9125
- 4 GiB RAM
- AMD Radeon R3 Graphics

SSH is already available for recovery and remote access.

## Main testing goal

The main question is:

**Can Caelestia KDE run in a usable way without relying on a normal Plasma Shell session?**

That means the test plan must focus on:

1. Whether Caelestia starts correctly.
2. Whether core shell features work in a live session.
3. Whether the remaining `plasmashell` dependency breaks anything visible.
4. Whether the system stays usable on low hardware.

## Test procedures (how to actually create each test condition)

The source audit (see `Caelestia_KDE_Plasma_Handover_2.md`, Section 13a) found that the current install leaves `plasmashell` running with its panels stripped — it does not stop the process. That means "test without plasmashell" is actually two different conditions, and both need to be tested separately, since they carry different risk:

**Condition A — headless plasmashell (current shipped state, no extra steps needed)**

This is just a normal post-install session. `plasmashell` is alive, panels are removed via the install scripts' D-Bus call. Use this as the baseline for everything in this document unless a test explicitly says "Condition B."

**Condition B — plasmashell fully stopped (the actual target state)**

Two ways to get here, in order of how permanent they are:

1. **Live kill, for a quick per-session check:** after a normal login (Condition A), run `kquitapp6 plasmashell` (or `qdbus6 org.kde.plasmashell /MainApplication quit` if `kquitapp6` isn't available) and observe what breaks immediately — this is the fastest way to iterate.
2. **Boot-time removal, for a real test of session startup without it:** first check how this specific session starts plasmashell before assuming a mechanism — run `systemctl --user status plasma-plasmashell.service` right after login to see if CachyOS's Plasma 6.7.5 session manages it as a systemd user unit (Plasma 5.27+/6.x sessions increasingly do). If it exists, `systemctl --user mask plasma-plasmashell.service` and re-login. If it doesn't exist as a unit, plasmashell is being started directly by the `startplasma-wayland` session script instead, and stopping it at boot means checking that script / the session's autostart `.desktop` entries rather than systemd. Confirm which mechanism applies on this machine before test 1 (Session startup) is run under Condition B — don't assume.

Record which condition (A or B, and which sub-method if B) every test result below was captured under. A "pass" under Condition A does not establish the project's actual goal — that requires Condition B to pass.

---

## What must be tested

### 1. Session startup

Test whether Caelestia can start cleanly from a fresh login/session state.

Check:

- session launches without looping or crashing
- no stuck startup screen
- no blank desktop after login
- no obvious startup delay beyond normal for this machine
- SSH remains reachable if the graphical session fails

### 2. `plasmashell` dependency check

This is the most important test. Run it under **both** conditions from the Test Procedures section above, and record which condition each result belongs to.

**Under Condition A (headless, current shipped state) — expected to pass, confirms nothing regressed:**

- Caelestia still launches
- panels and shell UI do not depend on Plasma Shell's panels being visible
- no repeated D-Bus errors in the logs even though plasmashell is alive

**Under Condition B (fully stopped) — this is the real test of the project goal:**

- Caelestia still launches and stays usable with no plasmashell process at all
- no hidden fallback behavior that only works when Plasma Shell is present
- no repeated D-Bus timeout/retry errors from the failed `org.kde.plasmashell` calls (Caelestia's wallpaper sync call in particular — see test 3, this is expected to fail silently, but "silently" needs to be confirmed, not assumed)
- nothing else in the session (KRunner, systray items, notification daemon ownership, polkit prompts) turns out to have an unexpected dependency on plasmashell being present

### 3. Wallpaper and appearance

Earlier drafts called this the main blocker. On closer reading of the actual wallpaper code, it's narrower than that: Caelestia applies its own wallpaper independently of plasmashell, and only *mirrors* that choice into Plasma's native desktop config and (separately, directly) into `kscreenlockerrc` for the lock screen — the mirror is fire-and-forget with no error handling. So the real risk isn't "wallpaper breaks," it's "Plasma's own config silently goes stale under Condition B, with no visible symptom." Test for the narrow thing, not the broad one:

- wallpaper applies from Caelestia UI (should work under both A and B — this doesn't go through plasmashell)
- wallpaper persists after logout/login
- theme or color updates follow wallpaper changes if expected
- appearance changes do not require a manual Plasma Shell restart
- failure mode when wallpaper application is interrupted
- **Condition B specific:** confirm the `qdbus6 org.kde.plasmashell` mirror call actually fails silently as expected (check logs for a D-Bus "service not available" style error, not a hang or retry loop) — this only matters if the machine ever falls back to stock Plasma, but "silent" needs confirming, not assuming
- **Condition B specific:** confirm the lock-screen wallpaper still syncs correctly — that path writes `kscreenlockerrc` directly via `kwriteconfig6` and does not go through plasmashell at all, so it should be unaffected; worth confirming it actually isn't

### 4. Core shell features

These are the shell functions that should be directly verified in a live session:

- launcher
- overview
- notifications
- tray
- clipboard history
- screenshot UI
- screen recording
- OSD
- workspace switching
- KWin integration/hot corners if enabled
- **lock screen** (not in the earlier list — this is its own integration path, delivered through KDE's Plasma shell-package mechanism rather than a custom lock daemon, with self-heal logic meant to reassert itself if a KDE update or config reset overwrites it)
- **screen edges/hot corners, unclean-exit case** — the KWin integration takes over KWin's own electric-border config and stores the original elsewhere to restore it later; specifically test what happens to hot corners after an unclean exit (`kill -9` on the shell process, not a normal quit), not just normal open/close

For each one, test:

- opens normally
- closes normally
- no obvious input freeze
- no visual corruption
- no crash after repeated open/close cycles

For lock screen specifically, also test:

- locks and unlocks correctly
- greeter renders correctly (not a fallback/broken state)
- self-heal actually triggers if `kwriteconfig6` is used to reset the shell-package config by hand (simulates what a KDE update might do)

For screenshot/recording specifically, also note:

- both appear to route through a desktop portal rather than plasmashell directly — whether that portal implementation itself has any implicit plasmashell dependency is an open question, not a confirmed one; if recording or screenshotting fails only under Condition B, this is the first place to look

### 5. Low-memory and low-GPU behavior

This laptop has only 4 GiB RAM and an AMD Radeon R3 (integrated, low-end even for its era) paired with a dual-core A4-9125 — both memory and GPU compositing are realistic bottlenecks here, not just memory.

Check:

- idle memory use after login
- memory use after opening launcher, overview, notifications, and wallpaper UI
- whether the shell stays responsive under pressure
- whether the system swaps heavily
- whether the session becomes unstable after repeated use
- frame pacing/smoothness of animations and blur effects specifically — stutter here can be a GPU-bound problem separate from memory pressure, and needs its own check rather than being inferred from the memory numbers

**Baseline for comparison:** before or alongside the above, do a quick pass of the same idle/interaction memory and smoothness checks on a stock Plasma session (panels intact, no Caelestia) on this same machine. Without that baseline, "feels slow" can't be attributed to Caelestia versus just this being weak hardware.

### 6. Recovery behavior

Because this is the only dedicated test machine, recovery matters.

Check:

- SSH still works if the GUI fails
- the session can be restarted without reinstalling
- logs can be collected after failure
- you can recover to a usable state without a full system reset

Specific commands to run over SSH when collecting logs after a failure:

- `journalctl --user -u caelestia-shell.service -b` — the shell's own systemd-managed startup/runtime log
- `journalctl -b -1` — previous boot's full log, if the failure happened before the current boot
- `coredumpctl list` then `coredumpctl info <PID>` — if the shell or plasmashell crashed rather than hung
- `~/.cache/caelestia-kde/shell-build.log` and `workspace-tracker-build.log` — only relevant if the failure looks build/install related rather than runtime
- `systemctl --user status plasma-plasmashell.service caelestia-shell.service` — quick state check of both, if the unit exists on this session (see Test Procedures)

## What should be observed and recorded

For each test, record:

- test name
- **`caelestia-kde` commit hash being tested** — the upstream is under active development (it already has self-heal logic reacting to past breakage), so a "pass" is only meaningful tied to a specific commit
- **which condition (A: headless, or B: fully stopped, and which sub-method if B)**
- pass or fail
- what happened
- whether the failure is Caelestia, KWin, KDE service, or `plasmashell` related
- exact error messages if any
- whether the issue is reproducible

## What to test first

Priority order:

1. Session startup — Condition A first, then Condition B
2. Wallpaper application — including the Condition B mirror/silent-failure checks
3. Lock screen — including the self-heal check
4. Launcher
5. Notifications
6. Tray
7. Overview
8. Clipboard history
9. Screenshot and recording — note the portal dependency question
10. OSD
11. Workspace switching
12. Screen edges/hot corners, including the unclean-exit case
13. Stock-Plasma baseline (memory + frame pacing), for comparison
14. Low-memory and low-GPU stress behavior
15. Recovery via SSH

## What not to focus on yet

Do not spend time on:

- final visual polish
- optional themes
- custom feature development
- rebuilding KDE infrastructure
- non-essential UI redesign
- advanced automation until the core session is stable

## Useful pass/fail rule

A test only counts as passed if:

- it works more than once
- it works after logout/login
- it does not depend on hidden manual repair
- it does not require Plasma Shell in a way that defeats the project goal

## Current testing objective

The immediate objective is not to finish the whole project.

The immediate objective is to determine:

- which parts already work
- which parts still need `plasmashell`
- which parts are usable on this low-end laptop
- whether the current Caelestia KDE path is worth continuing as the main direction

## Next conversation focus

Use the next session only for:

- live testing steps
- test results
- failure analysis
- log collection
- deciding what must be fixed next

