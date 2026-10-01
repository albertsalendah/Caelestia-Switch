cmake_minimum_required(VERSION 3.21)
project(caelestia-switch VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build on the ASUS test laptop (clone, pull, build); the MSI build is a compile check.
# Do NOT add -march=native: the two laptops have different CPUs.

find_package(Qt6 REQUIRED COMPONENTS Core DBus Widgets Test)
# KF6 ConfigCore: reads plasmashellrc now (Phase A1); group-level backups in Phase A2.
find_package(KF6Config REQUIRED)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/cli)
add_subdirectory(src/gui)
add_subdirectory(tests)

install(FILES data/caelestia-switch.desktop DESTINATION share/applications)
