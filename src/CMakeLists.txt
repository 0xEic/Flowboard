add_library(flowboard_engine STATIC
    log_setup.cpp
    builtin_types.cpp
    app_info.cpp
    engine/port.cpp
    engine/edge.cpp
    engine/node.cpp
    engine/registry.cpp
    engine/graph.cpp
    engine/error_budget.cpp
    engine/graph_holder.cpp
    engine/live_taps.cpp
    engine/tap_registry.cpp
    engine/extract_registry.cpp
    engine/log_registry.cpp
    engine/throttle_registry.cpp
    engine/synchronize_registry.cpp
    engine/key_value_accumulator_registry.cpp
    engine/wire_registry.cpp
    engine/list_build_registry.cpp
    engine/onboardapi_discovery.cpp
    engine/manipulate_ops.cpp
    nodes/threshold.cpp
    nodes/compare.cpp
    nodes/convert.cpp
    nodes/derivative.cpp
    nodes/filter.cpp
    nodes/inverter.cpp
    nodes/throttle.cpp
    nodes/constant_source.cpp
    nodes/log_node.cpp
    nodes/extract.cpp
    nodes/timer_node.cpp
    nodes/iterator_node.cpp
    nodes/sync_node.cpp
    nodes/current_time_node.cpp
    nodes/time_source.cpp
    nodes/key_value_accumulator.cpp
    nodes/list_sort.cpp
    nodes/list_filter.cpp
    nodes/list_size.cpp
    nodes/list_get_at.cpp
    nodes/list_find.cpp
    nodes/flow_machine.cpp
    nodes/onboardapi_discovery_node.cpp
    nodes/debug_nodes.cpp
    nodes/manipulate_nodes.cpp
    nodes/list_combine.cpp
    nodes/list_map_field.cpp
    nodes/button_handler.cpp
    nodes/random_source.cpp
    nodes/arithmetic.cpp
    nodes/number_holder.cpp
    nodes/list_create.cpp
    config/loader.cpp
)
target_include_directories(flowboard_engine
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
# The KeyValueAccumulator registers the full (key x value) primitive matrix —
# many template instantiations in one TU push past MSVC's object section limit.
if(MSVC)
    set_source_files_properties(nodes/key_value_accumulator.cpp
        PROPERTIES COMPILE_OPTIONS /bigobj)
endif()
target_link_libraries(flowboard_engine PUBLIC
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    rigtorp_spsc
    onboardapi::onboardapi
)
target_compile_features(flowboard_engine PUBLIC cxx_std_20)

configure_file(
    ${CMAKE_SOURCE_DIR}/schemas/graph.schema.json
    ${CMAKE_BINARY_DIR}/generated/graph_schema_embedded.cpp.in
    COPYONLY)

file(READ ${CMAKE_BINARY_DIR}/generated/graph_schema_embedded.cpp.in _OP_SCHEMA_CONTENT)
file(WRITE ${CMAKE_BINARY_DIR}/generated/graph_schema_embedded.cpp
    "#include \"config/schema.hpp\"\n"
    "namespace flowboard::config {\n"
    "static const char kSchema[] = R\"OPSCHEMA(\n"
    "${_OP_SCHEMA_CONTENT}\n"
    ")OPSCHEMA\";\n"
    "std::string_view graph_schema_json() { return kSchema; }\n"
    "}\n")
target_sources(flowboard_engine PRIVATE ${CMAKE_BINARY_DIR}/generated/graph_schema_embedded.cpp)

file(GLOB _all_rmodels CONFIGURE_DEPENDS ${ONBOARDAPI_DATAMODEL_DIR}/*.rmodel)

if(FLOWBOARD_USE_STUB_SDK)
    flowboard_add_codegen(onboardapi_gen
        OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/onboardapi
        RMODELS ${_all_rmodels}
    )
else()
    flowboard_add_codegen(onboardapi_gen
        OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/onboardapi
        RMODELS ${_all_rmodels}
        REAL_SDK
    )
endif()

set(_gen_dir ${CMAKE_BINARY_DIR}/generated/onboardapi)

# Allow graph.cpp to include generated type headers for dispatch arms.
# This is NOT a link-time cycle: flowboard_engine does not link
# onboardapi_generated; it only needs the headers at compile time.
target_include_directories(flowboard_engine PRIVATE ${_gen_dir})
if(FLOWBOARD_USE_STUB_SDK)
    target_compile_definitions(flowboard_engine PRIVATE FLOWBOARD_HAS_STUB_SDK=1)
endif()
add_dependencies(flowboard_engine onboardapi_gen_codegen)
# The .cpp files are produced by pipegen at build time.
# Derive the list from the rmodel filenames (each rmodel → M_<basename>_nodes.cpp,
# with "Types" stripped since both Foo.rmodel and FooTypes.rmodel merge into M_Foo).
set(_gen_node_cpps "${_gen_dir}/registry_init.cpp")
foreach(_rm IN LISTS _all_rmodels)
    get_filename_component(_n ${_rm} NAME_WE)
    # Strip trailing "Types" so M_FooTypes and M_Foo both map to M_Foo_nodes.cpp
    string(REGEX REPLACE "Types$" "" _module_short ${_n})
    list(APPEND _gen_node_cpps "${_gen_dir}/M_${_module_short}_nodes.cpp")
endforeach()
list(REMOVE_DUPLICATES _gen_node_cpps)
# Mark them GENERATED so CMake does not require them to exist at configure time.
set_source_files_properties(${_gen_node_cpps} PROPERTIES GENERATED TRUE)

add_library(onboardapi_generated STATIC ${_gen_node_cpps})
add_dependencies(onboardapi_generated onboardapi_gen_codegen)
# Hand-written real-SDK live-discovery backend (empty TU under the stub SDK).
target_sources(onboardapi_generated PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/onboardapi/runtime/live_discovery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/onboardapi/runtime/device_report_node.cpp)
if(NOT FLOWBOARD_USE_STUB_SDK)
    target_compile_definitions(onboardapi_generated PRIVATE FLOWBOARD_REAL_SDK=1)
endif()
target_include_directories(onboardapi_generated PUBLIC
    ${_gen_dir}
    ${CMAKE_BINARY_DIR}/generated   # version_info.hpp
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(onboardapi_generated PUBLIC
    flowboard_engine
    onboardapi::onboardapi
)
target_compile_features(onboardapi_generated PUBLIC cxx_std_20)
# The per-struct Factory.* nodes generated alongside Service/Client classes
# push some modules (M_Common in particular) past MSVC's default object-file
# section limit (~64K). /bigobj raises it.
if(MSVC)
    target_compile_options(onboardapi_generated PRIVATE /bigobj)
endif()

# Embed the Windows application icon (app.rc -> icon.ico) into the executable.
set(_flowboard_rc "")
if(WIN32)
    set(_flowboard_rc ${CMAKE_SOURCE_DIR}/app.rc)
endif()
add_executable(flowboard main.cpp ${_flowboard_rc})
target_link_libraries(flowboard PRIVATE flowboard_engine)
# MSVC silently strips unreferenced TUs from static libs, killing static
# AutoRegister initialisers.  /WHOLEARCHIVE forces every object file in.
if(MSVC)
    target_link_options(flowboard PRIVATE
        /WHOLEARCHIVE:$<TARGET_FILE:flowboard_engine>
    )
endif()
# Link the generated registry into the executable so static initialisers run
# (works for both stub and real SDK builds).
target_link_libraries(flowboard PRIVATE onboardapi_generated)
if(MSVC)
    target_link_options(flowboard PRIVATE
        /WHOLEARCHIVE:$<TARGET_FILE:onboardapi_generated>
    )
endif()
if(NOT FLOWBOARD_USE_STUB_SDK)
    # Stage CycloneDDS + ddkit + onboardapi runtime DLLs next to the engine exe.
    onboardapi_stage_runtime_dlls(flowboard)
endif()

if(FLOWBOARD_BUILD_CONTROL_PLANE)
    add_subdirectory(control)
    target_link_libraries(flowboard PRIVATE flowboard_control)
    target_compile_definitions(flowboard PRIVATE FLOWBOARD_HAS_CONTROL_PLANE=1)
endif()

if(FLOWBOARD_BUILD_WEB)
    set(_ts_out ${CMAKE_SOURCE_DIR}/web/src/generated)
    file(MAKE_DIRECTORY ${_ts_out})

    set(_ts_unused_dir ${CMAKE_BINARY_DIR}/generated/pipegen_ts_unused)
    file(MAKE_DIRECTORY ${_ts_unused_dir})

    file(GLOB _all_rmodels CONFIGURE_DEPENDS ${ONBOARDAPI_DATAMODEL_DIR}/*.rmodel)

    add_custom_command(
        OUTPUT ${_ts_out}/index.ts
        COMMAND $<TARGET_FILE:pipegen> ${_ts_unused_dir}
                ${_all_rmodels} --ts-out ${_ts_out}
        DEPENDS pipegen ${_all_rmodels}
        COMMENT "pipegen --ts-out -> ${_ts_out}"
        VERBATIM
    )
    add_custom_target(pipegen_ts ALL DEPENDS ${_ts_out}/index.ts)
endif()
