# SPDX-License-Identifier: MIT
add_library(pipegen_lib STATIC
    lexer.cpp
    parser.cpp
    emit_cpp.cpp
    emit_stub.cpp
    emit_ts.cpp
)
target_compile_features(pipegen_lib PUBLIC cxx_std_20)
target_include_directories(pipegen_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(pipegen main.cpp)
target_link_libraries(pipegen PRIVATE pipegen_lib)
target_compile_features(pipegen PUBLIC cxx_std_20)

if(FLOWBOARD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
