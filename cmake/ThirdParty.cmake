include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# CMake 4.x enforces a minimum version floor; several vendored deps (doctest,
# SPSCQueue) still carry very old cmake_minimum_required declarations.
# Setting this policy allows them to configure without hard errors.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)

FetchContent_Declare(spsc_queue
    GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
    GIT_TAG v1.1
    GIT_SHALLOW TRUE)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(nlohmann_json spsc_queue spdlog doctest)

add_library(rigtorp_spsc INTERFACE)
target_include_directories(rigtorp_spsc INTERFACE
    ${spsc_queue_SOURCE_DIR}/include)

FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(asio)

# Tell Crow's Findasio.cmake where to find asio.hpp so find_package(asio) succeeds
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "" FORCE)

add_library(asio_headeronly INTERFACE)
target_include_directories(asio_headeronly INTERFACE
    ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio_headeronly INTERFACE
    ASIO_STANDALONE=1 ASIO_NO_DEPRECATED=1)

FetchContent_Declare(crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.2.0
    GIT_SHALLOW TRUE)
set(CROW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(CROW_BUILD_TESTS OFF CACHE INTERNAL "")
set(CROW_AMALGAMATE OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(crow)

add_library(crow_with_asio INTERFACE)
target_compile_definitions(crow_with_asio INTERFACE CROW_USE_STANDALONE_ASIO=1)
target_link_libraries(crow_with_asio INTERFACE Crow::Crow asio_headeronly)

# Crow v1.2.0 uses the deprecated asio::io_service alias internally, so it
# must NOT see ASIO_NO_DEPRECATED.  Provide a separate interface target that
# gives Crow standalone-asio mode without stripping the deprecated symbols.
add_library(crow_standalone_asio INTERFACE)
target_compile_definitions(crow_standalone_asio INTERFACE
    CROW_USE_STANDALONE_ASIO=1
    ASIO_STANDALONE=1)
target_include_directories(crow_standalone_asio INTERFACE
    ${asio_SOURCE_DIR}/asio/include)
target_link_libraries(crow_standalone_asio INTERFACE Crow::Crow)
