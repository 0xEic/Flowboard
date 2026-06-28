# Example Flowboard node plugin.
#
# Built as a MODULE library: loaded at runtime via dlopen/LoadLibrary and never
# linked against. It links only the lightweight flowboard_plugin_sdk (no
# OnboardAPI dependency), so it builds anywhere the public headers are available.
add_library(hello_plugin MODULE hello_plugin.cpp)

target_link_libraries(hello_plugin PRIVATE flowboard_plugin_sdk)
target_compile_features(hello_plugin PRIVATE cxx_std_20)

# No "lib" prefix on Unix; the host discovers plugins purely by file extension.
set_target_properties(hello_plugin PROPERTIES PREFIX "")

# Drop the built plugin into a "plugins" folder next to the flowboard executable
# so a development build auto-discovers it at startup (matches the default plugin
# search path). Both properties are set so this works on Windows and Unix.
set_target_properties(hello_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:flowboard>/plugins"
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:flowboard>/plugins"
)
