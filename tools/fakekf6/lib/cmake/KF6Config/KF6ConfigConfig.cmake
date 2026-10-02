# Sandbox-only stand-in for KF6 Config (no KDE Frameworks dev package available there).
# Use: cmake -S . -B build -DCMAKE_PREFIX_PATH=$PWD/tools/fakekf6
# The real build (MSI/ASUS) uses the system KF6 and must NOT use this.
add_library(KF6::ConfigCore INTERFACE IMPORTED)
set_target_properties(KF6::ConfigCore PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include")
