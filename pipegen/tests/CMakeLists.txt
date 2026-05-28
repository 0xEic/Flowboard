# SPDX-License-Identifier: MIT
add_executable(pipegen_tests
    test_lexer.cpp
    test_parser.cpp
    test_emit_cpp.cpp
    test_emit_stub.cpp
    test_emit_ts.cpp
)
target_include_directories(pipegen_tests PRIVATE ${CMAKE_SOURCE_DIR}/pipegen)
target_link_libraries(pipegen_tests PRIVATE doctest::doctest pipegen_lib)
add_test(NAME pipegen_tests
         COMMAND pipegen_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
