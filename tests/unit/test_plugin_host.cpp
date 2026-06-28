// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/plugin_host.hpp"
#include "flowboard/registry.hpp"

TEST_CASE("PluginHost no-op on missing directory") {
    flowboard::PluginHost host;
    auto& reg = flowboard::NodeRegistry::instance();
    auto before = reg.registered_names().size();
    int n = host.load_directory("/path/that/does/not/exist", reg);
    CHECK(n == 0);
    CHECK(reg.registered_names().size() == before);
}
