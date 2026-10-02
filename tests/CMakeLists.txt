add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE switchcore Qt6::Test)
add_test(NAME core COMMAND test_core)

add_executable(test_backup test_backup.cpp)
target_link_libraries(test_backup PRIVATE switchcore Qt6::Test)
add_test(NAME backup COMMAND test_backup)

add_executable(test_switch test_switch.cpp)
target_link_libraries(test_switch PRIVATE switchcore Qt6::Test)
add_test(NAME switch COMMAND test_switch)
