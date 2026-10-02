Sandbox-only stub of `KConfig` / `KConfigGroup` (flat INI) so the code can be compiled and the unit
tests run where no KF6 dev package exists (Claude's sandbox). Not used for real builds.

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$PWD/tools/fakekf6 && cmake --build build && ctest --test-dir build
