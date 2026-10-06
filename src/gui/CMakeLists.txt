add_library(switchgui STATIC mainwindow.cpp mainwindow.h)
target_include_directories(switchgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(switchgui PUBLIC switchcore Qt6::Widgets)

add_executable(caelestia-switch-gui main.cpp)
target_link_libraries(caelestia-switch-gui PRIVATE switchgui)
install(TARGETS caelestia-switch-gui RUNTIME DESTINATION bin)
