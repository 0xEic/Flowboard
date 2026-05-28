# SPDX-License-Identifier: MIT
file(GLOB _stub_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(onboardapi_stub STATIC ${_stub_srcs})
target_include_directories(onboardapi_stub PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(onboardapi_stub PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(onboardapi_stub PUBLIC cxx_std_20)
add_library(onboardapi::onboardapi ALIAS onboardapi_stub)

# Generate stub SDK headers from rmodels.
# These contain the authoritative struct types + IService/IClient virtual shells
# that the hand-written stub .cpp files implement.
set(_stub_out ${CMAKE_BINARY_DIR}/generated/sdk_stub/onboardapi)
file(MAKE_DIRECTORY ${_stub_out})

file(GLOB _rmodels ${ONBOARDAPI_DATAMODEL_DIR}/*.rmodel)

# Use a throwaway dir for the engine-side types/nodes output so they don't conflict
# with the engine codegen output in generated/onboardapi.
set(_stub_types_unused ${CMAKE_BINARY_DIR}/generated/sdk_stub_unused)
file(MAKE_DIRECTORY ${_stub_types_unused})

add_custom_command(
    OUTPUT ${_stub_out}/M_Common_stub.hpp
    COMMAND $<TARGET_FILE:pipegen>
            ${_stub_types_unused}
            ${_rmodels}
            --stub-out ${_stub_out}
    DEPENDS pipegen ${_rmodels}
    COMMENT "pipegen --stub-out -> ${_stub_out}"
    VERBATIM
)
add_custom_target(sdk_stub_codegen ALL DEPENDS ${_stub_out}/M_Common_stub.hpp)
add_dependencies(onboardapi_stub sdk_stub_codegen)

# Expose the generated stub headers so consumers can #include "<module>_stub.hpp".
target_include_directories(onboardapi_stub PUBLIC ${_stub_out})
