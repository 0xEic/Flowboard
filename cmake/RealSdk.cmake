# SPDX-License-Identifier: MIT
# Locate the real (non-stub) OnboardAPI 9.x SDK and expose it as
# onboardapi::onboardapi. The user provides the SDK root via:
#   -DONBOARDAPI_SDK_ROOT=<path>      explicit override
# or the env var ONBOARDAPI_SDK_ROOT.
#
# Also exposes ONBOARDAPI_RUNTIME_DLLS so callers can stage the four DLLs
# the SDK needs at runtime (onboardapi-cyclone, ddkit-cyclone, ddsc, ddscxx)
# next to executables and tests.

if(NOT ONBOARDAPI_SDK_ROOT AND DEFINED ENV{ONBOARDAPI_SDK_ROOT})
    set(ONBOARDAPI_SDK_ROOT "$ENV{ONBOARDAPI_SDK_ROOT}")
endif()

if(NOT ONBOARDAPI_SDK_ROOT OR NOT EXISTS "${ONBOARDAPI_SDK_ROOT}/cmake/Findonboardapi.cmake")
    message(FATAL_ERROR
        "Real OnboardAPI SDK not found. Set -DONBOARDAPI_SDK_ROOT=<extracted SDK dir> "
        "or env ONBOARDAPI_SDK_ROOT. The dir must contain cmake/Findonboardapi.cmake.")
endif()

message(STATUS "OnboardAPI real SDK root: ${ONBOARDAPI_SDK_ROOT}")

list(APPEND CMAKE_MODULE_PATH "${ONBOARDAPI_SDK_ROOT}/cmake")

find_package(onboardapi REQUIRED)
# Findonboardapi.cmake creates the onboardapi::onboardapi INTERFACE IMPORTED target.

# Collect the runtime DLLs (Windows shared build).
set(_dll_candidates
    "${ONBOARDAPI_SDK_ROOT}/bin/onboardapi-cyclone.dll"
    "${ONBOARDAPI_SDK_ROOT}/bin/ddkit-cyclone.dll"
    "${ONBOARDAPI_SDK_ROOT}/bin/ddsc.dll"
    "${ONBOARDAPI_SDK_ROOT}/bin/ddscxx.dll")
set(_dlls_present "")
foreach(_dll IN LISTS _dll_candidates)
    if(EXISTS "${_dll}")
        list(APPEND _dlls_present "${_dll}")
    endif()
endforeach()
set(ONBOARDAPI_RUNTIME_DLLS "${_dlls_present}")

# Helper: copy the runtime DLLs next to a target's binary at post-build time.
function(onboardapi_stage_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()
    if(NOT ONBOARDAPI_RUNTIME_DLLS)
        return()
    endif()
    foreach(_dll IN LISTS ONBOARDAPI_RUNTIME_DLLS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endforeach()
endfunction()

# Write a CycloneDDS localhost configuration into the build dir. This is the
# minimal setup so two single-host processes (engine + driver) can talk to
# each other via the loopback adapter without network configuration.
set(_cyclone_xml "${CMAKE_BINARY_DIR}/cyclone.xml")
file(WRITE "${_cyclone_xml}"
"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>
<CycloneDDS xmlns=\"https://cdds.io/config\">
  <Domain Id=\"any\">
    <General>
      <Interfaces>
        <NetworkInterface address=\"127.0.0.1\" prefer_multicast=\"false\"/>
      </Interfaces>
    </General>
    <Discovery>
      <ParticipantIndex>auto</ParticipantIndex>
    </Discovery>
  </Domain>
</CycloneDDS>
")
# CycloneDDS on Windows accepts both file:// URIs and plain paths.
# We pass the plain path here; the older `file:///<drive>:/...` form caused
# CycloneDDS 11.x to log "can't open configuration file" on some Windows hosts.
set(ONBOARDAPI_CYCLONEDDS_URI "${_cyclone_xml}")
message(STATUS "CYCLONEDDS_URI for tests: ${_cyclone_xml}")
