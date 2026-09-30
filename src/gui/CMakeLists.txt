add_executable(caelestia-switch-gui main.cpp)
target_link_libraries(caelestia-switch-gui PRIVATE switchcore Qt6::Widgets)
install(TARGETS caelestia-switch-gui RUNTIME DESTINATION bin)
