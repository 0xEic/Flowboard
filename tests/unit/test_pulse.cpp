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

// Transform.Pulse turns a boolean stream into a "true" trigger pulse on every
// change — so a pushbutton's press (true) and release (false) each fire it.
TEST_CASE("Pulse emits a true pulse on each change of its boolean input") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Pulse", "pulse1", {});
    REQUIRE(node);

    std::vector<bool> got;
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { got.push_back(*v); });

    node->start();
    auto* in = dynamic_cast<InputPort<bool>*>(node->input("in"));
    REQUIRE(in);

    in->deliver(std::make_shared<const bool>(true));   // push    -> change
    in->deliver(std::make_shared<const bool>(false));  // release -> change
    in->deliver(std::make_shared<const bool>(true));   // push    -> change
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    node->stop();

    REQUIRE(got.size() == 3);
    CHECK(got[0] == true);
    CHECK(got[1] == true);
    CHECK(got[2] == true);
}

TEST_CASE("Pulse suppresses repeated identical values") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Pulse", "pulse2", {});
    REQUIRE(node);

    std::vector<bool> got;
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { got.push_back(*v); });

    node->start();
    auto* in = dynamic_cast<InputPort<bool>*>(node->input("in"));
    REQUIRE(in);

    in->deliver(std::make_shared<const bool>(true));    // first    -> pulse
    in->deliver(std::make_shared<const bool>(true));    // repeat   -> suppressed
    in->deliver(std::make_shared<const bool>(false));   // change   -> pulse
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    node->stop();

    REQUIRE(got.size() == 2);
    CHECK(got[0] == true);
    CHECK(got[1] == true);
}
