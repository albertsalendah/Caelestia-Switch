add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE switchcore Qt6::Test)
add_test(NAME core COMMAND test_core)

add_executable(test_backup test_backup.cpp)
target_link_libraries(test_backup PRIVATE switchcore Qt6::Test)
add_test(NAME backup COMMAND test_backup)

add_executable(test_switch test_switch.cpp)
target_link_libraries(test_switch PRIVATE switchcore Qt6::Test)
add_test(NAME switch COMMAND test_switch)

add_executable(test_gui test_gui.cpp)
target_link_libraries(test_gui PRIVATE switchgui Qt6::Test)
add_test(NAME gui COMMAND test_gui)
# The window is tested without a display (also over SSH on the test laptop).
set_tests_properties(gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
