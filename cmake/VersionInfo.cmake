# SPDX-License-Identifier: MIT
# Capture version + git + build metadata into a generated header
# (${CMAKE_BINARY_DIR}/generated/version_info.hpp) consumed by the
# OnboardApi.DeviceReport node. Git info is read at configure time and degrades
# gracefully to "unknown" when git or the .git dir is unavailable.

set(FLOWBOARD_GIT_COMMIT       "unknown")
set(FLOWBOARD_GIT_COMMIT_SHORT "unknown")
set(FLOWBOARD_GIT_BRANCH       "unknown")
set(FLOWBOARD_GIT_DESCRIBE     "unknown")
set(FLOWBOARD_GIT_DIRTY        0)

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE FLOWBOARD_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE FLOWBOARD_GIT_COMMIT_SHORT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE FLOWBOARD_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE FLOWBOARD_GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _flowboard_git_status OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT "${_flowboard_git_status}" STREQUAL "")
        set(FLOWBOARD_GIT_DIRTY 1)
    endif()
    # Guard against empty results (e.g. shallow clone with no tags).
    foreach(_v FLOWBOARD_GIT_COMMIT FLOWBOARD_GIT_COMMIT_SHORT FLOWBOARD_GIT_BRANCH FLOWBOARD_GIT_DESCRIBE)
        if("${${_v}}" STREQUAL "")
            set(${_v} "unknown")
        endif()
    endforeach()
endif()

string(TIMESTAMP FLOWBOARD_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)

if(CMAKE_CONFIGURATION_TYPES)
    set(FLOWBOARD_BUILD_TYPE "multi-config")
else()
    set(FLOWBOARD_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()
set(FLOWBOARD_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

if(FLOWBOARD_USE_STUB_SDK)
    set(FLOWBOARD_SDK_MODE "stub")
else()
    set(FLOWBOARD_SDK_MODE "real")
endif()

# Dependency versions. Third-party tags mirror cmake/ThirdParty.cmake; the
# OnboardAPI SDK + CycloneDDS versions come from the real-SDK find when present.
set(_flowboard_deps "")
macro(_flowboard_add_dep _name _ver)
    string(APPEND _flowboard_deps "    { \"${_name}\", \"${_ver}\" },\n")
endmacro()

if(FLOWBOARD_USE_STUB_SDK)
    _flowboard_add_dep("OnboardAPI SDK" "stub")
else()
    if(DEFINED onboardapi_VERSION AND NOT "${onboardapi_VERSION}" STREQUAL "")
        _flowboard_add_dep("OnboardAPI SDK" "${onboardapi_VERSION}")
    else()
        _flowboard_add_dep("OnboardAPI SDK" "real")
    endif()
    if(DEFINED CycloneDDS_VERSION AND NOT "${CycloneDDS_VERSION}" STREQUAL "")
        _flowboard_add_dep("CycloneDDS" "${CycloneDDS_VERSION}")
    endif()
endif()
_flowboard_add_dep("nlohmann_json" "3.11.3")
_flowboard_add_dep("spdlog" "1.14.1")
_flowboard_add_dep("asio" "1.30.2")
_flowboard_add_dep("Crow" "1.2.0")
_flowboard_add_dep("SPSCQueue" "1.1")
# Strip the trailing newline so the generated initializer list is tidy.
string(REGEX REPLACE "\n$" "" FLOWBOARD_DEP_ENTRIES "${_flowboard_deps}")

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/version_info.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/version_info.hpp"
    @ONLY)
