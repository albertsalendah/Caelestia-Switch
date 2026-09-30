# Handover: KDE Plasma + Caelestia KDE Architecture

> **Audit status:** A static source-level audit of `ladybug-me/caelestia-kde` (commit `be4188f`, 2026-09-23) was completed and is documented in **Section 13a**. It corrects one core assumption in this document — see the note in Section 2. Live runtime verification (Phase 2, Section 9) has not been performed; the audit is source-only.

## 1. Project Goal

Create a KDE-based desktop environment/session that keeps the KDE Plasma infrastructure that is **not tied to `plasmashell`**, while replacing `plasmashell` with **Caelestia KDE**, the Quickshell-based Caelestia shell port for KDE/KWin.

Guiding principle:

> **Keep KDE infrastructure unless it is specifically part of `plasmashell`; replace the shell/UI with Caelestia; if Caelestia cannot cover a shell function, use an existing suitable component or build a small custom replacement.**

This is **not** intended to fork or replace KDE Plasma as a whole.

---

## 2. Target Architecture

```text
┌─────────────────────────────────────┐
│       Plasma Login Manager (PLM)    │
│              Login/session          │
├─────────────────────────────────────┤
│                 KWin                │
│       Wayland compositor / WM       │
│       windows / workspaces / etc.   │
├─────────────────────────────────────┤
│       KDE / Plasma infrastructure   │
│                                     │
│ PowerDevil                          │
│ KScreen / display infrastructure    │
│ NetworkManager KDE integration      │
│ BlueDevil                           │
│ KRunner                             │
│ KGlobalAccel                        │
│ KDE Wallet                          │
│ Polkit authentication components    │
│ KDE Frameworks                       │
│ Other independent KDE services      │
├─────────────────────────────────────┤
│          Caelestia Shell             │
│                                     │
│ Bar / panel                         │
│ Launcher                            │
│ Task management                     │
│ System tray                         │
│ Notifications                       │
│ OSD                                 │
│ Overview                            │
│ Clipboard UI                        │
│ Widgets                             │
│ Desktop shell                       │
│ Other shell-level UI                │
├─────────────────────────────────────┤
│          KDE Applications            │
└─────────────────────────────────────┘
```

`plasmashell` is deliberately absent from the target runtime architecture — as a **goal**. This is not yet what the current `caelestia-kde` source does.

> **Correction (Section 13a):** in the audited source, `plasmashell` is not killed, stopped, or disabled anywhere in `install.sh`/`uninstall.sh`/`update.sh`. It is left running with its panels stripped out via a live D-Bus call, and stays a genuine runtime dependency (not just an install-time one) because `shell/services/Wallpapers.qml` calls back into `org.kde.plasmashell` every time the user changes wallpaper through Caelestia's own UI. The current implementation is closer to "plasmashell present but headless" than "plasmashell absent." Getting to the diagram above requires either removing that wallpaper call path or confirming `plasmashell` can be fully stopped without losing wallpaper application — this is now the concrete blocker for Phase 2.

**PLM is infrastructure and is not part of the `plasmashell` replacement.**

---

## 3. What Is Being Replaced?

Primary replacement:

```text
plasmashell
    ↓
Caelestia KDE / Quickshell
```

Shell-level functionality to replace may include:

- Plasma panel
- Task manager / taskbar UI
- Application launcher UI
- System tray UI
- Plasma widgets
- Desktop containment UI
- Notification UI
- OSD UI
- Overview UI
- Clipboard UI
- Shell-level workspace presentation
- Shell-level Activities presentation
- Other `plasmashell`-owned UI

The goal is **not** to replace KDE services simply because they are used by Plasma.

---

## 4. What Should Be Kept?

Unless source inspection proves otherwise, keep:

### Core

- KWin
- KDE Frameworks
- Wayland infrastructure
- KDE applications
- KDE configuration infrastructure

### KDE/Plasma services

Examples:

- PowerDevil
- KScreen/display management
- NetworkManager KDE integration
- BlueDevil
- KRunner
- KGlobalAccel
- KDE Wallet
- Polkit authentication components
- Other independent KDE background services

### Login

- **Plasma Login Manager (PLM)**

PLM is the login/display-manager layer and should remain outside the Caelestia replacement project.

---

## 5. PLM

Earlier planning referred to SDDM, but the current architecture should use:

> **Plasma Login Manager (PLM)**

PLM is the login/display-manager layer. It should remain infrastructure rather than being treated as part of `plasmashell`.

Conceptually:

```text
PLM
 │
 └── starts the user session
          │
          ├── KWin
          ├── KDE services
          └── Caelestia shell
```

Unless a future goal explicitly calls for a custom login manager, the project should **not** replace PLM.

PLM and the Plasma lock screen are separate concerns:

- PLM → login/session startup
- Plasma lock screen → locking an already-running user session

Caelestia may retain KDE's lock-screen infrastructure if it does not need to replace it.

---

## 6. Important Architectural Distinction

### Plasma ≠ plasmashell

"KDE Plasma" is the larger desktop environment/ecosystem.

`plasmashell` is one major process responsible for the desktop shell/UI.

Therefore:

```text
KDE Plasma
├── KWin
├── KDE Frameworks
├── KDE services
├── Plasma services
├── applications
├── PLM
└── plasmashell
       ↑
       └── THIS is the main replacement target
```

Do not assume:

> "If it is Plasma-related, it must be removed."

Instead ask:

> "Does this functionality depend specifically on `plasmashell`, or can it operate independently?"

---

## 7. Caelestia KDE

The replacement shell is:

`ladybug-me/caelestia-kde`

It is a KDE Plasma 6 port of the Caelestia shell.

The upstream Caelestia shell is based on Quickshell and was originally designed around Hyprland. The KDE port adapts it for KWin/Plasma.

The repository contains areas such as:

```text
shell/
src/
scripts/
installer/
docs/
tests/
tools/
assets/
```

There is also a Caelestia source dependency/submodule related to:

```text
caelestia-dots/caelestia
```

Treat the port as a starting point rather than assuming a completely new Quickshell shell must be written.

---

## 8. Current Caelestia Functionality of Interest

The current Caelestia KDE project includes functionality around:

- Application launcher
- Bar/panel
- Overview
- Notifications
- Clipboard history
- Screenshot UI
- Screen recording
- Shell widgets
- OSD-related functionality
- KWin/workspace integration
- Lock-screen integration
- Autostart/session integration

Example documented keybindings include:

```text
Super
    Application launcher

Super + Tab
    Overview

Super + B
    Notification sidebar

Super + V
    Clipboard history

Super + Shift + S
    Screenshot

Super + Ctrl + S
    Screen recorder
```

These should be verified against the current source before treating them as architectural requirements.

---

## 9. Target Replacement Strategy

### Phase 1 — Inventory

Audit the KDE/Plasma stack and Caelestia source.

Identify:

1. What belongs to `plasmashell`.
2. What belongs to KWin.
3. What belongs to KDE Frameworks.
4. What belongs to independent Plasma/KDE services.
5. What Caelestia already implements.
6. What Caelestia expects from Plasma.
7. What breaks when `plasmashell` is absent.

### Phase 2 — Run Without `plasmashell`

Create a test session where:

```text
PLM
 ↓
KWin
 ↓
KDE services
 ↓
Caelestia
```

starts normally while `plasmashell` does not start.

Do not initially remove unrelated KDE packages. The first goal is to discover actual missing functionality.

### Phase 3 — Fill Shell Gaps

For every missing function:

1. Check whether Caelestia already provides it.
2. Check whether an existing KDE component can provide it independently.
3. Check whether another lightweight component exists.
4. Only then write a custom implementation.

Example initial strategy:

| Function | Initial strategy |
|---|---|
| Panel | Caelestia |
| Launcher | Caelestia |
| Notifications | Caelestia |
| OSD | Caelestia |
| Tray | Caelestia |
| Clipboard UI | Caelestia |
| Task UI | Caelestia/custom |
| Desktop UI | Caelestia/custom |
| Overview | Caelestia + KWin integration |
| Workspace tracking | KWin/Caelestia integration |
| Lock screen | Prefer existing KDE infrastructure |
| Login | PLM |

---

## 10. KWin's Role

KWin should remain intact.

It provides the core window-management/compositor layer:

- Wayland compositor
- Window management
- Focus
- Moving/resizing windows
- Workspaces
- Effects
- Tiling/window behavior where applicable
- Screen/output management integration
- Window state

Caelestia should integrate with KWin rather than becoming a second window manager.

Conceptually:

```text
             KWin
          /        \
         /          \
    Windows       Caelestia
                    │
              shell presentation
```

When deeper integration is required, prefer a focused KWin effect/plugin/integration layer instead of duplicating window-management logic in Quickshell.

---

## 11. Avoid Reimplementing KDE

Major rule:

> **Do not rebuild infrastructure that KDE already provides just because `plasmashell` used it.**

If PowerDevil continues to work without `plasmashell`, keep PowerDevil.

Likewise, do not replace:

- KWin
- KDE Frameworks
- KDE Wallet
- NetworkManager integration
- Bluetooth infrastructure
- display infrastructure
- authentication infrastructure
- KDE applications

unless there is a concrete technical reason.

This keeps the project smaller and reduces maintenance burden.

---

## 12. Plasma APIs Need Individual Auditing

A dependency on something named "Plasma" does **not automatically mean `plasmashell` is required.

Classify each dependency as:

### A. Hard `plasmashell` dependency

Examples include:

```text
org.kde.plasmashell
PlasmaShell-specific D-Bus interfaces
```

These need replacement or removal.

### B. Independent KDE/Plasma service

Can remain if it works without `plasmashell`.

### C. KWin dependency

Keep and integrate with it.

### D. KDE Framework dependency

Keep.

### E. Optional shell integration

May be removed if Caelestia has another implementation.

---

## 13. Proposed Source Audit — Completed

This audit has been performed against `ladybug-me/caelestia-kde` (commit `be4188f`, 2026-09-23) using the methodology below. Results are in **Section 13a**.

The task was a real source-level audit of `caelestia-kde`.

Search the repository for:

```text
plasmashell
org.kde.plasmashell
PlasmaShell
org.kde.plasma
PlasmaCore
PlasmaComponents
KWin
org.kde.KWin
workspace
overview
task
notification
screenlocker
lock
kscreenlocker
activities
containment
applet
DBus
D-Bus
QDBus
```

Also inspect:

- QML imports
- C++ includes
- D-Bus calls
- process launches
- KWin effects/plugins
- autostart files
- lock-screen integration
- session files
- installer/package dependencies

The audit must determine whether each dependency is:

```text
plasmashell-specific
        OR
independent KDE/Plasma infrastructure
        OR
KWin
        OR
KDE Frameworks
        OR
external application/service
```

---

## 13a. Audit Findings (source-verified, 2026-09-25)

Method: cloned `ladybug-me/caelestia-kde` directly (`git clone --depth 1`) and grepped the working tree for the terms listed in Section 13, then read the surrounding code for each hit. File paths below are relative to the repo root. This is a static read of the source, not a live running-session trace.

**A. Hard `plasmashell` dependency**

- `scripts/04-deploy-kde.sh:134,149` and `scripts/09-system-tweaks.sh:46-49` — install-time: `qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript` sets the desktop wallpaper plugin/config and removes all Plasma panels. `09-system-tweaks.sh` falls back to editing `plasma-org.kde.plasma.desktop-appletsrc` directly if the D-Bus call fails (i.e. if plasmashell isn't running at install time).
- `shell/services/Wallpapers.qml:147` — **runtime**, not just install-time: `Quickshell.execDetached([...qdbus6 org.kde.plasmashell...])` fires every time the user sets a wallpaper from inside Caelestia's own UI. This is the one that matters most — it means the shipped design still expects `plasmashell` to be reachable over D-Bus during normal use.
- `shell/services/Kwin.qml:280` and `shell/services/startuptasks/02-krohnkite-setup.sh` — both still special-case a `plasmashell`/`org.kde.plasmashell` window class (filtering it from window lists / tiling), consistent with plasmashell continuing to have some window presence.
- None of `install.sh`, `uninstall.sh`, `update.sh`, or anything under `scripts/` stops, disables, or kills the `plasmashell` process. Only `caelestia-shell`/`quickshell` processes are killed on update/uninstall (`update.sh:164`, `uninstall.sh:202-203`).

**B. Independent KDE/Plasma service (works without `plasmashell`)**

- Lock screen: handled through KDE's real shell-package mechanism (`plasmashellrc` → `[Shell] ShellPackage=caelestia.desktop`, pointing at `~/.local/share/plasma/shells/caelestia.desktop`), not a hardcoded plasmashell dependency — `scripts/08-build-shell.sh` (`install_lockscreen_greeter`, `configure_lockscreen_greeter`) and the self-heal check in `scripts/10-autostart.sh`. The greeter UI itself is fully custom (`shell/services/GreeterService.qml`), not stock Breeze, but it's delivered through KDE's own mechanism rather than replacing it.

**C. KWin dependency (keep, already integrated)**

- Real native bridges in `shell/plugin/src/Caelestia/Services/`: `kwinactivewindowbridge.cpp`, `kwinworkspacestate.cpp`, `plasmawindows.cpp`, `windowscreencast.cpp`.
- `shell/kwin-effects/workspace-tracker/workspace_tracker.cpp` — a genuine native KWin effect (not D-Bus polling) for workspace tracking, matching the doc's "Workspace tracking | KWin/Caelestia integration" row.
- `shell/plugin/src/Caelestia/Services/screenedges.cpp` — talks to `org.kde.KWin` directly and **takes over KWin's electric-border/hot-corner config**, saving KWin's original state to `~/.config/caelestia/stolen-screen-edges.json` so it can be restored later. This wasn't in the Section 14 table at all and should be added (done below).

**D. KDE Framework dependency (keep)**

- `screenedges.cpp` pulls in `KConfigGroup`/`KSharedConfig` directly alongside its `QDBusConnection` usage — a legitimate Frameworks dependency, not a plasmashell one.

**E. Already covered by Caelestia (no plasmashell/Plasma dependency found)**

- Notifications: `shell/services/NotifData.qml`, `Notifs.qml`, `shell/modules/notifications/`, `shell/modules/sidebar/Notif*.qml`.
- Tray: `shell/modules/bar/components/Tray.qml`, `TrayItem.qml`, `shell/modules/nexus/pages/panels/taskbar/BarTray.qml` — this contradicts the Section 14 table's old "not confirmed" entry (see updated table).
- OSD: `shell/modules/osd/`, `shell/plugin/src/Caelestia/Config/osdconfig.hpp`.

---

## 14. Current Source-Based Comparison with `end-4dots-kde`

This table reflects the current file audit and uses stricter labels than the earlier high-level summary.

| Part | Caelestia KDE | end-4dots-kde | Strict label |
|---|---|---|---|
| Launcher / app search | Full launcher stack in `shell/modules/launcher/*` and launcher services. | Full launcher/search stack in `LauncherApps.qml`, `LauncherSearch.qml`, `StartPageApps.qml`, and related files. | **Already covered in both** |
| Overview / workspace switcher | Dedicated overview subsystem with animation, wrapper, content, window grid, and visibility handling. | Overview/search layer in `Overview.qml` and `SearchBar.qml`. | **Already covered in both** |
| Notifications | Notification modules, services, and settings pages are present. | Notification service, notification center, popups, and notification widgets are present. | **Already covered in both** |
| Clipboard UI / history | `Super + V` clipboard history is documented in the shell. | Clipboard history service and shortcut/search wiring are present. | **Already covered in both** |
| Screenshot UI | Screenshot, recorder, region selector, and area picker are present. | Screenshot / recording overlay and region selection are present. | **Already covered in both** |
| Installer / deployment | Installer, docs, and scripts are present. | Installer scripts and distro package layout are present. | **Already covered in both** |
| Wallpaper manager / wallpaper application | Confirmed hard dependency: `shell/services/Wallpapers.qml:147` calls `org.kde.plasmashell` live, at runtime, on every wallpaper change (not just at install). See 13a. | Wallpaper selector/service exists, but KDE wallpaper application still calls `org.kde.plasmashell`. | **Confirmed plasmashell dependency in Caelestia; not yet resolved by either project** |
| Material You / theme pipeline | Appearance settings exist, but wallpaper-driven color handling remains a separate concern. | Material Design 3 theming plus `kde-material-you-colors` and Kvantum support are present. | **Partial in Caelestia; reusable from end-4dots** |
| Color extraction / dynamic palette | Wallpaper/color plumbing exists, but the checked files still route through KDE shell integration. | `matugen`, `hyprpicker`, and wallpaper-to-color tooling are present. | **Partial in Caelestia; reusable from end-4dots** |
| KWin bridge / KDE bridge | Confirmed substantial, working bridge: native C++ services (`kwinactivewindowbridge.cpp`, `kwinworkspacestate.cpp`, `plasmawindows.cpp`, `windowscreencast.cpp`) plus a real KWin effect (`workspace_tracker.cpp`). Not a stub. Panel removal and wallpaper still route through `plasmashell` rather than KWin, per the row above. | README describes a custom KDE bridge via KWin script, but checked files still use `plasmashell` for wallpaper application. | **Caelestia's KWin bridge is further along than the earlier summary suggested; the plasmashell dependency is isolated to wallpaper + panel-removal, not the bridge itself** |
| Screen edges / hot corners | Not in original table. Confirmed: `screenedges.cpp` talks to `org.kde.KWin` directly and takes over KWin's electric-border config (saved to `~/.config/caelestia/stolen-screen-edges.json` for restoration). | Not checked. | **Already covered in Caelestia via direct KWin integration — no plasmashell involvement found** |
| Tray / system tray | Corrected: confirmed present. `shell/modules/bar/components/Tray.qml`, `TrayItem.qml`, `shell/modules/nexus/pages/panels/taskbar/BarTray.qml`. No plasmashell dependency found. | Explicit tray service and tray UI files are present. | **Already covered in both — earlier "not confirmed" entry was wrong** |

The main takeaway is that `end-4dots-kde` is a reusable source for several shell subsystems, but it does **not** finish the KWin/KDE bridge problem or remove the remaining `plasmashell` dependence by itself. The remaining `plasmashell` dependence in Caelestia is narrower than earlier drafts implied — it's isolated to wallpaper application and panel removal, not scattered through the KWin bridge, tray, or notifications.

This table reflects direct source inspection completed 2026-09-25 against `caelestia-kde` commit `be4188f` (see Section 13a for file/line evidence). `end-4dots-kde` rows are carried over from the earlier high-level summary and have not yet had the same file-level pass — treat those cells accordingly.

---

## 15. Session Architecture

The final system should start Caelestia directly as part of the desktop session rather than:

```text
start Plasma
    ↓
start plasmashell
    ↓
kill plasmashell
    ↓
start Caelestia
```

Preferred architecture:

```text
PLM
  ↓
Wayland session
  ↓
KWin
  ↓
KDE services
  ↓
Caelestia
```

with no unnecessary `plasmashell` startup.

This makes the result a genuine alternate shell/session composition rather than a modified stock Plasma startup.

---

## 16. Design Philosophy

### Minimal replacement

Replace only what `plasmashell` provides.

### Reuse KDE

If KDE already provides the infrastructure, keep it.

### KWin remains authoritative

Do not turn Caelestia into a window manager.

### Small integration layer

When Caelestia needs information/control from KWin, create focused integration rather than duplicating KWin functionality.

### Avoid Plasma containment compatibility unless necessary

There is no requirement to recreate Plasma's historical:

```text
Corona
  ↓
Containments
  ↓
Applets
```

architecture inside Caelestia.

Quickshell can use a simpler shell model.

### Prefer existing components

Only create custom replacements when:

- Caelestia cannot provide the functionality,
- KDE cannot provide it independently,
- and an appropriate existing third-party component is unavailable or unsuitable.

---

## 17. Desired End State

```text
                 PLM
                  │
                  ▼
                KWin
                  │
        ┌─────────┴─────────┐
        │                   │
 KDE infrastructure     Caelestia
        │                   │
        │             shell / UI
        │                   │
        └─────────┬─────────┘
                  │
                  ▼
            KDE Applications
```

Expected runtime state (target):

```text
plasmashell       = NOT RUNNING
KWin              = RUNNING
KDE services      = RUNNING
KDE Frameworks    = USED
KDE applications  = RUNNING
PLM               = RUNNING
Caelestia         = RUNNING
```

Actual runtime state per the current `caelestia-kde` source (Section 13a) — nothing in the codebase stops `plasmashell`, and `Wallpapers.qml` calls back into it at runtime:

```text
plasmashell       = RUNNING (panels stripped via D-Bus, still targeted by live wallpaper calls)
KWin              = RUNNING
KDE services      = RUNNING
KDE Frameworks    = USED
KDE applications  = RUNNING
PLM               = RUNNING
Caelestia         = RUNNING
```

---

## 18. Immediate Next Step — Done; New Next Step Below

The file-by-file dependency audit called for here has been performed — see **Section 13a** for the full findings and **Section 14** for the updated comparison table. Summary against the original checklist:

1. Files that can remain unchanged — notifications, tray, OSD, launcher, overview subsystems: no `plasmashell` dependency found.
2. Files that assume `plasmashell` exists — narrowed to two: `shell/services/Wallpapers.qml` (runtime) and the install scripts' panel-removal/wallpaper-seed calls (install-time only).
3. Files that communicate with KWin — `shell/plugin/src/Caelestia/Services/{kwinactivewindowbridge,kwinworkspacestate,plasmawindows,windowscreencast,screenedges}.cpp`, `shell/kwin-effects/workspace-tracker/workspace_tracker.cpp`.
4. Files that communicate with independent KDE services — the lock-screen greeter, via the standard Plasma shell-package mechanism rather than plasmashell directly.
5. Functionality already fully covered by Caelestia — notifications, tray, OSD, screen edges/hot corners, KWin workspace/window bridging.
6. Missing shell functionality — not assessed by a static read; needs the Phase 2 live-session test.
7. Candidates for existing replacements — n/a so far; the remaining gap (wallpaper application without plasmashell) doesn't have an obvious existing KDE component to swap in, since it needs to write the same desktop-containment wallpaper config plasmashell owns.
8. Functionality requiring custom implementation — a non-plasmashell path for applying wallpaper (e.g. writing the config Plasma reads directly, if that's viable without a live plasmashell instance to notify).
9. Anything that should remain KDE infrastructure — PLM, the lock-screen mechanism, KWin itself: all confirmed independent of plasmashell in the source.

**New immediate next step:** run Phase 2 (Section 9) — a live session with `plasmashell` actually stopped — specifically to see whether wallpaper application breaks without it, since that's now the one concrete open question standing between the current source and the Section 2 target architecture.

---

The current comparison shows that `end-4dots-kde` can seed or replace several shell features, but bridge work remains partial and still needs custom implementation where `plasmashell` dependencies remain.

## 19. Central Project Rule

> **Keep everything from KDE Plasma that is not specifically tied to `plasmashell`. Replace `plasmashell` with Caelestia KDE. For anything Caelestia cannot cover, first look for an existing KDE or third-party replacement; only build a custom component when necessary. Treat Plasma Login Manager (PLM) as infrastructure and leave it outside the shell replacement.**
