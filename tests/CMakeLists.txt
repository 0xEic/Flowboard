set(_op_test_sources
    unit/test_type_tag.cpp
    unit/test_port.cpp
    unit/test_edge.cpp
    unit/test_node.cpp
    unit/test_registry.cpp
    unit/test_extract_registry.cpp
    unit/test_extract_nodes.cpp
    unit/test_json_helpers.cpp
    unit/test_extract_node_factory.cpp
    unit/test_registry_schema.cpp
    unit/test_catalog_schemas.cpp
    unit/test_graph.cpp
    unit/test_error_budget.cpp
    unit/test_threshold.cpp
    unit/test_compare.cpp
    unit/test_convert.cpp
    unit/test_derivative.cpp
    unit/test_filter.cpp
    unit/test_inverter.cpp
    unit/test_throttle.cpp
    unit/test_iterator.cpp
    unit/test_synchronize.cpp
    unit/test_constant_source.cpp
    unit/test_log_node.cpp
    unit/test_loader.cpp
    unit/test_key_value_accumulator.cpp
    unit/test_list_ops.cpp
    unit/test_flow_machine.cpp
    unit/test_button_handler.cpp
    unit/test_random_source.cpp
    unit/test_arithmetic.cpp
    unit/test_number_holder.cpp
    unit/test_list_create.cpp
    unit/test_device_report.cpp
    unit/test_onboardapi_discovery.cpp
    unit/test_manipulate.cpp
    unit/test_coalescing_mailbox.cpp
    integration/test_extract_graph.cpp
    integration/test_keyvalue_pipeline.cpp
)
# These integration tests hardcode the stub-only convenience headers
# (onboardapi/M_Mount.hpp, sdk_stub/src/stub_bus.hpp) without #ifdef guards,
# so they only compile under the stub SDK. Skip them in real-SDK builds.
if(FLOWBOARD_USE_STUB_SDK)
    list(APPEND _op_test_sources
        integration/test_mount_altitude_alert.cpp
        integration/test_generated_mount_alert.cpp
        unit/test_reliability_nodes.cpp
    )
endif()
add_executable(flowboard_tests ${_op_test_sources})
target_link_libraries(flowboard_tests PRIVATE
    flowboard_engine
    doctest::doctest
)
if(FLOWBOARD_BUILD_CONTROL_PLANE)
    target_link_libraries(flowboard_tests PRIVATE flowboard_control)
endif()
# MSVC silently strips unreferenced TUs from static libs, killing static
# AutoRegister initialisers.  /WHOLEARCHIVE forces every object file in.
if(MSVC)
    target_link_options(flowboard_tests PRIVATE
        /WHOLEARCHIVE:$<TARGET_FILE:flowboard_engine>
    )
endif()
target_include_directories(flowboard_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated/onboardapi
)
# Tests reference generated struct headers (M_Common_types.hpp etc.),
# SDK headers (onboardapi/M_Mount.hpp etc.), and rely on OP_REGISTER_*
# initialisers in onboardapi_generated — all needed in both stub and
# real-SDK modes. onboardapi::onboardapi is linked explicitly because the
# real-SDK imported target's headers don't propagate transitively from
# flowboard_engine on every platform.
target_link_libraries(flowboard_tests PRIVATE
    onboardapi_generated
    onboardapi::onboardapi
)
if(MSVC)
    target_link_options(flowboard_tests PRIVATE
        /WHOLEARCHIVE:$<TARGET_FILE:onboardapi_generated>
    )
endif()
add_test(NAME flowboard_tests
         COMMAND flowboard_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ----------------------------------------------------------------------------
# E2E test: separate driver exe + spawn the real flowboard binary.
# Skipped automatically on stub SDK builds (process-local bus can't bridge).
# ----------------------------------------------------------------------------
if(FLOWBOARD_BUILD_CONTROL_PLANE)
    add_executable(e2e_mount_alert_driver
        integration/e2e_mount_alert_driver.cpp)
    target_include_directories(e2e_mount_alert_driver PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated/onboardapi)
    target_link_libraries(e2e_mount_alert_driver PRIVATE
        onboardapi::onboardapi)
    # Driver needs the generated type-tag registrations + struct headers in both modes.
    target_link_libraries(e2e_mount_alert_driver PRIVATE onboardapi_generated)
    if(FLOWBOARD_USE_STUB_SDK)
        target_compile_definitions(e2e_mount_alert_driver PRIVATE
            FLOWBOARD_HAS_STUB_SDK)
        if(MSVC)
            target_link_options(e2e_mount_alert_driver PRIVATE
                /WHOLEARCHIVE:$<TARGET_FILE:onboardapi_generated>)
        endif()
    else()
        onboardapi_stage_runtime_dlls(e2e_mount_alert_driver)
    endif()

    if(FLOWBOARD_USE_STUB_SDK)
        set(_e2e_use_stub "ON")
        set(_e2e_cyclone_uri "")
    else()
        set(_e2e_use_stub "OFF")
        set(_e2e_cyclone_uri "${ONBOARDAPI_CYCLONEDDS_URI}")
    endif()
    add_test(
        NAME e2e_mount_alert
        COMMAND ${CMAKE_COMMAND}
            -DFLOWBOARD_EXE=$<TARGET_FILE:flowboard>
            -DDRIVER_EXE=$<TARGET_FILE:e2e_mount_alert_driver>
            -DEXAMPLE_JSON=${CMAKE_SOURCE_DIR}/tests/integration/fixtures/e2e-mount-alert.json
            -DUSE_STUB_SDK=${_e2e_use_stub}
            -DCYCLONEDDS_URI=${_e2e_cyclone_uri}
            -P ${CMAKE_SOURCE_DIR}/tests/integration/e2e_run.cmake)
    # ctest convention: exit code 77 = SKIP.
    set_tests_properties(e2e_mount_alert PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 60)
endif()

if(FLOWBOARD_BUILD_BENCHMARKS)
    add_executable(bench_pipeline perf/bench_pipeline.cpp)
    target_link_libraries(bench_pipeline PRIVATE flowboard_engine)
    target_include_directories(bench_pipeline PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        target_link_options(bench_pipeline PRIVATE
            /WHOLEARCHIVE:flowboard_engine)
    endif()
endif()
