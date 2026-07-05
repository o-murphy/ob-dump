add_executable(schema_test schema_test.cpp)
target_link_libraries(schema_test PRIVATE ob_dump_core)
add_test(NAME schema_test COMMAND schema_test)

add_executable(fb_decode_test fb_decode_test.cpp)
target_link_libraries(fb_decode_test PRIVATE ob_dump_core)
add_test(NAME fb_decode_test COMMAND fb_decode_test)

add_executable(schema_json_test schema_json_test.cpp)
target_link_libraries(schema_json_test PRIVATE ob_dump_core)
add_test(NAME schema_json_test COMMAND schema_json_test)

add_executable(fbs_gen_test fbs_gen_test.cpp)
target_link_libraries(fbs_gen_test PRIVATE ob_dump_core)
add_test(NAME fbs_gen_test COMMAND fbs_gen_test)
