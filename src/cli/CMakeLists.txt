add_executable(caelestia-switch main.cpp)
target_link_libraries(caelestia-switch PRIVATE switchcore Qt6::Core)
install(TARGETS caelestia-switch RUNTIME DESTINATION bin)
