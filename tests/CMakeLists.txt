add_executable(schema_test schema_test.cpp)
target_link_libraries(schema_test PRIVATE ob_dump_core)
add_test(NAME schema_test COMMAND schema_test)
