# SPDX-License-Identifier: MIT
# Generates API + architecture documentation with Doxygen.
#
# Enable with -DFLOWBOARD_BUILD_DOCS=ON, then:
#   cmake --build <build-dir> --target docs
# Output: <build-dir>/docs/html/index.html
#
# Graphviz (`dot`) is used for class/collaboration diagrams when available;
# Doxygen falls back to built-in diagrams otherwise.

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(NOT DOXYGEN_FOUND)
    message(WARNING
        "FLOWBOARD_BUILD_DOCS=ON but Doxygen was not found. "
        "Install Doxygen (and optionally Graphviz) to build the 'docs' target.")
    return()
endif()

if(TARGET Doxygen::dot)
    set(DOXYGEN_HAVE_DOT "YES")
else()
    set(DOXYGEN_HAVE_DOT "NO")
    message(STATUS "Graphviz 'dot' not found — Doxygen will use built-in diagrams.")
endif()

set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
set(DOXYGEN_MAINPAGE   "${CMAKE_SOURCE_DIR}/docs/doxygen/mainpage.md")

# Space-separated list of inputs Doxygen scans (quoted per path).
set(DOXYGEN_INPUT
    "\"${CMAKE_SOURCE_DIR}/docs/doxygen\" \
\"${CMAKE_SOURCE_DIR}/include\" \
\"${CMAKE_SOURCE_DIR}/src\" \
\"${CMAKE_SOURCE_DIR}/pipegen\" \
\"${CMAKE_SOURCE_DIR}/docs/ARCHITECTURE.md\" \
\"${CMAKE_SOURCE_DIR}/docs/NODES.md\" \
\"${CMAKE_SOURCE_DIR}/docs/ADDING_A_NODE.md\" \
\"${CMAKE_SOURCE_DIR}/docs/ADR-001-backpressure.md\" \
\"${CMAKE_SOURCE_DIR}/docs/INTEGRATING_THE_REAL_SDK.md\"")

set(_doxyfile_in  "${CMAKE_SOURCE_DIR}/docs/Doxyfile.in")
set(_doxyfile_out "${CMAKE_BINARY_DIR}/Doxyfile")
configure_file("${_doxyfile_in}" "${_doxyfile_out}" @ONLY)

file(MAKE_DIRECTORY "${DOXYGEN_OUTPUT_DIR}")

add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} "${_doxyfile_out}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating API + architecture documentation with Doxygen"
    VERBATIM)

message(STATUS "Doxygen docs target enabled (output: ${DOXYGEN_OUTPUT_DIR}/html).")
