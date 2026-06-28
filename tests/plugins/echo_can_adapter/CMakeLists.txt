add_library(echo_can_adapter MODULE echo_can_adapter.cpp)
target_link_libraries(echo_can_adapter PRIVATE flowboard_plugin_sdk)
target_compile_features(echo_can_adapter PRIVATE cxx_std_20)
set_target_properties(echo_can_adapter PROPERTIES PREFIX "")
# Drop next to the flowboard exe under test_plugins/ so a test that fires up
# the plugin host can point at it explicitly.
set_target_properties(echo_can_adapter PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:flowboard>/test_plugins"
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:flowboard>/test_plugins"
)
