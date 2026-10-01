# Caelestia KDE — Live Testing Progress Handover

## Purpose

This file is the running handover for the live testing of `ladybug-me/caelestia-kde` on the test laptop.

It is intended to preserve the complete testing progress, exact conditions, commands used, observed results, warnings, and the next test to resume from if the work moves to a new conversation/session.

This file should be **updated after each test is completed**. Do not create a separate progress file for every test; keep extending this one.

---

## Source / project context

Primary architecture/testing references used for this work:

- `Caelestia_KDE_Plasma_Handover_2.md`
- `Caelestia_KDE_Testing_Handover (1).md`

The architecture handover records a source audit of `ladybug-me/caelestia-kde` at commit `be4188f` and identifies the remaining `plasmashell` dependency as narrower than originally assumed: Caelestia wallpaper rendering works independently, while a separate wallpaper synchronization/mirror path targets `org.kde.plasmashell`. It also confirms native KWin integration and treats PLM as infrastructure rather than part of the shell replacement.

The testing handover defines two live conditions:

### Condition A — headless plasmashell

Normal post-install/session state in the current shipped setup. `plasmashell` is running, but its panels have been stripped/removed from the visible shell presentation.

### Condition B — plasmashell fully stopped

The actual target condition. The session continues with KWin, KDE services, and Caelestia while `plasmashell` is stopped and `org.kde.plasmashell` is absent from D-Bus.

Condition B is the important architectural test because the project goal is to run Caelestia without a normal running `plasmashell` process.

---

## Test machine

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
- SSH available for recovery/remote access

---

## Testing rule

For every completed test, record:

1. Test number/name.
2. Condition A or B.
3. `caelestia-kde` commit/hash when known.
4. Commands used.
5. What happened.
6. Pass/fail result.
7. Warnings or unrelated issues.
8. Exact next step.

A test is considered passed only when it works repeatedly enough to establish the behavior, survives logout/login where relevant, does not require hidden manual repair, and does not depend on `plasmashell` in a way that defeats the project goal.

---

# Completed Tests

## Test 1 — Session startup

**Status:** Not recorded in this progress file yet.

The current live-testing sequence began with Test 2, so no Test 1 result has been supplied here.

---

# Test 2 — Wallpaper

## Test 2A — Wallpaper with `plasmashell` running

**Condition:** A — `plasmashell` running

### Initial state checks

Command:

```bash
pgrep -a plasmashell
```

Observed:

```text
1198 /usr/bin/plasmashell --no-respawn
```

`caelestia-shell.service` was active/running.

### Wallpaper test

Before changing wallpaper:

```bash
cp ~/.config/plasma-org.kde.plasma.desktop-appletsrc /tmp/plasma-wallpaper.before
cp ~/.config/kscreenlockerrc /tmp/kscreenlockerrc.before
```

Wallpaper was changed using the Caelestia KDE wallpaper UI.

The Plasma desktop wallpaper config changed from:

```text
file:///home/asus/Pictures/Wallpapers/dharmx-digital/1387138.png
```

to:

```text
file:///home/asus/Pictures/Wallpapers/New Folder/dharmx-digital/3x9iofas38aa1.webp
```

The lock-screen config changed to the same selected wallpaper path.

### User-visible result

- Wallpaper changed immediately: **YES**
- Wallpaper remained correct after about 10 seconds: **YES**
- Visual glitch: **NO**
- Caelestia crash/restart: **NO**
- Wallpaper survived logout/login: **YES**

### Journal observation

A narrow journal query around the wallpaper change showed no matching `plasmashell/qdbus/dbus/wallpaper/error/failed` entries.

### Verdict

**PASS**

---

## Test 2B — Wallpaper with `plasmashell` fully stopped

**Condition:** B — `plasmashell` fully stopped

### Creating Condition B

Command:

```bash
kquitapp6 plasmashell
```

Verification with `pgrep -a plasmashell` produced no output.

`caelestia-shell.service` remained active/running.

### Wallpaper behavior

Wallpaper was changed using the Caelestia KDE wallpaper UI and **continued to change successfully** with `plasmashell` stopped.

The Plasma desktop wallpaper config did not update in Condition B.

The lock-screen wallpaper config did update independently through `kscreenlockerrc`.

### Second verification run

A second Test 2B run was performed to confirm repeatability.

Second-run config state showed:

```text
~/.config/plasma-org.kde.plasma.desktop-appletsrc
Containment 43 wallpaper:
file:///home/asus/Pictures/Wallpapers/dharmx-digital/1387138.png
```

while:

```text
~/.config/kscreenlockerrc
Greeter wallpaper:
file:///home/asus/Pictures/Wallpapers/dharmx-digital/a_car_driving_on_a_road_at_night.png
```

### Proof `plasmashell` was genuinely absent

```bash
qdbus6 org.kde.plasmashell /PlasmaShell
```

Result:

```text
Service 'org.kde.plasmashell' does not exist.
```

Thus Condition B was genuinely a no-`plasmashell` D-Bus state.

### Journal checks

Commands included:

```bash
journalctl --user -u caelestia-shell.service -b --since "10 minutes ago" --no-pager -l | grep -Ei 'plasmashell|qdbus|dbus|wallpaper|error|failed|timeout'
```

and:

```bash
journalctl --user -b --since "10 minutes ago" --no-pager -l | grep -Ei 'plasmashell|qdbus|dbus|wallpaper|error|failed|timeout'
```

No wallpaper-specific `plasmashell` D-Bus error/retry loop was observed.

Other journal messages were observed but not attributed to wallpaper failure, including portal app-ID warning, missing NVIDIA VDPAU backend, KWin electric-border D-Bus warnings, KSplash wait/timeout messages, and crash-recovery messages mentioning old `plasmashell` shortcuts.

### Lock-screen confirmation during Test 2B

The screen was manually locked. The lock screen worked and the lock-screen wallpaper remained synchronized with the desktop wallpaper.

### Verdict

**PASS**

### Architectural conclusion from Test 2

Caelestia's actual wallpaper functionality does **not** require a running `plasmashell` process.

The remaining dependency observed in this test is limited to the Plasma-native wallpaper configuration mirror. Under Condition B, that Plasma desktop config can remain stale without preventing Caelestia's visible wallpaper or lock-screen wallpaper from working.

---

# Test 3 — Lock Screen

## Test 3A — Normal lock/unlock

**Condition:** B — `plasmashell` stopped, Caelestia running

### Shell-package verification

```bash
kreadconfig6 --file plasmashellrc --group Shell --key ShellPackage
```

Result:

```text
caelestia.desktop
```

### Locking

Attempted:

```bash
loginctl lock-session
```

Observed issue: this did not visibly lock the session during testing.

Working manual method:

```text
Meta + L
```

### User-visible result

- Lock screen appeared: **YES**
- Caelestia greeter rendered: **YES**
- Wallpaper correct: **YES**
- Password unlock worked: **YES**
- Desktop returned normally: **YES**

### Journal observation

Observed:

```text
kscreenlocker_greet: ... ProfileAvatar.qml:97:5: QML Image: Cannot open: file:///home/asus/.face
```

This is a missing user-avatar file. It did not prevent the greeter from rendering or unlocking.

### Verdict

**PASS**

### Separate issue

`loginctl lock-session` did not work visibly, while Meta+L did. Treat this as a separate lock-trigger/session-control issue, not as a Caelestia greeter failure.

---

## Test 3B — Shell-package self-heal

**Condition:** B — `plasmashell` stopped

### Test setup

The shell-package setting was deliberately changed away from Caelestia:

```bash
kwriteconfig6 --file plasmashellrc --group Shell --key ShellPackage org.kde.plasma.desktop
```

Then Caelestia was restarted:

```bash
systemctl --user restart caelestia-shell.service
```

### Restart behavior

The laptop briefly showed a black screen and then returned to normal. This was treated as the expected visible effect of restarting the shell.

After restart, `caelestia-shell.service` was `active (running)` with a new Quickshell PID.

### Self-heal result

```bash
kreadconfig6 --file plasmashellrc --group Shell --key ShellPackage
```

returned:

```text
caelestia.desktop
```

`pgrep -a plasmashell` produced no output.

The D-Bus service remained absent:

```bash
qdbus6 org.kde.plasmashell /PlasmaShell
```

returned:

```text
Service 'org.kde.plasmashell' does not exist.
```

### Lock-screen verification after self-heal

Using Meta+L:

- Caelestia greeter appeared: **YES**
- Wallpaper correct: **YES**
- Password unlock worked: **YES**
- Desktop returned: **YES**

### Verdict

**PASS**

### Architectural conclusion from Test 3

The Caelestia lock-screen/greeter integration works while `plasmashell` is fully stopped.

The shell-package self-heal also restores `ShellPackage=caelestia.desktop` after deliberate configuration damage, without requiring `plasmashell` to run.

---

# Test 4 — Launcher

**Condition:** B — `plasmashell` fully stopped, Caelestia running

### Starting state

`pgrep -a plasmashell` produced no output.

`systemctl --user is-active caelestia-shell.service` returned:

```text
active
```

### Launcher repeated open/close test

The launcher was opened and closed for five consecutive cycles.

Results:

```text
1 = YES
2 = YES
3 = YES
4 = YES
5 = YES
```

During the same test, random application-category navigation was performed and worked normally.

### Application search / launch

#### Dolphin

- Found in launcher: **YES**
- Launched: **YES**
- Usable: **YES**

A real file-operation check was performed in Dolphin:

- created a blank test file in Downloads
- renamed it
- copied/pasted it into the Pictures folder

These operations completed normally.

#### System Settings

- Found: **YES**
- Launched: **YES**

#### KCalc

- Found: **YES**
- Launched: **YES**

### Stability observations

- Visual corruption: **NO**
- Input freeze: **NO**
- Crash/restart: **NO**

### Memory observation

Condition B launcher baseline:

```text
MemoryCurrent=742846464 bytes
```

After launcher/search/application interaction:

```text
MemoryCurrent=818397184 bytes
```

Increase:

```text
75,550,720 bytes ≈ 72.0 MiB
```

Quickshell RSS after the test:

```text
615396 KiB
```

The memory increase was recorded for later low-memory analysis; it did not coincide with a crash, freeze, or launcher failure.

### Journal observation

The launcher-filtered journal query showed repeated warnings:

```text
WARN caelestia.config: Tokens.padding accessed without a screen set on Popup_QMLTYPE_286
```

These appeared repeatedly during interaction but did not produce visible launcher failure, input freeze, corruption, or crash.

### Verdict

**PASS**

### Architectural conclusion from Test 4

The launcher is operational and usable without a running `plasmashell`. Search, category navigation, application launching, and real file-management interaction through Dolphin all worked under Condition B. The observed memory increase should be carried forward to the later low-memory/low-GPU analysis rather than treated as a launcher failure.

---

# Memory Baselines Recorded Before Test 4

These are preliminary observations for the later low-memory test, not pass/fail criteria for Tests 2–4.

## Condition A — Caelestia + plasmashell running

```text
systemd Caelestia MemoryCurrent = 743047168 bytes ≈ 708.7 MiB
plasmashell MemoryCurrent       = 89710592 bytes ≈ 85.5 MiB
quickshell RSS                  = 582712 KiB ≈ 569.1 MiB
plasmashell RSS                 = 189976 KiB ≈ 185.5 MiB
```

## Condition B — Caelestia + plasmashell fully stopped

```text
systemd Caelestia MemoryCurrent = 743120896 bytes ≈ 708.8 MiB
plasmashell MemoryCurrent       = [not set]
quickshell RSS                  = 582744 KiB ≈ 569.1 MiB
plasmashell process             = not running
```

Later during Test 4, Caelestia's cgroup memory rose to `818397184` bytes after launcher/application interaction.

---

# Known Non-Blocking / Separate Issues

23. **Test 15:** Caelestia showed a substantially higher memory footprint than the stock-Plasma reference on the same machine. Idle Caelestia cgroup memory was approximately `909 MiB` with quickshell RSS approximately `646 MiB`; later current memory reached approximately `1.09 GiB` during wallpaper interaction. These measurements are retained as performance findings, not classified as a confirmed leak.
24. **Test 15:** Minor animation/transition stutter was observed during launcher interaction, notification sidebar opening, and wallpaper/color propagation. It did not increase over the sustained test and was not accompanied by input lag, freezes, visual corruption, or shell restart.
25. **Test 15:** A controlled approximately `1 GiB` external memory-pressure event increased swap usage to approximately `240.7 MiB` but produced no OOM, Caelestia crash, restart, or loss of usability.

1. `loginctl lock-session` did not visibly lock the session; Meta+L works.
2. Greeter logs a missing `~/.face` avatar warning.
3. Caelestia logs unrelated warnings/errors including portal app-ID registration, missing NVIDIA VDPAU backend, KWin electric-border D-Bus type messages, KSplash wait/timeout messages, and `Tokens.padding`/screen warnings. None has been shown to break Tests 2–4.
4. Under Condition B, Plasma's native desktop wallpaper config can remain stale because the `plasmashell` D-Bus service is unavailable. This did not prevent actual Caelestia wallpaper or lock-screen wallpaper operation.
5. Launcher interaction increased Caelestia memory usage by about 72 MiB in the observed run. This is not currently classified as a failure and should be evaluated during the dedicated low-memory test.

---

# Power Outage / Resume Note

The machine experienced a household power outage after the Test 3B work was performed. The intentionally modified `ShellPackage` setting had already been restored to `caelestia.desktop` before shutdown.

When resuming after a reboot, recreate Condition B before continuing no-`plasmashell` testing:

```bash
pgrep -a plasmashell
```

If `plasmashell` is running:

```bash
kquitapp6 plasmashell
```

Then verify:

```bash
pgrep -a plasmashell
```

Expected: no output.

Verify D-Bus absence:

```bash
qdbus6 org.kde.plasmashell /PlasmaShell
```

Expected:

```text
Service 'org.kde.plasmashell' does not exist.
```

Verify Caelestia:

```bash
systemctl --user status caelestia-shell.service --no-pager
```

Expected: `active (running)`.

---



# Test 5 — Notifications

**Condition:** B — `plasmashell` stopped, Caelestia running

## Initial state

Commands:

```bash
pgrep -a plasmashell
systemctl --user is-active caelestia-shell.service
command -v notify-send
```

Results:

- `pgrep -a plasmashell`: no output; `plasmashell` remained stopped.
- `caelestia-shell.service`: `active`.
- `notify-send`: `/usr/bin/notify-send`.

## Single notification

Command:

```bash
notify-send "Caelestia Test" "Test notification 1 — plasmashell is stopped."
```

Results:

- Notification popup appeared: **YES**
- Title displayed correctly: **YES**
- Message displayed correctly: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**

## Multiple notifications

Notifications generated with `notify-send`:

- `Notification Test` — **YES**
- `Important Test` — **YES**
- `Application Test` — **YES**

Results:

1. First notification: **YES**
2. Second notification: **YES**
3. Third notification: **YES**
4. Multiple notifications displayed correctly: **YES**
5. Visual overlap/corruption: **NO**

## Notification sidebar

Results:

- Notification sidebar opens: **YES**
- Notifications visible in sidebar: **YES**
- Notification contents correct: **YES**
- Sidebar responsive: **YES**
- Visual corruption: **NO**

## Repeated sidebar open/close

Five repeated cycles were performed:

```text
Cycle 1 = YES
Cycle 2 = YES
Cycle 3 = YES
Cycle 4 = YES
Cycle 5 = YES
```

## Dismissal

Results:

- Dismiss works: **YES**
- Notification disappears: **YES**
- Sidebar remains usable afterward: **YES**

## Notification while application is open

Dolphin was opened through the Caelestia launcher. A notification was then generated while Dolphin remained open.

Result:

- Notification worked normally: **YES**

## Journal observations

Commands used:

```bash
journalctl --user -u caelestia-shell.service -b --since "15 minutes ago" --no-pager -l | grep -Ei 'notif|notification|error|failed|crash|qml'
```

and:

```bash
journalctl --user -b --since "15 minutes ago" --no-pager -l | grep -Ei 'org.freedesktop.Notifications|notification|plasmashell|dbus|error|failed|timeout'
```

The logs contained several unrelated/general warnings, including the known missing `~/.face` warning and screen/token warnings.

More importantly, the notification component itself produced repeated QML `TypeError` warnings during notification interaction, for example:

```text
@modules/notifications/Notification.qml[...] TypeError: Cannot read property 'actions' of null
@modules/notifications/Notification.qml[...] TypeError: Cannot read property 'urgency' of null
@modules/notifications/Notification.qml[...] TypeError: Cannot read property 'body' of null
@modules/notifications/Notification.qml[...] TypeError: Cannot read property 'summary' of null
@modules/notifications/Notification.qml[...] TypeError: Cannot read property 'appName' of null
@modules/notifications/Notification.qml[...] TypeError: Cannot read property 'hints' of null
@modules/notifications/Notification.qml[...] TypeError: Cannot read property 'image' of null
@modules/notifications/Notification.qml[...] TypeError: Cannot read property 'appIcon' of null
```

These warnings occurred at approximately 13:47 and again at 13:48 while the notification feature was being exercised.

No corresponding user-visible notification failure, freeze, corruption, or shell crash was observed during the test.

### Interpretation

The notification feature **functionally passed the live tests**, but the logs expose a real notification-side QML defect condition: `Notification.qml` is attempting to read properties from a null object during notification handling.

This should be carried forward as a **known bug to investigate**, not ignored as a generic unrelated warning. Because the visible behavior remained correct, it does not invalidate the functional PASS, but it prevents treating Test 5 as a completely clean run.

## Memory observation

End-of-test measurements:

```text
systemctl --user show caelestia-shell.service -p MemoryCurrent
MemoryCurrent=881094656
```

and:

```text
ps -C quickshell -o pid=,rss=,vsz=,cmd=
 10807 703140 3122904 /usr/bin/quickshell -n -p /home/asus/.config/quickshell/caelestia/shell.qml
```

This memory value is recorded for the later dedicated low-memory analysis. It is not treated as a Test 5 failure by itself.

## Verdict

**PASS — FUNCTIONAL, WITH A KNOWN NOTIFICATION QML BUG**

### Architectural conclusion from Test 5

Notifications are operational under Condition B with no running `plasmashell`. Notification delivery, multiple notifications, sidebar display, dismissal, repeated sidebar use, and notification delivery while an application is open all worked.

The remaining issue is an internal QML null-object condition in `modules/notifications/Notification.qml`, which should be investigated separately because it may represent an underlying robustness bug even though the tested user-visible flows currently succeed.

---


# Test 6 — Tray / System Tray

**Condition:** B — `plasmashell` fully stopped, Caelestia running

### Condition verification

Commands:

```bash
pgrep -a plasmashell
systemctl --user is-active caelestia-shell.service
qdbus6 org.kde.plasmashell /PlasmaShell
```

Results:

- `pgrep -a plasmashell`: **no output**
- `systemctl --user is-active caelestia-shell.service`: `active`
- `qdbus6 org.kde.plasmashell /PlasmaShell`: `Service 'org.kde.plasmashell' does not exist.`

Condition B was therefore confirmed.

### Tray contents

The user observed **10 visible bar/tray-area items**:

1. CachyOS Update
2. Caelestia Update (name reported by user; exact label not confirmed)
3. Clock
4. Wi-Fi
5. Bluetooth
6. Volume
7. Battery
8. Notification
9. Show Desktop
10. Power Button / power menu

All observed interactive items opened and worked normally **except the Clock**, which did not open anything. The user noted this may be intentional behavior in the current Caelestia version; this behavior has not been independently verified from source and is therefore recorded without classifying it as a failure.

### Tray interaction

For the first two selected items:

- Item 1 opens: **YES**
- Item 1 works: **YES**
- Item 1 closes: **YES**
- Item 2 opens: **YES**
- Item 2 works: **YES**
- Item 2 closes: **YES**

The remaining observed interactive items also opened normally according to the user's test.

Repeated tray interaction:

```text
Cycle 1 = YES
Cycle 2 = YES
Cycle 3 = YES
Cycle 4 = YES
Cycle 5 = YES
```

### Tray with Dolphin open

- Tray works with Dolphin open: **YES**
- Tray remains responsive: **YES**
- Dolphin remains usable: **YES**

### Stability

- Visual corruption: **NO**
- Input freeze: **NO**
- Caelestia crash/restart: **NO**

### Memory observation

End-of-test cgroup memory:

```text
systemctl --user show caelestia-shell.service -p MemoryCurrent
MemoryCurrent=885444608
```

This is approximately **844.4 MiB**.

Quickshell RSS:

```text
707412 KiB ≈ 690.8 MiB
```

Compared with the Test 5 end-of-test cgroup value of `881094656` bytes, this is an increase of about **4.1 MiB**. This is recorded for the later dedicated low-memory analysis and is not considered a Test 6 failure.

### Journal observations

Tray-related Caelestia logging showed normal debug/performance messages such as:

```text
[perf][TrayMenu] updateGroups ... groups=3 rebuildCount=1
[perf][TrayMenu] open/update latency ...
```

One Caelestia QML warning was observed:

```text
WARN scene: QML QQuickItem at @modules/bar/popouts/Content.qml[47:13]: Binding loop detected for property "height"
```

This did not cause visible tray failure, input freeze, visual corruption, or shell restart, so it is recorded as a **tray/bar QML bug candidate**, not a functional test failure.

While Dolphin was open, the system journal also showed:

```text
dolphin: Failed to check which JobView API is supported "The name is not activatable"
```

This is a Dolphin/JobView message rather than evidence of a Caelestia tray failure.

### Verdict

**PASS — FUNCTIONAL, WITH A TRAY/BAR QML WARNING**

### Architectural conclusion from Test 6

The Caelestia tray/bar integration is operational while `plasmashell` is fully stopped. Multiple tray-area items are visible, interactive tray items open and close correctly, repeated interaction succeeds, and tray use remains functional while Dolphin is running. No tray-related crash, input freeze, or visual corruption was observed.

A QML binding-loop warning in `modules/bar/popouts/Content.qml` should be carried forward for later investigation. The Clock's non-opening behavior was observed but not independently verified as intentional.

---


# Test 7 — Overview

**Condition:** B — `plasmashell` fully stopped, Caelestia running

## Condition verification

Commands used:

```bash
pgrep -a plasmashell
systemctl --user is-active caelestia-shell.service
qdbus6 org.kde.plasmashell /PlasmaShell
```

Results:

- `plasmashell` stopped: **YES** — no process output.
- `caelestia-shell.service`: **active**.
- `org.kde.plasmashell` D-Bus service: **unavailable** — `Service 'org.kde.plasmashell' does not exist.`

## Overview interaction

Overview was opened using the Caelestia Overview shortcut (`Meta+Tab`).

Results:

- Opens: **YES**
- Windows visible: **YES**
- Workspace information visible: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**

## Multiple-window test

Multiple applications/windows were present during testing.

- Windows shown correctly: **YES**
- Can select a window: **YES**
- Selected window becomes active: **YES**

## Close test

- Overview closes normally: **YES**
- Desktop returns normally: **YES**

## Repeated open/close test

Five consecutive Overview open/close cycles:

```text
Cycle 1 = YES
Cycle 2 = YES
Cycle 3 = YES
Cycle 4 = YES
Cycle 5 = YES
```

## Window switching

- Window switching works: **YES**
- Focus changes correctly: **YES**

## Workspace test

Workspace switching worked using the actual configured Caelestia shortcuts:

```text
Meta+1 through Meta+0
```

Observed:

- Switch right/forward: **YES**
- Switch left/back: **YES**
- Overview reflects the current workspace: **YES**

**Correction:** `Ctrl+Alt+Right/Left` was initially suggested, but the test machine's active shortcut is `Meta+1` through `Meta+0`.

## Stability

- Crash/restart: **NO**
- Visual corruption: **NO**
- Input freeze: **NO**

## Memory observation

The submitted test log contains two different memory figures at different points:

```text
MemoryCurrent=884609024
```

and later:

```text
MemoryCurrent=798793728
```

The corresponding quickshell RSS measurement was:

```text
798793728 KiB
```

The values are preserved exactly as reported rather than silently reconciled. For future memory comparisons, capture `MemoryCurrent` and `ps` RSS in the same command/time point so the units and timing are unambiguous.

These measurements are for the later dedicated low-memory analysis and are not a Test 7 pass/fail criterion.

## Journal observations

The Overview/window interaction journal query did not show an Overview crash or explicit Overview failure.

Caelestia produced repeated debug messages from `WindowScreencastRequest` / `WindowScreencastGlobal`, including successful binding and creation of window streams. These are consistent with the Overview/window preview path being active during the test and did not correspond to a visible failure.

The following unrelated or pre-existing warnings/messages were also present:

```text
KWin: Skipped method "borderActivated" : Type not registered with QtDBus ... KWin::ElectricBorder
System Settings: Failed to connect to Bolt manager DBus interface
System Settings: Failed to register with host portal ... app ID ...
WirePlumber: ... link failed: some node was destroyed before the link was created
```

They were not observed to break the Overview or workspace switching tests.

## Verdict

**PASS**

## Architectural conclusion from Test 7

Caelestia's Overview and workspace/window integration are operational under Condition B with no running `plasmashell`. The Overview rendered windows and workspace information, supported selecting/focusing windows, survived five repeated open/close cycles, and correctly followed workspace changes. No crash, input freeze, or visual corruption was observed.

The test also exercised Caelestia's native KWin/window-preview path, with successful `WindowScreencast` debug events appearing in the log.

# Test 8 — Clipboard History

**Condition:** B — `plasmashell` fully stopped, Caelestia running

## Condition verification

At the beginning of the test, `plasmashell` was initially running after the session resumed. It was stopped with:

```bash
kquitapp6 plasmashell
```

Verification then showed:

```text
pgrep -a plasmashell
    no output

qdbus6 org.kde.plasmashell /PlasmaShell
    Service 'org.kde.plasmashell' does not exist.

systemctl --user status caelestia-shell.service --no-pager
    Active: active (running)
```

Condition B was therefore confirmed before the clipboard test.

## Clipboard backend

Commands:

```bash
command -v wl-copy
command -v wl-paste
command -v kate
command -v kwrite
```

Results:

```text
/usr/bin/wl-copy
/usr/bin/wl-paste
/usr/bin/kate
/usr/bin/kwrite
```

The system therefore had both Wayland clipboard utilities and Kate available for the live test.

## Clipboard history population

Three distinct clipboard entries were created:

```bash
printf '%s' 'CAELESTIA-CLIPBOARD-ONE' | wl-copy
sleep 1
printf '%s' 'CAELESTIA-CLIPBOARD-TWO' | wl-copy
sleep 1
printf '%s' 'CAELESTIA-CLIPBOARD-THREE' | wl-copy
```

The active clipboard was then verified with:

```bash
wl-paste --no-newline
echo
```

Result:

```text
CAELESTIA-CLIPBOARD-THREE
```

The Caelestia Clipboard History UI was opened with **Meta+V**. The three generated entries were available in the history.

## Selecting an older clipboard entry

The older entry:

```text
CAELESTIA-CLIPBOARD-ONE
```

was selected from the Caelestia clipboard history. The active clipboard was then checked with `wl-paste` and returned:

```text
CAELESTIA-CLIPBOARD-ONE
```

This confirms that Caelestia can retrieve/select an older history entry and make it the active clipboard.

## Actual application paste

A real application paste test was performed with Kate. The temporary file was created with:

```bash
printf '%s\n' 'PASTE-TARGET-EMPTY' > /tmp/caelestia-clipboard-test.txt
```

The clipboard was set to:

```text
CAELESTIA-CLIPBOARD-ONE
```

Kate opened the file:

```bash
kate /tmp/caelestia-clipboard-test.txt
```

The existing contents were replaced through the normal application paste operation, the file was saved, and the saved file was verified with:

```bash
cat /tmp/caelestia-clipboard-test.txt
```

Result:

```text
CAELESTIA-CLIPBOARD-ONE
```

This confirms that a value selected/used through the clipboard flow can be pasted successfully into a normal KDE application.

## Repeated open/close/use test

Five repeated Clipboard History uses were performed:

```text
Cycle 1 = YES
Cycle 2 = YES
Cycle 3 = YES
Cycle 4 = YES
Cycle 5 = YES
```

The user reported that each cycle worked normally.

One UI behavior was observed: opening Clipboard History with **Meta+V** works, but pressing **Meta+V** again does not close the UI. Clicking the desktop closes it. This was not accompanied by a freeze or crash. It is recorded as a behavior/bug candidate rather than a clipboard functionality failure.

## Search test

Clipboard History search was tested several times. Search successfully found older entries, including the generated test entries.

## Stability / journal check

Command used:

```bash
journalctl --user -u caelestia-shell.service -b --since "15 minutes ago" --no-pager -l \
  | grep -Ei 'clipboard|clip|error|failed|crash|qml'
```

No clipboard-specific error, crash, or failure was reported by the filtered output. The output mainly contained pre-existing/general warnings such as:

```text
WARN caelestia.config: Tokens.padding accessed without a screen set on Popup_QMLTYPE_355
WARN caelestia.services.weather: Failed to fetch auto location from ipinfo: ... status code 429
```

These were not observed to cause clipboard history failure.

## Final Condition B verification

Final checks returned:

```text
NO plasmashell process
active
Service 'org.kde.plasmashell' does not exist.
```

Condition B remained intact at the end of the test.

## Verdict

**PASS — FUNCTIONAL, WITH A CLIPBOARD UI CLOSE/TOGGLE QUIRK**

## Architectural conclusion from Test 8

Caelestia Clipboard History is operational with `plasmashell` fully stopped. It captures multiple clipboard entries, displays history, allows an older entry to be selected and restored to the active clipboard, supports search, and successfully pastes clipboard content into a normal KDE application. Repeated use remained stable.

The only feature-specific issue observed was that **Meta+V is not a toggle for closing Clipboard History** in the tested session; clicking the desktop closes it. This should be retained as a later UI/interaction investigation item.

# Test 9 — Screenshot UI

**Condition:** B — `plasmashell` fully stopped, Caelestia running

## 9.1 — Screenshot environment

Condition verification returned:

```text
plasmashell: NOT RUNNING
Caelestia: active
plasmashell D-Bus: Service 'org.kde.plasmashell' does not exist.
```

Screenshot-related programs available:

```text
spectacle: /usr/bin/spectacle
grim: /usr/bin/grim
slurp: /usr/bin/slurp
```

Portal state:

```text
xdg-desktop-portal: active
xdg-desktop-portal-kde: inactive
```

The desktop portal D-Bus service exposed `org.freedesktop.portal.Screenshot`, including `Screenshot()` and `PickColor()` methods.

## 9.2 — Region screenshot

Caelestia screenshot UI was opened with:

```text
Meta + Shift + S
```

The screenshot interface opened with a region selector and the following controls:

```text
Window Selector
Screenshot
Google Lens
Text Recognition
Full Screen Screenshot
Close
```

A region was selected. The capture occurred automatically, the screenshot interface closed, and a notification reported that the image was saved.

Saved file:

```text
/home/asus/Pictures/Screenshots/screenshot-2026-09-27_08.04.11.png
```

The user verified the resulting image in Dolphin and confirmed it was the expected screenshot. File size was `320387` bytes.

Result:

- Screenshot UI opens: **YES**
- Region selection works: **YES**
- Automatic capture works: **YES**
- Interface closes after capture: **YES**
- Notification appears: **YES**
- Saved image is usable/correct: **YES**

## 9.3 — Full-screen screenshot

The `Full Screen Screenshot` control was tested from the Caelestia screenshot UI.

Result:

- Capture: **YES**
- Interface closed automatically: **YES**
- Notification appeared: **YES**
- Correct full-screen image: **YES** — verified in Dolphin

Resulting file:

```text
/home/asus/Pictures/Screenshots/screenshot-2026-09-27_08.10.31.png
```

File size: `605889` bytes.

## 9.4 — Window selector

`Window Selector` was tested by selecting Dolphin.

Result:

- Capture: **YES**
- Interface closed automatically: **YES**
- Notification appeared: **YES**
- Only selected window captured: **YES** — verified in Dolphin

Resulting file:

```text
/home/asus/Pictures/Screenshots/screenshot-2026-09-27_08.13.52.png
```

File size: `59478` bytes.

## 9.5 — Repeated screenshot invocation

Six repeated region-capture cycles were completed by the user:

```text
Cycle 1 = YES
Cycle 2 = YES
Cycle 3 = YES
Cycle 4 = YES
Cycle 5 = YES
Cycle 6 = YES
```

At the end of the test, `~/Pictures/Screenshots` contained **10 image files**. The latest files all had non-zero sizes, including:

```text
73008 bytes
5875 bytes
6273 bytes
16936 bytes
14809 bytes
6231 bytes
23501 bytes
```

No capture failure, shell crash, or visible instability was reported.

## Journal observations

Command used:

```bash
journalctl --user -u caelestia-shell.service -b --since "30 minutes ago" --no-pager -l \
  | grep -Ei 'screenshot|portal|spectacle|grim|slurp|error|failed|crash|qml'
```

The journal showed repeated `spectacle` messages:

```text
spectacle: static bool KX11Extras::compositingActive() may only be used on X11
```

These messages appeared during the screenshot captures while the session was running Wayland. They did not prevent region, full-screen, or window capture from succeeding and are therefore recorded as a **non-blocking warning** rather than a test failure.

Other messages in the filter were general Caelestia warnings such as `Tokens.spacing/padding accessed without a screen set` and a weather HTTP 429 response. No screenshot-specific error or crash was observed.

## Final Condition B verification

The final checks returned:

```text
plasmashell: NOT RUNNING
Caelestia: active
plasmashell D-Bus: Service 'org.kde.plasmashell' does not exist.
```

Condition B remained intact throughout and at the end of Test 9.

## Verdict

**PASS — FUNCTIONAL, WITH A NON-BLOCKING SPECTACLE WAYLAND/X11 WARNING**

## Architectural conclusion from Test 9

Caelestia's screenshot functionality works under Condition B with no running `plasmashell`. The screenshot UI, region capture, full-screen capture, and window-target capture all succeeded, produced usable files, and remained stable across six repeated invocations. The successful captures provide direct live evidence that the screenshot path does not require a running `plasmashell`.

The journal shows Caelestia launching/using `spectacle` during captures. The environment's `xdg-desktop-portal-kde` service was inactive, but the desktop portal was active and exposed the standard Screenshot interface; no screenshot failure resulted from that state.

The only screenshot-specific warning observed was Spectacle's `KX11Extras::compositingActive()` message under Wayland. This should be retained for later investigation but does not invalidate the functional PASS.


# Test 10 — Screen recording

**Condition:** B — `plasmashell` fully stopped, Caelestia running

## 10.1 — Screen-recording environment

Initial Condition B verification returned:

```text
plasmashell: NOT RUNNING
Caelestia: active
plasmashell D-Bus: Service 'org.kde.plasmashell' does not exist.
```

Recording-related tools:

```text
spectacle: /usr/bin/spectacle
ffmpeg: /usr/bin/ffmpeg
wf-recorder: not found
```

The desktop portal was active and exposed the standard `org.freedesktop.portal.ScreenCast` interface, including `CreateSession`, `SelectSources`, `Start`, and `OpenPipeWireRemote`.

`xdg-desktop-portal-kde.service` was inactive, but the general desktop portal was active.

## 10.2 — Caelestia recording UI

Shortcut tested:

```text
Meta + Ctrl + S
```

The Caelestia recording UI opened successfully. Available source choices were:

```text
Main Screen
Share Virtual Screen
Share Region
```

A UI behavior was observed: pressing `Meta + Ctrl + S` again did not close the recording UI.

## 10.3 — Caelestia Main Screen recording

`Main Screen` was selected. Recording started automatically.

Observed:

- Recording start: **YES**
- Start notification: **YES**
- Recording indicator/tray item: **YES**
- Tray item provided an `End` action: **YES**
- Recording ended automatically after approximately 26 seconds during this run: **YES**

## 10.4 — Main Screen recording file verification

Recording produced:

```text
/home/asus/Videos/Recordings/recording_20260927_13-57-27.mp4
```

File size:

```text
786520 bytes
```

`ffprobe` reported:

```text
codec_name=h264
codec_type=video
width=1366
height=768
r_frame_rate=1000000/16659
filename=/home/asus/Videos/Recordings/recording_20260927_13-57-27.mp4
duration=26.066651
size=786520
```

The file was opened manually in Haruna and visually verified:

```text
Video opens: YES
Video plays normally: YES
Recorded desktop content is correct: YES
Visible corruption/artifacts: NO
Playback reaches the end: YES
```

An attempted command-line launch using `haruna` was not successful, but manual opening and playback worked. This was not treated as a recording failure.

## 10.5 — Caelestia Share Region recording

`Share Region` was tested.

Observed:

- Share Region starts: **YES**
- Region selection works: **YES**
- Start notification: **YES**
- Recording indicator/tray item: **YES**
- Stop via `End` action: **NOT CONFIRMED** — the recording ended before the tray action could be tested
- Recording ended cleanly: **YES**, but unexpectedly after less than approximately 2 seconds

Two resulting files were produced:

```text
/home/asus/Videos/Recordings/recording_20260927_14-12-49.mp4
/home/asus/Videos/Recordings/recording_20260927_14-13-40.mp4
```

Both files were only `88` bytes and were not usable recordings.

This is a **real Caelestia Share Region recording failure**.

## 10.6 — KDE Spectacle recording comparison

Spectacle was tested separately using the working system shortcut:

```text
Meta + R
```

Observed:

- Spectacle recorder starts: **YES**
- Start notification: **YES**
- Recording tray control appears: **YES**
- Tray stop button stops the recording correctly: **YES**
- A separate recording of approximately **55 seconds** was made and manually verified by the user: **YES**
- Video playback: **YES**
- Video content: **correct**
- Visible corruption/artifacts: **NO**

This demonstrates that screen recording works on the same Wayland session while `plasmashell` is stopped, independently of Caelestia's native recording implementation.

## 10.7 — Architectural decision

The project's functional requirement is screen recording capability while keeping `plasmashell` out of the runtime. That requirement is satisfied by the existing KDE Spectacle application.

Caelestia's native recorder is therefore treated as **redundant rather than required infrastructure** for this project:

- Caelestia Main Screen recording: **functional**
- Caelestia Share Region recording: **broken** — unusable 88-byte files
- KDE Spectacle recording: **functional and manually verified with a 55-second recording**

The Share Region defect should remain documented as a Caelestia bug candidate, but it does not need to become a shell-development blocker. The project can retain Spectacle as the system screenshot/recording application instead of duplicating capture functionality inside Caelestia.

## Verdict

**PASS — FUNCTIONAL VIA SPECTACLE; CAELESTIA NATIVE RECORDER PARTIALLY FUNCTIONAL**

### Architectural conclusion from Test 10

Screen recording is available under Condition B without a running `plasmashell`. The Caelestia native recorder works for Main Screen capture but fails for Share Region capture. KDE Spectacle provides a working independent recording path, including successful start/stop behavior and a manually verified 55-second recording.

For the target architecture, Spectacle should be retained as the external KDE screenshot/recording component. There is no current need to repair or maintain duplicate screen-recording functionality inside the Caelestia shell unless a later project requirement specifically demands Caelestia-owned recording UI.


# Test 11 — OSD

**Condition:** B — `plasmashell` fully stopped, Caelestia running

## 11.1 — Condition verification

Commands used:

```bash
echo "=== plasmashell ==="
pgrep -a plasmashell || echo "NOT RUNNING"

echo "=== Caelestia ==="
systemctl --user is-active caelestia-shell.service

echo "=== plasmashell D-Bus ==="
qdbus6 org.kde.plasmashell /PlasmaShell
```

Results:

```text
=== plasmashell ===
NOT RUNNING
=== Caelestia ===
active
=== plasmashell D-Bus ===
Service 'org.kde.plasmashell' does not exist.
```

Condition B was confirmed before the OSD test.

## 11.2 — OSD control availability and baseline

Commands:

```bash
command -v wpctl
command -v brightnessctl
```

Results:

```text
/usr/bin/wpctl
/usr/bin/brightnessctl
```

Initial values:

```bash
wpctl get-volume @DEFAULT_AUDIO_SINK@
brightnessctl get
brightnessctl max
```

Observed:

```text
Volume: 0.81
7492
65535
```

After volume interaction, a later baseline read returned:

```text
Volume: 0.75
7492
65535
```

## 11.3 — Volume OSD

A volume-up action was tested using the laptop's normal hardware control.

Results:

- OSD appears: **YES**
- Volume value/indicator updates: **YES**
- OSD disappears normally: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**
- Caelestia crash/restart: **NO**

## 11.4 — Mute OSD

The laptop's normal mute control was tested and then toggled back to restore the previous audio state.

Results:

- Mute OSD appears: **YES**
- Mute state changes: **YES**
- Unmute state restores: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**

## 11.5 — Brightness OSD

The laptop's normal brightness controls were tested.

Results:

- Brightness OSD appears: **YES**
- Brightness changes: **YES**
- Repeated changes work: **YES**
- OSD closes normally: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**

## 11.6 — Journal check

Commands used:

```bash
journalctl --user -u caelestia-shell.service -b --since "10 minutes ago" --no-pager -l \
  | grep -Ei 'osd|volume|brightness|audio|wpctl|pulse|pipewire|error|failed|crash|qml'
```

and:

```bash
journalctl --user -b --since "10 minutes ago" --no-pager -l \
  | grep -Ei 'osd|volume|brightness|audio|wpctl|pulse|pipewire|plasmashell|dbus|error|failed|timeout|crash'
```

Both filtered commands returned no output.

No OSD-specific error, failure, crash, timeout, or QML warning was observed in the captured journal window.

## 11.7 — Final Condition B verification

Commands:

```bash
pgrep -a plasmashell || echo "plasmashell: NOT RUNNING"
systemctl --user is-active caelestia-shell.service
qdbus6 org.kde.plasmashell /PlasmaShell
```

Results:

```text
plasmashell: NOT RUNNING
active
Service 'org.kde.plasmashell' does not exist.
```

Condition B remained intact at the end of Test 11.

## Verdict

**PASS**

### Architectural conclusion from Test 11

Caelestia's OSD functionality is operational with `plasmashell` fully stopped. Volume, mute, and brightness OSDs all appeared and responded correctly under Condition B. Brightness changes were repeatable, the mute state could be restored, and no visual corruption, input freeze, shell crash/restart, or OSD-specific journal error was observed.

# Test 11 — OSD

**Condition:** B — `plasmashell` fully stopped, Caelestia running

## 11.1 — Condition verification

Commands:

```bash
echo "=== plasmashell ==="
pgrep -a plasmashell || echo "NOT RUNNING"

echo "=== Caelestia ==="
systemctl --user is-active caelestia-shell.service

echo "=== plasmashell D-Bus ==="
qdbus6 org.kde.plasmashell /PlasmaShell
```

Results:

```text
plasmashell: NOT RUNNING
Caelestia: active
Service 'org.kde.plasmashell' does not exist.
```

Condition B was confirmed before the OSD test.

## 11.2 — OSD controls

Commands:

```bash
command -v wpctl
command -v brightnessctl
wpctl get-volume @DEFAULT_AUDIO_SINK@
brightnessctl get
brightnessctl max
```

Results:

```text
/usr/bin/wpctl
/usr/bin/brightnessctl
Volume: 0.81
7492
65535
```

Both volume and brightness control tools were available.

## 11.3 — Volume OSD

The laptop volume controls were tested.

Results:

- OSD appeared: **YES**
- Volume value/indicator updated: **YES**
- OSD disappeared normally: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**
- Caelestia crash/restart: **NO**

After the test, `wpctl get-volume @DEFAULT_AUDIO_SINK@` reported:

```text
Volume: 0.75
```

## 11.4 — Mute OSD

Results:

- Mute OSD appeared: **YES**
- Mute state changed: **YES**
- Unmute restored the previous state: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**

## 11.5 — Brightness OSD

Results:

- Brightness OSD appeared: **YES**
- Brightness changed: **YES**
- Repeated brightness changes worked: **YES**
- OSD closed normally: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**

## 11.6 — Repeated OSD use

Volume and brightness controls were exercised repeatedly during the test. No visible OSD instability, input freeze, corruption, or shell restart was observed.

## 11.7 — Journal check

Commands used:

```bash
journalctl --user -u caelestia-shell.service -b --since "10 minutes ago" --no-pager -l \
  | grep -Ei 'osd|volume|brightness|audio|wpctl|pulse|pipewire|error|failed|crash|qml'
```

and:

```bash
journalctl --user -b --since "10 minutes ago" --no-pager -l \
  | grep -Ei 'osd|volume|brightness|audio|wpctl|pulse|pipewire|plasmashell|dbus|error|failed|timeout|crash'
```

Both filtered journal commands produced no output.

No OSD-specific journal error, failure, or crash was observed.

## 11.8 — Final Condition verification

Final checks returned:

```text
plasmashell: NOT RUNNING
active
Service 'org.kde.plasmashell' does not exist.
```

Condition B remained intact at the end of the test.

## Verdict

**PASS**

### Architectural conclusion from Test 11

Caelestia's OSD functionality works under Condition B without a running `plasmashell`. Volume, mute, and brightness OSDs appeared and updated correctly, repeated use remained stable, and no OSD-specific error or crash was found in the filtered journal.


# Test 12 — Workspace Switching

**Condition:** B — `plasmashell` fully stopped, Caelestia running

## 12.1 — Condition verification

Commands:

```bash
echo "=== plasmashell ==="
pgrep -a plasmashell || echo "NOT RUNNING"

echo "=== Caelestia ==="
systemctl --user is-active caelestia-shell.service

echo "=== plasmashell D-Bus ==="
qdbus6 org.kde.plasmashell /PlasmaShell

echo "=== KWin current desktop ==="
qdbus6 org.kde.KWin /KWin currentDesktop
```

Results:

```text
plasmashell: NOT RUNNING
Caelestia: active
Service 'org.kde.plasmashell' does not exist.
KWin current desktop: 1
```

Condition B was confirmed before workspace testing.

## 12.2 — Virtual desktop state

Commands used:

```bash
qdbus6 org.kde.KWin /VirtualDesktopManager count
qdbus6 org.kde.KWin /VirtualDesktopManager current
```

Observed results:

```text
count = 5
current = 32be46a8-3b96-430f-a966-503434bcd714
```

KWin also logged that the `count` and `current` D-Bus slots were unavailable for this interface:

```text
QDBusConnection: couldn't handle call to count, no slot matched
Could not find slot VirtualDesktopManagerAdaptor::count
QDBusConnection: couldn't handle call to current, no slot matched
Could not find slot VirtualDesktopManagerAdaptor::current
```

These messages describe the D-Bus queries used during the test, not a failure of workspace switching itself. The actual workspace operations were tested successfully through KWin/Caelestia.

## 12.3 — Window setup

Two test applications were opened:

```text
Dolphin
Kate
```

A third application, KCalc, was used later to verify workspace separation.

## 12.4 — Workspace switching

Using the normal KDE workspace switching shortcuts:

```text
Meta + Ctrl + Right
Meta + Ctrl + Left
```

Results:

- Switch to another workspace: **YES**
- Return to original workspace: **YES**
- Caelestia updates workspace presentation: **YES**
- Visual corruption: **NO**
- Input freeze: **NO**

## 12.5 — Window separation

The test placed KCalc on the second workspace and switched back to the original workspace.

Results:

- Dolphin visible on original workspace: **YES**
- Kate visible on original workspace: **YES**
- KCalc absent from original workspace: **YES**
- KCalc visible on the second workspace: **YES**

This confirms that workspace separation and window placement remained correct while `plasmashell` was stopped.

## 12.6 — Repeated switching

Five or more complete workspace-switching cycles were performed. The user then performed additional cycles beyond the required five.

```text
Cycle 1 = YES
Cycle 2 = YES
Cycle 3 = YES
Cycle 4 = YES
Cycle 5 = YES
Additional cycles = YES
```

No workspace desynchronization, input freeze, visual corruption, or shell restart was observed.

## 12.7 — Caelestia Overview integration

Caelestia Overview was opened using:

```text
Meta + Tab
```

Results:

- Overview shows workspace state correctly: **YES**
- Selecting a window on another workspace switches correctly: **YES**
- Selected window receives focus: **YES**
- Overview closes normally: **YES**

Approximately seven additional Overview/workspace cycles were performed successfully.

## 12.8 — Journal observations

Caelestia-filtered journal output contained only the already-known token/screen warning:

```text
WARN caelestia.config: Tokens.padding accessed without a screen set on Popup_QMLTYPE_145
```

No workspace-specific Caelestia error or crash was observed.

The broader journal also contained unrelated or separate messages:

```text
Libinput: ... Touchpad: client bug: event processing lagging behind by 99ms, your system is too slow
Skipped method "borderActivated" : Type not registered with QtDBus in parameter list: KWin::ElectricBorder
WirePlumber: ... link failed to activate
```

The touchpad lag message is a performance observation from the running low-end system, not evidence of workspace switching failure. The KWin electric-border message is retained because screen-edge testing is still a later dedicated test. The WirePlumber link messages occurred outside the workspace-specific path and were not associated with a workspace failure.

## 12.9 — Final Condition verification

Final checks returned:

```text
plasmashell: NOT RUNNING
Caelestia: active
Service 'org.kde.plasmashell' does not exist.
```

Condition B remained intact throughout and at the end of Test 12.

## Verdict

**PASS**

### Architectural conclusion from Test 12

KWin workspace switching and Caelestia's workspace presentation remain fully operational under Condition B with no running `plasmashell`. Switching between five configured virtual desktops worked repeatedly, windows stayed associated with the correct workspace, and Caelestia Overview correctly followed workspace/window changes and focus.



# Test 13 — Screen edges / hot corners

**Condition:** B — `plasmashell` fully stopped, Caelestia running

## 13.1 — Condition verification

Results:

```text
plasmashell: NOT RUNNING
Caelestia: active
plasmashell D-Bus: Service 'org.kde.plasmashell' does not exist.
```

Condition B was confirmed before the test.

## 13.2 — Caelestia saved screen-edge state

The Caelestia state file was present:

```text
/home/asus/.config/caelestia/stolen-screen-edges.json
```

Contents included:

```text
[
    {
        "corner": 7,
        "entries": {
            "Effect-overview": {
                "BorderActivate": null
            }
        }
    }
]
```

This confirms that Caelestia had saved KWin edge state for later restoration.

## 13.3 — KWin electric-border configuration

The relevant KWin configuration contained:

```text
BorderActivate=9
```

No manual changes were made to the configuration during this test.

## 13.4 — Normal hot-corner / screen-edge behavior

The user tested the configured active areas:

```text
Top-left   = opens the workspace/Overview-style interface
Top-right  = opens Quick Toggle when the pointer is slightly left of the extreme corner; the extreme corner itself did not trigger an action
Bottom-left = no action
Bottom-right = no action
Top-center = opens Dashboard
```

Observed results:

- Hot corner/edge activates: **YES**
- Expected Caelestia/KWin action occurs: **YES**
- Action completes normally: **YES**
- Pointer remains responsive: **YES**
- Visual corruption: **NO**

The active edge/corner actions were then repeated for five cycles:

```text
Cycle 1 = YES
Cycle 2 = YES
Cycle 3 = YES
Cycle 4 = YES
Cycle 5 = YES
```

## 13.5 — Pre-crash Caelestia state

Before the unclean-exit test:

```text
caelestia-shell.service: active (running)
Main PID: 1278 (quickshell)
```

The relevant process was:

```text
1278 /usr/bin/quickshell -n -p /home/asus/.config/quickshell/caelestia/shell.qml
```

## 13.6 — Unclean Caelestia exit

The Caelestia shell was deliberately terminated with:

```bash
kill -9 1278
```

Observed immediately afterward:

- Caelestia shell disappeared: **YES**
- KWin remained usable: **YES** — pointer remained movable and responsive
- Desktop became temporarily black: **YES**
- Shell automatically returned: **YES** — after approximately 5 seconds

The systemd-managed Caelestia service automatically restarted. A new Quickshell PID was:

```text
107595
```

The service was active/running after recovery.

## 13.7 — KWin screen-edge state after unclean exit

After the crash/restart:

```text
~/.config/kwinrc
BorderActivate=9
```

The Caelestia saved state file still contained the saved `corner: 7` / `Effect-overview` entry.

Most importantly, Caelestia's crash-recovery log explicitly reported:

```text
DEBUG: [Caelestia] Crash recovery: restoring screen corner 7
```

The earlier KWin workspace tracker disconnect/reconnect messages were also observed during the restart, showing that KWin noticed the shell-side tracker disappear and then reconnect.

## 13.8 — Hot-corner behavior after recovery

After Caelestia automatically restarted:

- Top-left works after recovery: **YES**
- Top-right / Quick Toggle works: **YES**
- Top-center / Dashboard works: **YES**
- Pointer remains responsive: **YES**
- Visual corruption: **NO**

## 13.9 — Final Condition B verification

Final checks returned:

```text
plasmashell: NOT RUNNING
Caelestia: active
Service 'org.kde.plasmashell' does not exist.
```

Condition B therefore remained intact after the unclean shell termination and automatic recovery.

## Journal observations

The Caelestia service journal recorded the deliberate `SIGKILL` and automatic recovery:

```text
caelestia-shell.service: Main process exited, code=killed, status=9/KILL
caelestia-shell.service: Failed with result 'signal'.
caelestia-shell.service: Scheduled restart job, restart counter is at 1.
```

Caelestia then reported crash recovery actions, including:

```text
Crash recovery: restoring screen corner 7
```

KWin's workspace tracker temporarily reported socket disconnect/connection-refused messages while Caelestia was down, then successfully reconnected after the shell restarted.

Other messages in the same time window included:

```text
Failed to open VDPAU backend libvdpau_nvidia.so: cannot open shared object file: No such file or directory
Skipped method "borderActivated" : Type not registered with QtDBus in parameter list: KWin::ElectricBorder
Source file does not exist: /home/asus/.face
qt.network.http2: ... HTTP/2 protocol error
```

These were not associated with a screen-edge failure. The missing `.face` warning is already known, and the VDPAU, portal/network, and electric-border QtDBus messages are retained as separate/non-blocking issues.

## Verdict

**PASS — FUNCTIONAL, INCLUDING UNCLEAN-EXIT RECOVERY**

### Architectural conclusion from Test 13

Caelestia's screen-edge/hot-corner integration works under Condition B without a running `plasmashell`. The configured active areas performed their expected actions repeatedly, and the pointer remained responsive without visual corruption.

The deliberate `kill -9` test also passed the recovery requirement: systemd automatically restarted Caelestia, the shell returned after approximately five seconds, Caelestia's crash-recovery logic explicitly restored screen corner 7, and the tested hot-corner actions continued working afterward. `plasmashell` remained stopped throughout the recovery.

# Test 14 — Stock Plasma Baseline

**Reference environment:** Stock KDE Plasma with Caelestia shell service disabled and `plasmashell` using `org.kde.plasma.desktop`

### Stock Plasma restoration / starting state

The Caelestia user service was disabled and stopped:

```bash
systemctl --user disable --now caelestia-shell.service
```

Verification:

```text
systemctl --user is-active caelestia-shell.service
inactive

pgrep -a quickshell
NO QUICKSHELL

pgrep -a plasmashell
28383 /usr/bin/plasmashell --no-respawn
```

The existing panel-less Plasma applet configuration was moved aside:

```bash
mv ~/.config/plasma-org.kde.plasma.desktop-appletsrc \\
   ~/.config/plasma-org.kde.plasma.desktop-appletsrc.caelestia-panelless
```

`plasmashell` initially remained panel-less because `~/.config/plasmashellrc` still contained:

```text
ShellPackage=caelestia.desktop
```

A backup was made and the shell package was changed to the stock Plasma package:

```bash
cp -a ~/.config/plasmashellrc \\
      ~/caelestia-test-backup/plasmashellrc.before-stock

kwriteconfig6 \\
  --file ~/.config/plasmashellrc \\
  --group Shell \\
  --key ShellPackage \\
  org.kde.plasma.desktop
```

Verification:

```text
org.kde.plasma.desktop
```

`plasmashell` was then restarted with:

```bash
systemctl --user restart plasma-plasmashell.service
```

After restart, the normal Plasma panel returned with the application launcher, task manager, system tray, and normal Plasma desktop behavior. The desktop wallpaper also returned to the image used before the Caelestia installation.

### Stock Plasma visual/state verification

The user observed:

- Normal Plasma panel visible: **YES**
- Application launcher on panel: **YES**
- Task manager on panel: **YES**
- System tray on panel: **YES**
- Normal Plasma desktop right-click: **YES**
- Pre-Caelestia wallpaper visible: **YES**

The panel also contained a workspace switcher between the launcher and pinned task-manager applications. Because the system was a fresh CachyOS installation before Caelestia was installed, this was retained as part of the observed stock baseline rather than removed.

### Test 14A — Idle memory baseline

Initial measurement at `Mon Sep 28 08:07:28 AM WITA 2026`:

```text
plasmashell: 31106 /usr/bin/plasmashell --no-respawn
quickshell: NO QUICKSHELL
caelestia-shell.service: inactive

Memory:
  total        3.7Gi
  used         1.4Gi
  free         254Mi
  shared       55Mi
  buff/cache   2.5Gi
  available    2.3Gi

Swap:
  /dev/zram0  3.7G total, 60.9M used

Plasma Shell cgroup:
  MemoryCurrent=255774720
  MemoryPeak=259215360

Plasmashell process:
  RSS=371788 KiB
  VSZ=1718440 KiB
```

After approximately two minutes of idle time:

```text
Memory:
  total        3.7Gi
  used         1.4Gi
  free         283Mi
  shared       53Mi
  buff/cache   2.5Gi
  available    2.3Gi

Swap:
  /dev/zram0  3.7G total, 63.3M used

Plasma Shell cgroup:
  MemoryCurrent=258338816
  MemoryPeak=261812224

Plasmashell process:
  RSS=374312 KiB
  VSZ=1706360 KiB
```

Idle cgroup memory therefore remained around `244–246 MiB`, while the process RSS remained around `363–366 MiB`. Swap use stayed near `61–63 MiB` during the idle observation.

### Test 14B — Representative shell interaction

The following stock Plasma functions were tested:

- Launcher: **YES**
- Overview: **YES**
- Notifications: **YES**
- Desktop/wallpaper UI: **YES**
- Virtual desktop switching: **YES**
- Input responsiveness: **YES**
- Visible stutter: **NO**
- Animation stutter: **NO**
- Blur/effects stutter: **NO**
- Visual corruption: **NO**

The normal Plasma Overview was usable manually, including through the panel/taskbar interface. The user reported that `Meta+W` did not invoke stock Plasma Overview and the top-left corner did not invoke it either. These inputs appeared to have been affected by residual Caelestia shortcut/screen-edge configuration. This did not prevent the Overview feature itself from being opened and tested manually.

Post-interaction measurement:

```text
Memory:
  total        3.7Gi
  used         1.6Gi
  free         751Mi
  shared       33Mi
  buff/cache   1.7Gi
  available    2.1Gi

Swap:
  /dev/zram0  3.7G total, 120.8M used

Plasma Shell cgroup:
  MemoryCurrent=370733056
  MemoryPeak=541233152

Plasmashell process:
  RSS=477412 KiB
  VSZ=2166560 KiB
```

Compared with the approximately two-minute idle measurement, the post-interaction Plasma cgroup memory was about `112 MiB` higher, while the process RSS was about `103 MiB` higher. The cgroup peak reached `541233152` bytes (about `516 MiB`) during the interaction window.

Swap use increased from `63.3M` at idle to `120.8M` after interaction. No shell instability, input freeze, visual corruption, or abnormal frame pacing was observed during this increase.

The largest processes in the captured snapshots included `plasmashell`, `kwin_wayland`, Jellyfin, `kded6`, `kdeconnectd`, and other normal KDE/session services. The background Jellyfin process is retained as part of the real machine baseline and was not treated as Plasma Shell memory.

### Test 14C — Frame pacing / animation observation

The following qualitative frame-pacing checks were performed under stock Plasma:

```text
Overview animation:   SMOOTH
Desktop switching:    SMOOTH
Launcher animation:   SMOOTH
Window movement:      SMOOTH
Scrolling:            SMOOTH
Overall frame pacing: SMOOTH
```

No visible stutter, animation stutter, blur/effects stutter, input lag sufficient to disrupt interaction, or visual corruption was observed during the representative shell interaction sequence.

### Verdict

**PASS — STOCK REFERENCE BASELINE ESTABLISHED**

### Architectural conclusion from Test 14

A usable stock KDE Plasma reference environment was successfully restored on the same machine with the Caelestia shell service disabled and `plasmashell` loading `org.kde.plasma.desktop`.

The measured stock-Plasma idle cgroup memory was approximately `244–246 MiB`, rising to approximately `354 MiB` after representative shell interaction, with a measured cgroup peak of approximately `516 MiB`. Plasmashell RSS rose from approximately `363–366 MiB` at idle to approximately `466 MiB` after interaction. Swap use increased from approximately `61–63 MiB` at idle to approximately `121 MiB` after interaction.

The stock Plasma shell and representative animations remained responsive and visually smooth throughout the observed run. These values now provide the required same-machine reference for the later Caelestia low-memory/low-GPU comparison.

A residual Caelestia shortcut/screen-edge configuration was still present during the stock session: `Meta+W` and the top-left Overview trigger did not work, while Overview itself remained accessible manually. This is recorded as a configuration-contamination observation rather than a stock Plasma feature failure.

---


# Test 15 — Low-memory and low-GPU stress behavior

**Condition:** B — `plasmashell` fully stopped, Caelestia running

**`caelestia-kde` commit:** `be4188f3c6d2f1f5212981818f692ec53e87c1b3` (`be4188f`)

## 15.1 — Condition verification

Before the test, the session was restored to the actual target Condition B:

```text
plasmashell: NOT RUNNING
Caelestia: active
Service 'org.kde.plasmashell' does not exist.
```

The `ShellPackage` value remained:

```text
caelestia.desktop
```

## 15.2 — Initial idle measurement

The synchronized initial snapshot was captured at `09:51:47 WITA` after Caelestia had already been running for approximately 57 minutes.

```text
Memory:
  total        3.7Gi
  used         1.8Gi
  free         862Mi
  shared       29Mi
  buff/cache   1.4Gi
  available    1.9Gi

Swap:
  /dev/zram0  3.7G total, 180.4M used

Caelestia cgroup:
  MemoryCurrent=952557568
  MemoryPeak=990846976

Quickshell:
  RSS=661176 KiB
  VSZ=3006612 KiB
  CPU=1.3%

KWin:
  RSS=211328 KiB
  VSZ=1366068 KiB
```

A second idle measurement after approximately two minutes showed:

```text
MemoryCurrent=952823808
MemoryPeak=990846976
Quickshell RSS=661180 KiB
Swap=180.4M
```

The two-minute idle measurement was therefore stable. Caelestia's idle cgroup memory was approximately `909 MiB` and quickshell RSS was approximately `646 MiB`.

For comparison, the stock-Plasma Test 14 reference recorded approximately `244–246 MiB` of `plasmashell` cgroup memory and approximately `363–366 MiB` of `plasmashell` RSS at idle. These are different process/cgroup scopes, so the values are retained as a practical same-machine reference rather than treated as perfectly identical accounting domains.

## 15.3 — Launcher interaction

Five complete launcher open/close/search cycles were performed.

Observed:

```text
Input lag:          NO
Visible stutter:    YES — slight
Animation stutter:  YES — slight
Temporary freeze:   NO
Visual corruption:  NO
Launcher failure:   NO
Caelestia restart:  NO
```

Post-interaction measurement:

```text
MemoryCurrent=968839168
MemoryPeak=990846976
Quickshell RSS=657304 KiB
Swap=180.2M
```

Current Caelestia cgroup memory was approximately `924 MiB`. The cgroup peak did not increase beyond the existing idle peak during this phase.

## 15.4 — Overview interaction

Five complete Overview open/close cycles were performed.

Observed:

```text
Overview opening animation:          GOOD
Overview closing animation:          GOOD
Window preview movement:             GOOD
Pointer responsiveness:              GOOD
Scrolling/movement inside Overview:  GOOD
Visible stutter:                     NO
Animation stutter:                   NO
Temporary freeze:                    NO
Visual corruption:                   NO
```

Post-interaction measurement:

```text
MemoryCurrent=983367680
MemoryPeak=1031749632
Quickshell RSS=647436 KiB
Swap=180.2M
```

Caelestia cgroup memory was approximately `938 MiB`, with a peak of approximately `984 MiB`. Swap remained effectively unchanged.

## 15.5 — Notifications interaction

Five notifications were generated with `notify-send`, followed by notification sidebar opening/closing and dismissal interaction.

Observed:

```text
Notification popup appears:   YES
Notification contents:        YES
Sidebar opens:                YES
Sidebar closes:               YES
Dismissal works:              YES
Input responsiveness:         YES
Visible stutter:              YES — slight, mainly when opening the sidebar from the taskbar notification icon
Temporary freeze:             NO
Visual corruption:            NO
Caelestia restart:            NO
```

The test was interrupted by a ChatGPT usage limit between the interaction and the later measurement; this was an external conversation interruption rather than elapsed continuous test time. The measurement was taken when testing resumed.

Post-interaction measurement:

```text
MemoryCurrent=991981568
MemoryPeak=1031749632
Quickshell RSS=684776 KiB
Swap=154.8M
```

The notification-specific journal filter did not reproduce the earlier Test 5 `Notification.qml` null-property errors during this run. The captured warning was instead:

```text
WARN caelestia.config: Tokens.sizes accessed without a screen set on QQmlConnections_QML_1199
```

No notification-related crash, failure, or restart was observed.

Because the measurement was separated from the notification interaction by the conversation interruption, the RSS increase is **not** attributed solely to the notification phase.

## 15.6 — Wallpaper UI interaction

The wallpaper UI was opened and several wallpaper thumbnails/previews were browsed.

Observed:

```text
Wallpaper UI opens:       YES
Thumbnails load:          YES
Preview movement:         GOOD
Visible stutter:          NO
Animation stutter:        YES — MINOR
Input lag:                NO
Temporary freeze:         NO
Visual corruption:        NO
Caelestia restart:        NO
```

The minor animation stutter was associated with Caelestia immediately applying the selected wallpaper and deriving the initial wallpaper color, followed by a short delay before the taskbar/window colors finished updating. This was recorded as a minor transition effect rather than a general rendering failure.

Post-wallpaper measurement at `12:40:33 WITA`:

```text
MemoryCurrent=1173618688
MemoryPeak=1360588800
Quickshell RSS=686088 KiB
Swap=154.6M
```

Caelestia cgroup memory reached approximately `1.09 GiB`, with a peak of approximately `1.27 GiB`.

After the wallpaper UI was closed and the desktop was left untouched for approximately two minutes, the measurement remained approximately:

```text
MemoryCurrent=1172688896
MemoryPeak=1360588800
Quickshell RSS=743968 KiB
Swap=154.5M
```

This showed that the elevated memory remained after the wallpaper UI closed and settled for two minutes. It was not classified as a confirmed memory leak because later testing showed that the current cgroup memory could fall again substantially.

## 15.7 — Combined repeated shell interaction

Three complete rounds were performed, each consisting of:

```text
Launcher → search → close
Overview → window movement → close
Notification sidebar → close
Wallpaper UI → browse → close
```

Observed:

```text
Input lag:          NO
Visible stutter:    YES — slight, same as earlier
Animation stutter:  YES — slight, same as earlier
Temporary freeze:   NO
Visual corruption:  NO
Feature failure:    NO
Caelestia restart:  NO
```

After the combined workload:

```text
MemoryCurrent=941031424
MemoryPeak=1522212864
Quickshell RSS=733192 KiB
Swap=184.9M
```

Current Caelestia cgroup memory fell to approximately `897 MiB`, while the cumulative cgroup peak increased to approximately `1.42 GiB`.

This showed that the wallpaper-associated high current memory value was not monotonically retained under later workload. The current cgroup memory was able to fall back below the earlier idle measurements, although quickshell RSS remained elevated compared with the original idle snapshot.

## 15.8 — Controlled 1 GiB memory-pressure test

`stress-ng` and `stress` were not installed on the machine. The initial Bash-style heredoc attempt therefore failed because the session uses `fish`:

```text
fish: Expected a string, but found a redirection
```

No memory-pressure test was executed by that failed command.

A fish-compatible Python test was then used to allocate and actively touch approximately `1 GiB` of memory for 30 seconds, after which the allocation was released.

Observed during the pressure window:

```text
Desktop remains responsive:     YES
Visible stutter:                YES — slight, but less than previous test
Mouse movement responsive:      YES
Temporary freeze:               NO
Caelestia disappears/restarts:  NO
Desktop becomes unusable:       NO
```

Immediately after the allocation was released:

```text
MemoryCurrent=784732160
MemoryPeak=1522212864
Quickshell RSS=684316 KiB
Swap=240.7M
```

Caelestia remained operational. The pressure phase increased swap usage to approximately `240.7 MiB`, but did not produce a shell crash or visible system failure.

After a further two minutes of idle time:

```text
MemoryCurrent=783527936
MemoryPeak=1522212864
Quickshell RSS=694896 KiB
Swap=240.7M
```

Swap remained stable at approximately `240.7 MiB` after the pressure event rather than continuing to rise during the observed recovery period.

## 15.9 — Pressure-related journal check

Commands filtered the Caelestia service journal and kernel journal for OOM, out-of-memory, killed-process, memory-cgroup, crash, restart, and signal events.

Results:

```text
Caelestia pressure journal errors:  NONE
Kernel OOM events:                  NONE
Caelestia service:                  active
plasmashell:                        NOT RUNNING
```

No OOM event, shell crash, or automatic Caelestia restart was observed.

## 15.10 — Sustained frame-pacing test

A three-minute sustained sequence repeatedly exercised:

```text
Overview → Launcher → Notification sidebar → Workspace switch
```

Observed:

```text
Input lag:                      NO
Visible stutter:                NO
Animation stutter:              YES — tiny bit slow
Stutter increased over time:    NO
Temporary freeze:               NO
Visual corruption:              NO
Workspace/UI desynchronization: NO
Caelestia restart:              NO
```

Final measurement after the sustained sequence:

```text
MemoryCurrent=798949376
MemoryPeak=1522212864
Quickshell RSS=707560 KiB
Swap=239.5M
```

Caelestia current cgroup memory was approximately `761 MiB`, while the cumulative peak remained approximately `1.42 GiB`. Swap remained near `240 MiB`.

## Verdict

**PASS — FUNCTIONAL UNDER LOW-MEMORY / LOW-GPU STRESS, WITH HIGH MEMORY FOOTPRINT AND MINOR ANIMATION STUTTER**

## Architectural conclusion from Test 15

Caelestia remained usable with `plasmashell` fully stopped throughout the low-memory and low-GPU testing. Launcher, Overview, notifications, wallpaper UI, repeated mixed interaction, and workspace-related activity did not produce a crash, input freeze, visual corruption, or shell restart.

The test did expose a materially higher memory footprint than the stock-Plasma reference. Caelestia's synchronized idle cgroup memory was approximately `909 MiB`, with quickshell RSS around `646 MiB`, versus the stock Plasma reference of approximately `244–246 MiB` `plasmashell` cgroup memory and `363–366 MiB` `plasmashell` RSS. These values use different process/cgroup scopes and should therefore be treated as practical same-machine reference measurements rather than exact apples-to-apples accounting.

Wallpaper interaction produced the largest observed current-memory increase, reaching approximately `1.09 GiB` current and approximately `1.27 GiB` peak immediately afterward. Later combined interaction reduced current memory again to approximately `897 MiB`, demonstrating that the high wallpaper reading was not simply monotonic accumulation.

A controlled approximately `1 GiB` external memory-pressure event was tolerated without OOM, shell crash, or loss of usability. The observed cgroup peak reached approximately `1.42 GiB` during the broader stress run, and swap usage rose to approximately `240 MiB` after pressure and remained stable during the observed recovery period.

Frame pacing remained generally usable on the low-end AMD A4-9125/Radeon R3 system. The only recurring issue was a small amount of animation/transition stutter, most noticeable around launcher/notification/sidebar transitions and wallpaper color propagation. During Overview and the three-minute sustained mixed sequence, no visible stutter was observed; the final sustained observation described the animations as only slightly slow.

Test 15 therefore establishes that the current Caelestia build can remain operational under significant memory pressure and sustained low-end hardware interaction without `plasmashell`, but its memory footprint is substantially larger than the stock Plasma shell reference and minor animation latency should remain documented for later optimization work.

---


---

# 16. Recovery via SSH

## 16.1 — SSH availability and Condition B verification

Condition B was confirmed before recovery testing:

```text
plasmashell: NOT RUNNING
Caelestia: active
Service 'org.kde.plasmashell' does not exist.
```

SSH verification:

- `sshd.service` was active and running.
- SSH listening was confirmed through the service journal.
- A second SSH session successfully connected while Caelestia was active.

Result: **PASS**

## 16.2 — Caelestia interruption test

Caelestia was intentionally stopped through SSH:

```bash
systemctl --user stop caelestia-shell.service
```

Observed:

- `caelestia-shell.service` stopped cleanly.
- The graphical shell disappeared and the screen became black while the cursor remained active.
- SSH access remained available.
- Commands continued to execute remotely.

Result: **PASS**

## 16.3 — Manual recovery test

Caelestia was restarted through SSH:

```bash
systemctl --user start caelestia-shell.service
```

Observed:

- Service returned to `active (running)`.
- Desktop returned.
- Caelestia bar/widgets returned.
- No reboot or reinstall was required.

Result: **PASS**

## 16.4 — Post-recovery verification

Post-recovery state:

```text
caelestia-shell.service: active (running)
Main PID: 47120 (quickshell)
MemoryCurrent: ~879 MiB
MemoryPeak: ~919 MiB
```

Recovery journal showed Caelestia crash-recovery restoration actions including shortcut and screen-corner restoration.

Existing warnings remained:

- Missing NVIDIA VDPAU backend
- Missing `/home/asus/.face` image
- QML cache/image warnings

These were not recovery blockers.

## Test 16 Verdict

**PASS — SSH recovery and Caelestia shell recovery verified**

Recovery path is functional under Condition B.

## 16.5 — Full session restart test (follow-up)

This follow-up tests session-level recovery (a full logout/login cycle), not just the shell service, and specifically checks whether `loginctl terminate-session` is a valid way to simulate a full restart for a systemd-user-managed Plasma/Caelestia session.

**Attempt 1 — `loginctl terminate-session <id>` on a live Condition B session:**

```bash
loginctl list-sessions          # identified the seat0 session, e.g. session 4
kquitapp6 plasmashell           # re-confirmed Condition B immediately before the test
loginctl terminate-session 4
```

Observed: the laptop screen went black for about a second, then returned to the KDE/PLM login screen — this looked like a clean logout.

A fresh login was then attempted at the greeter. Result: **the login hung** — the screen froze immediately after entering the password, with the cursor unresponsive. SSH remained reachable throughout, so recovery was possible without a reboot.

Diagnosis over SSH:

```bash
loginctl list-sessions
ps -u asus -o pid,ppid,stat,pcpu,etimes,cmd --sort=-pcpu | head -40
systemctl --user list-units 'plasma*' 'caelestia*' --all --no-pager
```

Findings:

- The new login's session did exist (`user` class on `seat0`/`tty3`), but its `startplasma-wayland` process was idle at 0% CPU, doing nothing.
- The *old* session's `kwin_wayland`, `quickshell`, and supporting processes were all still running, with `ELAPSED` times going back to before `terminate-session` was run.
- `systemctl --user list-units` confirmed `plasma-kwin_wayland.service`, `plasma-ksmserver.service`, and `caelestia-shell.service` were all still `active (running)`.

Root cause: `kwin_wayland`, `ksmserver`, and `caelestia-shell` run as **systemd user units**, and a systemd user manager (`systemd --user`) is shared per-UID, not scoped to an individual login session. `loginctl terminate-session` ends the logind session object, but does not stop those units — so the old compositor never released the GPU/display, and the new login's compositor hung waiting behind it. **This means `loginctl terminate-session` is not a valid way to test a full session restart for this session type** — it is a test-methodology limitation, not a Caelestia defect.

Recovery (no reboot required):

```bash
systemctl --user stop plasma-workspace-wayland.target
```

The screen went black briefly and returned to the greeter. A follow-up check confirmed the old processes were genuinely gone this time (`pgrep -a kwin_wayland/quickshell/plasmashell` returned nothing but the greeter's own `--no-lockscreen` compositor). A subsequent login then succeeded cleanly, with fresh PIDs for `kwin_wayland`, `quickshell`, and `plasmashell`, and a working `org.kde.plasmashell` D-Bus interface (Condition A, as expected for a fresh login).

**Attempt 2 — real logout via Caelestia's own UI, on the now-clean session:**

An attempt to trigger a scripted logout via `qdbus6 org.kde.ksmserver /KSMServer logout 0 0 1` failed with `Cannot find '.logout' in object /KSMServer at org.kde.ksmserver`. This documented KSMServer D-Bus call appears to no longer match this Plasma 6.7.5 build's interface; not investigated further since Caelestia's own Logout button provided a valid alternative test path.

Caelestia's **Logout** button was clicked directly from the UI. Observed: clean drop to the login screen, no delay or visual issue.

Verification over SSH immediately after, before logging back in:

```bash
pgrep -a kwin_wayland; pgrep -a quickshell; pgrep -a plasmashell
```

Result: `quickshell` and `plasmashell` were both gone; only the greeter's own `kwin_wayland` (`--no-lockscreen --no-global-shortcuts --no-kactivities`) remained. A subsequent login came up cleanly with fresh PIDs for `kwin_wayland`, `quickshell`, and `plasmashell`, and no manual repair needed.

**16.5 Verdict: PASS (via real logout).** A genuine logout, using Caelestia's own UI, correctly stops the full stack and allows a clean subsequent login. The earlier hang under `loginctl terminate-session` is attributed to that command not being equivalent to a real logout for this session type, not to a Caelestia defect. `loginctl terminate-session` should not be used again as a stand-in for a full session-restart test.

One minor leftover: after this sequence, `loginctl list-sessions` continued to show a stale entry (the earlier session, by TTY) with no live processes attached. Treated as a cosmetic logind bookkeeping artifact from the manual recovery path, not a functional issue; expected to clear on the next reboot.

## 16.6 — SSH reachable before login (boot-time check)

Performed via a real reboot, not inferred from config.

Pre-check:

```bash
nmcli -f NAME,TYPE,AUTOCONNECT connection show
nmcli -g connection.permissions connection show "Richard's A35"
```

The active Wi-Fi profile ("Richard's A35") returned empty `connection.permissions`, meaning it is system-wide, not tied to a specific user account.

Confirmed directly with a real reboot:

```bash
sudo reboot
```

SSH was retried immediately from a second device. Result: `No route to host` for the first couple of retries during boot, then a successful connection at roughly the 2-minute mark, matching the reported uptime.

Verified this was genuinely pre-login, not post-login:

```bash
loginctl list-sessions
```

The seat0 session (`c1`) was still class `greeter` at the time of the successful SSH connection — confirming SSH was reachable before any graphical login occurred.

**16.6 Verdict: PASS.** Wi-Fi and SSH are both available before login. This is the confirmed safety net for Test 1's boot-time method.

---

# Test 1 — Session Startup

The final item on the test plan: verifying startup behavior with `plasmashell` removed from the boot process itself (masked at the systemd level before login), rather than killed after a normal login as in all prior tests.

## 1.1 — Pre-flight dependency check

Before masking, checked what depends on `plasma-plasmashell.service`:

```bash
systemctl --user list-dependencies --reverse plasma-plasmashell.service
```
Result: only `graphical-session.target`.

```bash
systemctl --user show plasma-plasmashell.service -p WantedBy,RequiredBy,PartOf,BoundBy
```
Result:
```
PartOf=graphical-session.target
RequiredBy=
WantedBy=
BoundBy=
```
`PartOf` is one-directional (plasmashell stops when the target stops, not the reverse); `RequiredBy`/`WantedBy`/`BoundBy` are all empty, so nothing depends on plasmashell to start. Masking it was not expected to cascade into other units.

## 1.2 — Masking and first boot

```bash
systemctl --user mask plasma-plasmashell.service
```
Result: `Created symlink '/home/asus/.config/systemd/user/plasma-plasmashell.service' → '/dev/null'`. Masked while still sitting at the greeter — the following login was a genuine cold start with plasmashell already removed from the boot process, not a live kill after the fact.

Logged in at the physical keyboard. Observed:

- Splash screen for roughly 10 seconds (visually), then a normal, usable desktop.
- No hang, no loop, no crash.
- Hovering the cursor to trigger Caelestia's dashboard/edge behavior worked immediately.
- Launcher opened, category switching worked, search worked.

Verification over SSH:

```bash
pgrep -a plasmashell || echo "NOT RUNNING (expected — masked)"
systemctl --user is-active caelestia-shell.service
pgrep -a kwin_wayland
systemctl --user is-enabled plasma-plasmashell.service
journalctl -b --no-pager | grep -i ksplash | tail -20
```

Result: `plasmashell` not running (expected), `caelestia-shell.service` active, `kwin_wayland` running with fresh PIDs, mask confirmed still in place. The KSplash log surfaced two unrelated findings, not blockers:

- `ReferenceError: bottomRect is not defined` in `Splash.qml` — a bug in the `CachyOS-Nord` splash theme itself, unrelated to plasmashell or Caelestia.
- The same recurring portal app-ID registration failure already logged in earlier tests.

Exact timing, via systemd's own timestamps:

```bash
systemctl --user show caelestia-shell.service plasma-kwin_wayland.service -p ActiveEnterTimestamp
```
Result: KWin active at `13:18:36`, Caelestia active at `13:18:42` — a **6-second** handoff. (`plasma-ksplash.service` separately logged 31.9s of total wall-clock CPU accounting; this doesn't match the ~10s visual splash duration, most likely because the `ksplashqml` process lingered doing background cleanup after the splash visually cleared. Not investigated further — the KWin→Caelestia handoff is the number that reflects actual time-to-usable-desktop.)

## 1.3 — Reproducibility (second boot)

Rebooted a second time to confirm the result wasn't a fluke, per the doc's own pass rule (must work more than once).

```bash
sudo reboot
```

Confirmed genuinely pre-login again via `loginctl list-sessions` (seat0 still `greeter`) before logging in, and confirmed `plasmashell` still masked and absent, `caelestia-shell.service` inactive, only the greeter's own `kwin_wayland` running — all correct for the pre-login state.

Logged in. Result: **same clean outcome as the first boot** — no hang, normal desktop.

```bash
systemctl --user show caelestia-shell.service plasma-kwin_wayland.service -p ActiveEnterTimestamp
```
Result: KWin active at `13:34:20`, Caelestia active at `13:34:27` — a **7-second** handoff, consistent with the first boot's 6 seconds.

## Test 1 Verdict

**PASS — confirmed on two independent reboots.** Caelestia KDE starts and runs correctly with `plasmashell` masked at the systemd level before any login — not merely killed after a normal Condition A login, but genuinely absent from the boot process itself. This is the core question the entire test plan was built to answer, and it now has a reproducible, source-independent, boot-time confirmation, not just a live-kill approximation.

---

# Current Overall Status

| Test | Condition | Result |
|---|---|---|
| Test 1 — Session startup (boot-time, plasmashell masked) | B (boot-time) | **PASS — confirmed on 2 independent reboots** |
| Test 2A — Wallpaper | A | **PASS** |
| Test 2B — Wallpaper | B | **PASS** |
| Test 3A — Lock screen | B | **PASS** |
| Test 3B — Self-heal | B | **PASS** |
| Test 4 — Launcher | B | **PASS** |
| Test 5 — Notifications | B | **PASS — functional; QML bug observed** |
| Test 6 — Tray / System Tray | B | **PASS — functional; QML warning observed** |
| Test 7 — Overview | B | **PASS** |
| Test 8 — Clipboard history | B | **PASS — functional; close/toggle behavior noted** |
| Test 9 — Screenshot UI | B | **PASS — functional; Wayland/X11 Spectacle warning observed** |
| Test 10 — Screen recording | B | **PASS — functional via Spectacle; Caelestia Share Region failure recorded** |
| Test 11 — OSD | B | **PASS** |
| Test 12 — Workspace Switching | B | **PASS** |
| Test 13 — Screen edges / hot corners | B | **PASS — functional; unclean-exit recovery verified** |
| Test 14 — Stock Plasma baseline | Stock Plasma | **PASS — reference baseline established** |
| Test 15 — Low-memory and low-GPU stress behavior | B | **PASS — functional under stress; high memory footprint and minor animation stutter observed** |
| Test 16 — Recovery via SSH (shell-level) | B | **PASS — SSH recovery and Caelestia shell recovery verified** |
| Test 16.5 — Full session restart | B | **PASS — via real logout; `loginctl terminate-session` found invalid for this test (see 16.5)** |
| Test 16.6 — SSH reachable before login | Boot-time | **PASS — confirmed via real reboot** |

**All planned tests are now complete.**

# Known Non-Blocking / Separate Issues

1. `loginctl lock-session` did not visibly lock the session; Meta+L works.
2. Greeter logs a missing `~/.face` avatar warning.
3. Caelestia logs unrelated warnings/errors including portal app-ID registration, missing NVIDIA VDPAU backend, KWin electric-border D-Bus type messages, KSplash wait/timeout messages, and token/screen warnings.
4. Under Condition B, Plasma's native desktop wallpaper config can remain stale because the `plasmashell` D-Bus service is unavailable. This did not prevent actual Caelestia wallpaper or lock-screen wallpaper operation.
5. Launcher interaction increased Caelestia memory usage by about 72 MiB in the observed Test 4 run; this is reserved for the dedicated low-memory test.
6. **Test 5:** `modules/notifications/Notification.qml` repeatedly emitted `TypeError: Cannot read property ... of null` warnings during notification interaction. No visible notification failure occurred, but this is a genuine feature-specific bug candidate and should be investigated later.
7. **Test 6:** `modules/bar/popouts/Content.qml[47:13]` emitted a `Binding loop detected for property \"height\"` warning during tray/bar interaction. No visible tray failure occurred.
8. **Test 6:** Dolphin emitted `Failed to check which JobView API is supported \"The name is not activatable\"` while Dolphin was open; this was not associated with a tray malfunction.
9. **Test 7:** The live report contained two different `MemoryCurrent` values (`884609024` and `798793728`) plus a quickshell RSS value of `798793728`; retain both and recapture synchronized measurements during the dedicated memory test.
10. **Test 8:** `Meta+V` opens the Caelestia clipboard history, but pressing `Meta+V` again does not close it during the observed run; clicking the desktop closes it. Record this as a UI behavior/bug candidate, not a functional clipboard failure.
11. **Test 8:** No clipboard-specific error, crash, or failure was visible in the filtered journal. Repeated `Tokens.padding accessed without a screen set` warnings and weather HTTP 429 warnings were present but were not tied to clipboard failure.
12. **Test 9:** `spectacle` repeatedly logged `static bool KX11Extras::compositingActive() may only be used on X11` during Wayland screenshot capture. Screenshots still captured correctly; this is recorded as a non-blocking compatibility warning, not a screenshot failure.
13. **Test 9:** `xdg-desktop-portal.service` was active and the portal exposed `org.freedesktop.portal.Screenshot`; `xdg-desktop-portal-kde.service` was inactive during the test. Screenshot capture nevertheless worked through the Caelestia screenshot path. No failure attributable to the inactive KDE portal backend was observed.
14. **Test 10:** Caelestia `Share Region` recording produced unusable `88`-byte MP4 files and stopped unexpectedly within approximately 2 seconds.
15. **Test 10:** Pressing `Meta + Ctrl + S` again did not close the Caelestia recording UI during the observed run.

---
16. **Test 11:** Volume, mute, and brightness OSDs all worked under Condition B. The filtered OSD-specific journal queries produced no output, so no OSD-specific error was observed.
17. **Test 12:** KWin logged that the `VirtualDesktopManager` D-Bus `count` and `current` slots were unavailable when queried. These were interface-query messages only; actual workspace switching and workspace/window integration worked normally.
18. **Test 12:** KWin logged a touchpad event-processing lag of `99ms` with the message `your system is too slow`. This is retained as a low-end hardware/performance observation and was not associated with workspace failure.
19. **Test 12:** WirePlumber emitted repeated PipeWire link-activation failures during the test window. No workspace, Caelestia, or input failure was observed in connection with these messages; retain them for later system-level/performance investigation if they recur.
20. **Test 13:** KWin emitted temporary WorkspaceTracker socket disconnect/connection-refused messages while Caelestia was intentionally killed. The tracker reconnected successfully after automatic Caelestia restart; no persistent workspace failure was observed.
21. **Test 13:** The unclean-exit recovery produced the existing KWin `borderActivated` QtDBus type-registration warning, but the screen-edge/hot-corner actions continued to work.
22. **Test 14:** After restoring `ShellPackage=org.kde.plasma.desktop`, stock Plasma returned with its normal panel, launcher, task manager, tray, and pre-Caelestia wallpaper. The `Meta+W` shortcut and top-left Overview trigger still appeared affected by residual Caelestia shortcut/screen-edge configuration, although Overview itself remained functional through manual access.
23. **Test 16:** Restarting Caelestia restored the desktop successfully after intentional shell interruption. Post-recovery memory usage was higher than the pre-interruption baseline; retain this as a memory behavior observation for future optimization work.
24. **Test 16.5:** `loginctl terminate-session` does not stop the systemd user units (`plasma-kwin_wayland.service`, `plasma-ksmserver.service`, `caelestia-shell.service`) because the systemd user manager is shared per-UID, not scoped to an individual login session. It should not be used to simulate a full session restart; use the actual Logout action, or `systemctl --user stop plasma-workspace-wayland.target` for manual recovery. This is a testing-methodology finding, not a Caelestia defect.
25. **Test 16.5:** The documented `qdbus org.kde.ksmserver /KSMServer logout <confirm> <type> <mode>` D-Bus call failed with `Cannot find '.logout' in object /KSMServer` on this Plasma 6.7.5 build. The interface appears to have changed from older KDE4/5-era documentation. Not investigated further since the UI Logout button provided a working test path.
26. **Test 16.5:** After the `loginctl terminate-session` recovery sequence, `loginctl list-sessions` continued to show a stale session entry with no live processes attached. Treated as a cosmetic logind bookkeeping artifact; expected to clear on next reboot.
27. **Test 1:** `Splash.qml` in the `CachyOS-Nord` look-and-feel theme emitted `ReferenceError: bottomRect is not defined` twice during boot-time splash rendering. This is a splash-theme bug, unrelated to plasmashell removal or Caelestia.
28. **Test 1:** `plasma-ksplash.service`'s systemd-reported wall-clock duration (31.9s) did not match the visually observed splash duration (~10s) or the actual KWin→Caelestia handoff time (6-7s across two boots). Likely explained by `ksplashqml` continuing background work after the splash visually cleared; not investigated further since it does not affect perceived startup time.

# Project Status

**All 17 planned test items (Test 1 through Test 16.6) are complete, with every item passing.** The central question of the project — whether Caelestia KDE can run as a practical desktop shell with `plasmashell` fully removed from the boot process — has been answered: **yes**, confirmed on two independent reboots with `plasma-plasmashell.service` masked before login.

Open items, none blocking, carried forward as future work rather than pending tests:

- The `Share Region` screen-recording failure (issue 14).
- The `Notification.qml` null-property warnings (issue 6).
- Memory growth after shell restarts (issues 15/23).
- Residual `Meta+W`/Overview shortcut interference when falling back to stock Plasma (issue 22).
- Whether the wallpaper/lock-screen mirror to Plasma's native config actually degrades gracefully long-term under Condition B (issue 4 — confirmed non-blocking, not confirmed silent-forever).

# Important Continuation Rule

**This is the canonical live-testing progress file.**

After every completed test:

1. Append/update the test result here.
2. Preserve both successful results and failures/warnings.
3. Keep Condition A/B explicitly recorded.
4. Keep the exact commands used when they are important for reproducing the result.
5. Update the `Current Overall Status` table.
6. Update `Next Test`.
7. Do not erase earlier results merely because a later rerun changes or refines a conclusion; record reruns separately.
