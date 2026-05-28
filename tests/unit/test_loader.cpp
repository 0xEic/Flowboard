// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/loader.hpp"
#include "flowboard/graph.hpp"
#include "flowboard/port.hpp"
#include "flowboard/reliability.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace flowboard;

TEST_CASE("Loader builds a working two-node graph from JSON") {
    nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.0",
        "nodes": [
          { "id": "th",  "type": "Transform.Threshold",
            "config": {"inputType":"flowboard::Double","op":">","value":1.0} },
          { "id": "snk", "type": "Sinks.Log",
            "config": {"inputType":"flowboard::Bool","prefix":"trig"} }
        ],
        "edges": [
          { "from": "th.out", "to": "snk.in", "capacity": 8, "policy": "block" }
        ]
    })");
    auto result = config::load(doc);
    REQUIRE(result.errors.empty());
    REQUIRE(result.graph);
    result.graph->start();
    auto* in = dynamic_cast<InputPort<double>*>(result.graph->node("th")->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const double>(2.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    result.graph->stop();
}

TEST_CASE("Loader collects multiple errors") {
    nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.0",
        "nodes": [
          { "id": "a",  "type": "Does.Not.Exist" },
          { "id": "b",  "type": "Transform.Threshold",
            "config": {"inputType":"flowboard::Double","op":">","value":1.0} }
        ],
        "edges": [
          { "from": "a.x", "to": "b.in" },
          { "from": "b.out", "to": "missing.in" }
        ]
    })");
    auto result = config::load(doc);
    CHECK(result.errors.size() >= 2);
    CHECK_FALSE(result.graph);
}

TEST_CASE("default_edge_policy derives from source port op prefix") {
    CHECK(default_edge_policy("ReportAxis.Value")   == BackpressurePolicy::DropOldest);
    CHECK(default_edge_policy("ConfigSlewLimits.X")  == BackpressurePolicy::DropOldest);
    CHECK(default_edge_policy("CmdMode.Mode")        == BackpressurePolicy::Block);
    CHECK(default_edge_policy("NotifyAngle.Azimuth") == BackpressurePolicy::Block);
    CHECK(default_edge_policy("EventButton.Index")   == BackpressurePolicy::Block);
    CHECK(default_edge_policy("out")                 == BackpressurePolicy::DropOldest);
}
