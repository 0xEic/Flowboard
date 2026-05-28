cmake_minimum_required(VERSION 3.20)
project(flowboard
    VERSION 1.0.0
    DESCRIPTION "Configurable pipe-and-filter workflow engine for OnboardAPI"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows resource compiler — needed to embed the executable icon (app.rc).
if(WIN32)
    enable_language(RC)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(FLOWBOARD_BUILD_TESTS "Build unit & integration tests" ON)
option(FLOWBOARD_BUILD_BENCHMARKS "Build perf benchmarks" ON)
option(FLOWBOARD_USE_STUB_SDK
    "Use the in-tree stub OnboardAPI SDK instead of the real SDK. When OFF (the default), the real SDK is located via find_package(onboardapi) (see cmake/RealSdk.cmake)."
    OFF)
option(FLOWBOARD_BUILD_PIPEGEN
    "Build the pipegen codegen tool"
    ON)
option(FLOWBOARD_BUILD_CONTROL_PLANE
    "Build the HTTP+WebSocket control plane server"
    ON)
option(FLOWBOARD_BUILD_WEB
    "Build the web configurator and embed it into the binary"
    ON)
option(FLOWBOARD_BUILD_DOCS
    "Add a 'docs' target that generates Doxygen API + architecture documentation"
    OFF)

# OnboardAPI data model (.rmodel IDL files) — sourced from the upstream
# Rheinmetall/onboardapi submodule pinned at external/onboardapi (tag v9.9.0).
# pipegen consumes these to generate node classes, stub headers, and TS metadata.
set(ONBOARDAPI_DATAMODEL_DIR "${CMAKE_SOURCE_DIR}/external/onboardapi/datamodel"
    CACHE PATH "Directory containing the OnboardAPI .rmodel files")
file(GLOB _onboardapi_rmodels "${ONBOARDAPI_DATAMODEL_DIR}/*.rmodel")
if(NOT _onboardapi_rmodels)
    message(FATAL_ERROR
        "No .rmodel files found in ${ONBOARDAPI_DATAMODEL_DIR}.\n"
        "The OnboardAPI data model is a git submodule. Fetch it with:\n"
        "    git submodule update --init external/onboardapi\n"
        "or override the location with -DONBOARDAPI_DATAMODEL_DIR=<dir>.")
endif()

include(cmake/ThirdParty.cmake)
include(cmake/Pipegen.cmake)
include(cmake/EmbedWebAssets.cmake)

if(FLOWBOARD_USE_STUB_SDK)
    add_subdirectory(sdk_stub)
else()
    include(cmake/RealSdk.cmake)
endif()

# Generate version_info.hpp (after the SDK is located so its version is known).
include(cmake/VersionInfo.cmake)

add_subdirectory(src)

if(FLOWBOARD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(FLOWBOARD_BUILD_PIPEGEN)
    add_subdirectory(pipegen)
endif()

if(FLOWBOARD_BUILD_DOCS)
    include(cmake/Doxygen.cmake)
endif()
