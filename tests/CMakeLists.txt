add_executable(schema_test schema_test.cpp)
target_link_libraries(schema_test PRIVATE ob_dump_core)
add_test(NAME schema_test COMMAND schema_test)

add_executable(fb_decode_test fb_decode_test.cpp)
target_link_libraries(fb_decode_test PRIVATE ob_dump_core)
add_test(NAME fb_decode_test COMMAND fb_decode_test)
