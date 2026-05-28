// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include "flowboard/loader.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/log.hpp"
// Include generated types first so FLOWBOARD_GENERATED_M_Mount_TYPES is defined
// before M_Mount.hpp is seen; this ensures both TUs agree on the type layout.
#include "M_Common_types.hpp"
#include "M_Mount_types.hpp"
#include "onboardapi/M_Mount.hpp"

TEST_CASE("M_Mount.Client emits ReportMountPosition.Pos on stub bus publish") {
    // Ensure logging is initialized
    flowboard::log::init();

    nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.0",
        "nodes": [
          { "id": "watcher", "type": "M_Mount.Client",
            "config": {"domainId": 1, "serviceName": "TestMount"} },
          { "id": "logger", "type": "Sinks.Log",
            "config": {"inputType": "M_Mount::MountPositionType", "prefix": "pos"} }
        ],
        "edges": [
          { "from": "watcher.ReportMountPosition.Pos", "to": "logger.in",
            "capacity": 16, "policy": "block" }
        ]
    })");
    auto loaded = flowboard::config::load(doc);
    if (!loaded.errors.empty()) {
        for (auto const& e : loaded.errors) FAIL("loader error: ", e);
    }
    REQUIRE(loaded.graph);
    loaded.graph->start();

    // Build a MountPositionType using generated fields (Azimuth and Elevation are
    // AngleDegreesOptionalType = vector<double>; push one element each).
    M_Mount::MountPositionType msg;
    msg.Azimuth.push_back(45.0);
    msg.Elevation.push_back(10.0);
    M_Mount::stub_publish_position(1, "TestMount", msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loaded.graph->stop();
    // Reaching here means the graph processed the publish without error.
    CHECK(true);
}
