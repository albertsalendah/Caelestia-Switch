# caelestia-switch

A standalone app to switch between stock KDE Plasma (`plasmashell`) and Caelestia KDE without uninstalling either. It works with an unmodified `ladybug-me/caelestia-kde` install, and later with a custom fork.

**Status:** project skeleton only. The CLI and GUI build and start, but no commands are implemented yet.

## Layout

- `src/core` — core library (detection, backup/restore, switch logic; to be built)
- `src/cli` — `caelestia-switch` command-line tool, a thin layer over the core
- `src/gui` — `caelestia-switch-gui`, a thin Qt Widgets layer over the core
- `data` — `.desktop` entry
- `docs` — design docs: architecture and decision log, roadmap, app spec

## Build

Requires Qt6 (Core, DBus, Widgets) and CMake 3.21+. KF6 is added later (roadmap Phase A2).

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Build on the MSI and copy the binaries to the ASUS. Do not use `-march=native`.

## Licence

Not chosen yet.
