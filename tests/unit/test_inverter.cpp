// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <chrono>
#include <thread>
#include <vector>

using namespace flowboard;

TEST_CASE("Inverter negates each boolean it receives") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Inverter", "inv1", {});
    REQUIRE(node);

    std::vector<bool> got;
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { got.push_back(*v); });

    node->start();
    auto* in = dynamic_cast<InputPort<bool>*>(node->input("in"));
    REQUIRE(in);

    in->deliver(std::make_shared<const bool>(true));
    in->deliver(std::make_shared<const bool>(false));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    node->stop();

    REQUIRE(got.size() == 2);
    CHECK(got[0] == false);
    CHECK(got[1] == true);
}
