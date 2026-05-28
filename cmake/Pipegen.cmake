# SPDX-License-Identifier: MIT
# Invoke pipegen on a list of .rmodel files; produce a CMake custom command
# that emits registry_init.cpp + per-module types.hpp/nodes.cpp into OUTPUT_DIR.
#
# Usage:
#   flowboard_add_codegen(my_target
#       OUTPUT_DIR <dir>            # default: ${CMAKE_BINARY_DIR}/generated/<target>
#       RMODELS <file> <file> ...)
#
# The function defines a custom target `<target>_codegen` and exports the
# variable `${target}_CODEGEN_DIR` to the parent scope so the consumer can
# discover the output directory.

function(flowboard_add_codegen target)
    set(opts REAL_SDK)
    set(one_value OUTPUT_DIR)
    set(multi_value RMODELS)
    cmake_parse_arguments(PG "${opts}" "${one_value}" "${multi_value}" ${ARGN})

    if(NOT PG_OUTPUT_DIR)
        set(PG_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/${target})
    endif()
    file(MAKE_DIRECTORY ${PG_OUTPUT_DIR})

    set(_extra_flags "")
    if(PG_REAL_SDK)
        set(_extra_flags "--real-sdk")
    endif()

    add_custom_command(
        OUTPUT ${PG_OUTPUT_DIR}/registry_init.cpp
        COMMAND $<TARGET_FILE:pipegen> ${PG_OUTPUT_DIR} ${_extra_flags} ${PG_RMODELS}
        DEPENDS pipegen ${PG_RMODELS}
        COMMENT "pipegen -> ${PG_OUTPUT_DIR}${_extra_flags}"
        VERBATIM)

    add_custom_target(${target}_codegen ALL
        DEPENDS ${PG_OUTPUT_DIR}/registry_init.cpp)

    set(${target}_CODEGEN_DIR ${PG_OUTPUT_DIR} PARENT_SCOPE)
endfunction()
