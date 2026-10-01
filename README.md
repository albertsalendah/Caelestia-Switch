# caelestia-switch

A standalone app to switch between stock KDE Plasma (`plasmashell`) and Caelestia KDE without uninstalling either. It works with an unmodified `ladybug-me/caelestia-kde` install, and later with a custom fork.

**Status:** Phase A1 in progress. `caelestia-switch status [--json]` reads the system (read-only); all other commands are not implemented yet.

## Layout

- `src/core` — core library (detection, backup/restore, switch logic; to be built)
- `src/cli` — `caelestia-switch` command-line tool, a thin layer over the core
- `src/gui` — `caelestia-switch-gui`, a thin Qt Widgets layer over the core
- `data` — `.desktop` entry
- `docs` — design docs: architecture and decision log, roadmap, app spec

## Build

Requires Qt6 (Core, DBus, Widgets, Test), KF6 Config (`kconfig`) and CMake 3.21+.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Build on the ASUS test laptop (clone, `git pull --ff-only`, build with `-j2`); the MSI build is a compile check. Do not use `-march=native`.

Run the unit tests with `ctest --test-dir build --output-on-failure`.

## Licence

Not chosen yet.
