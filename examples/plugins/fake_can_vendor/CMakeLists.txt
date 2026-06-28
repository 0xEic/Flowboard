# Fake vendor CAN-adapter plugin. Built as a MODULE library (loaded at runtime
# via dlopen / LoadLibrary, never linked against). Links only the lightweight
# flowboard_plugin_sdk — no OnboardAPI dependency — same as a real OSS-friendly
# vendor plugin would.
add_library(fake_can_vendor MODULE fake_can_vendor.cpp)

target_link_libraries(fake_can_vendor PRIVATE flowboard_plugin_sdk)
target_compile_features(fake_can_vendor PRIVATE cxx_std_20)

# No "lib" prefix on Unix; the host discovers plugins by file extension.
set_target_properties(fake_can_vendor PROPERTIES PREFIX "")

# Land in the auto-loaded plugins/ dir alongside the flowboard exe so a fresh
# build picks it up at startup (matches the default search path).
set_target_properties(fake_can_vendor PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:flowboard>/plugins"
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:flowboard>/plugins"
)
