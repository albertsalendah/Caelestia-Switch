add_library(switchcore STATIC version.cpp version.h)
target_include_directories(switchcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(switchcore PRIVATE CS_VERSION="${PROJECT_VERSION}")
target_link_libraries(switchcore PUBLIC Qt6::Core Qt6::DBus)
