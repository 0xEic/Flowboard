# SPDX-License-Identifier: MIT
add_library(flowboard_control STATIC
    server.cpp
    serialization.cpp
    ws_hub.cpp
)
target_compile_features(flowboard_control PUBLIC cxx_std_20)
target_include_directories(flowboard_control PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(flowboard_control PUBLIC
    flowboard_engine
    crow_standalone_asio
)

if(FLOWBOARD_BUILD_WEB)
    # Locate npm.  On Windows the shim is npm.cmd; on Unix it is npm.
    find_program(NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)

    # Build the React bundle. Depends on pipegen_ts so the generated TS catalog
    # exists before TypeScript compilation (otherwise tsc errors on fresh clones).
    add_custom_target(web_build ALL
        COMMAND "${NPM_EXECUTABLE}" install --no-audit --no-fund
        COMMAND "${NPM_EXECUTABLE}" run build
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/web"
        COMMENT "Building web bundle (npm install + npm run build)"
        VERBATIM)
    if(TARGET pipegen_ts)
        add_dependencies(web_build pipegen_ts)
    endif()

    # Generate web_assets.cpp from the built bundle.
    flowboard_embed_web_assets(flowboard_control
        "${CMAKE_SOURCE_DIR}/web/dist"
        "${CMAKE_BINARY_DIR}/generated/web_assets.cpp")

    # Make sure the bundle is ready before the embedding command runs.
    add_dependencies(flowboard_control web_build)

    # Add the static route handler.
    target_sources(flowboard_control PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/routes_static.cpp")

    # Let server.cpp know that the web bundle is available.
    target_compile_definitions(flowboard_control PRIVATE FLOWBOARD_HAS_WEB=1)
endif()

