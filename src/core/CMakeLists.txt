add_library(switchcore STATIC
    version.cpp version.h
    readings.cpp readings.h
    systemd.cpp systemd.h
    plasmaconfig.cpp plasmaconfig.h
    install.cpp install.h
    consistency.cpp consistency.h
    backup.cpp backup.h
    state.cpp state.h
    switchops.cpp switchops.h
    switch.cpp switch.h
    fsutil.h)
target_include_directories(switchcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(switchcore PRIVATE CS_VERSION="${PROJECT_VERSION}")
target_link_libraries(switchcore PUBLIC Qt6::Core Qt6::DBus KF6::ConfigCore)
