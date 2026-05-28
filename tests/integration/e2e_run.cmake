# SPDX-License-Identifier: MIT
#
# E2E test harness. Invoked via `cmake -P e2e_run.cmake` with these -D vars:
#   FLOWBOARD_EXE   absolute path to the engine binary
#   DRIVER_EXE         absolute path to e2e_mount_alert_driver
#   EXAMPLE_JSON       absolute path to examples/mount-altitude-alert.json
#   USE_STUB_SDK       "ON" -> exit 77 (ctest SKIP); "OFF" -> run the test
#   ENGINE_PORT        HTTP port for the engine (default 18765)
#   HEALTH_TIMEOUT_S   max seconds to wait for /api/health (default 10)
#
# Behaviour:
#   - With stub SDK: exits 77 immediately. The stub bus is process-local so
#     cross-process publish from the driver cannot reach the engine; the
#     unit-level integration test (tests/integration/test_mount_altitude_alert.cpp)
#     covers behaviour verification in-process.
#   - With real SDK: spawns the engine in the background, polls /api/health,
#     runs the driver synchronously, then terminates the engine. Propagates
#     the driver's exit code.

cmake_minimum_required(VERSION 3.20)

if(NOT FLOWBOARD_EXE OR NOT DRIVER_EXE OR NOT EXAMPLE_JSON)
    message(FATAL_ERROR
        "Required: -DFLOWBOARD_EXE=... -DDRIVER_EXE=... -DEXAMPLE_JSON=...")
endif()
if(NOT EXISTS "${FLOWBOARD_EXE}")
    message(FATAL_ERROR "engine not found: ${FLOWBOARD_EXE}")
endif()
if(NOT EXISTS "${DRIVER_EXE}")
    message(FATAL_ERROR "driver not found: ${DRIVER_EXE}")
endif()
if(NOT EXISTS "${EXAMPLE_JSON}")
    message(FATAL_ERROR "example not found: ${EXAMPLE_JSON}")
endif()

if(USE_STUB_SDK STREQUAL "ON")
    message(STATUS
        "[e2e] FLOWBOARD_USE_STUB_SDK=ON; stub bus is process-local. "
        "Skipping cross-process test (returning ctest SKIP code 77). "
        "The in-process integration test covers behaviour for stub builds.")
    cmake_language(EXIT 77)
endif()

if(NOT ENGINE_PORT)
    set(ENGINE_PORT 18765)
endif()
if(NOT HEALTH_TIMEOUT_S)
    set(HEALTH_TIMEOUT_S 10)
endif()

# Launch the engine as a detached background process. Redirect output to a
# log file so the test report can show what happened on failure.
set(ENGINE_LOG     "${CMAKE_CURRENT_LIST_DIR}/e2e_engine.log")
set(ENGINE_LOG_ERR "${CMAKE_CURRENT_LIST_DIR}/e2e_engine.err.log")
file(REMOVE "${ENGINE_LOG}" "${ENGINE_LOG_ERR}")

# execute_process can't truly background a child; spawn via the platform shell
# so the engine survives this script's lifetime, then capture its PID for kill.
set(PID_FILE "${CMAKE_CURRENT_LIST_DIR}/e2e_engine.pid")
file(REMOVE "${PID_FILE}")

# Propagate CYCLONEDDS_URI to both spawned processes when real SDK is in use.
if(CYCLONEDDS_URI)
    set(ENV{CYCLONEDDS_URI} "${CYCLONEDDS_URI}")
    message(STATUS "[e2e] CYCLONEDDS_URI=${CYCLONEDDS_URI}")
endif()

if(WIN32)
    # PowerShell Start-Process returns a Process object; capture its Id.
    # Note: $env:CYCLONEDDS_URI inherits from this CMake process when set above.
    # CAUTION: PowerShell statement separator `;` is also CMake's list separator.
    # We use a bracket-escaped string (no list expansion) and pass the whole
    # thing as one argument to powershell -Command.
    set(_ps_cmd "$p = Start-Process -FilePath '${FLOWBOARD_EXE}' -ArgumentList '--port','${ENGINE_PORT}','${EXAMPLE_JSON}' -WindowStyle Hidden -RedirectStandardOutput '${ENGINE_LOG}' -RedirectStandardError '${ENGINE_LOG_ERR}' -PassThru\nSet-Content -Path '${PID_FILE}' -Value $p.Id")
    execute_process(
        COMMAND powershell -NoProfile -Command "${_ps_cmd}"
        RESULT_VARIABLE _spawn_rc)
else()
    execute_process(
        COMMAND sh -c
        "'${FLOWBOARD_EXE}' --port ${ENGINE_PORT} '${EXAMPLE_JSON}' \
            > '${ENGINE_LOG}' 2>&1 & echo $! > '${PID_FILE}'"
        RESULT_VARIABLE _spawn_rc)
endif()
if(NOT _spawn_rc EQUAL 0)
    message(FATAL_ERROR "[e2e] failed to spawn engine (rc=${_spawn_rc})")
endif()

# Poll /api/health until alive or timeout.
set(_health_url "http://127.0.0.1:${ENGINE_PORT}/api/health")
set(_ready 0)
foreach(_i RANGE 1 ${HEALTH_TIMEOUT_S})
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E sleep 1
        RESULT_VARIABLE _ignored OUTPUT_QUIET ERROR_QUIET)
    file(DOWNLOAD "${_health_url}" "${CMAKE_CURRENT_LIST_DIR}/e2e_health.tmp"
         STATUS _dl_status TIMEOUT 1)
    list(GET _dl_status 0 _dl_code)
    if(_dl_code EQUAL 0)
        set(_ready 1)
        break()
    endif()
endforeach()
file(REMOVE "${CMAKE_CURRENT_LIST_DIR}/e2e_health.tmp")

function(kill_engine)
    if(NOT EXISTS "${PID_FILE}")
        return()
    endif()
    file(READ "${PID_FILE}" _engine_pid)
    string(STRIP "${_engine_pid}" _engine_pid)
    if(NOT _engine_pid MATCHES "^[0-9]+$")
        return()
    endif()
    if(WIN32)
        execute_process(COMMAND taskkill /F /PID ${_engine_pid}
            RESULT_VARIABLE _kill_rc OUTPUT_QUIET ERROR_QUIET)
    else()
        execute_process(COMMAND kill -TERM ${_engine_pid}
            RESULT_VARIABLE _kill_rc OUTPUT_QUIET ERROR_QUIET)
    endif()
endfunction()

function(dump_engine_logs)
    if(EXISTS "${ENGINE_LOG}")
        file(READ "${ENGINE_LOG}" _log)
        message(STATUS "[e2e] engine stdout:\n${_log}")
    endif()
    if(EXISTS "${ENGINE_LOG_ERR}")
        file(READ "${ENGINE_LOG_ERR}" _err)
        message(STATUS "[e2e] engine stderr:\n${_err}")
    endif()
endfunction()

if(NOT _ready)
    kill_engine()
    dump_engine_logs()
    message(FATAL_ERROR
        "[e2e] engine did not become healthy within ${HEALTH_TIMEOUT_S}s")
endif()

# Run the driver synchronously.
execute_process(
    COMMAND "${DRIVER_EXE}"
        --domain 1
        --mount-service MainMount
        --alert-service MainAlert
        --pulses 10
        --elevation 45
    RESULT_VARIABLE _driver_rc
    OUTPUT_VARIABLE _driver_out
    ERROR_VARIABLE  _driver_err)
message(STATUS "[e2e] driver stdout:\n${_driver_out}")
if(_driver_err)
    message(STATUS "[e2e] driver stderr:\n${_driver_err}")
endif()

kill_engine()
file(REMOVE "${PID_FILE}")

if(NOT _driver_rc EQUAL 0)
    dump_engine_logs()
    message(FATAL_ERROR "[e2e] driver exited with code ${_driver_rc}")
endif()

message(STATUS "[e2e] PASSED")
