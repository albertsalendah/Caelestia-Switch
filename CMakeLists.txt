cmake_minimum_required(VERSION 3.21)
project(caelestia-switch VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build on the MSI and copy the binaries to the ASUS.
# Do NOT add -march=native: the two laptops have different CPUs.

find_package(Qt6 REQUIRED COMPONENTS Core DBus Widgets)
# KF6 ConfigCore (group-level backups) is added in roadmap Phase A2, when it is first used.

add_subdirectory(src/core)
add_subdirectory(src/cli)
add_subdirectory(src/gui)

install(FILES data/caelestia-switch.desktop DESTINATION share/applications)
