// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

// Include generated types first so the guard macros are set before M_Mount.hpp /
// M_Alert.hpp are seen. This ensures this TU agrees with the engine on the
// generated layout (not the stub hand-coded layout).
#include "M_Common_types.hpp"
#include "M_Mount_types.hpp"
#include "M_Alert_types.hpp"
#include "onboardapi/M_Mount.hpp"
#include "onboardapi/M_Alert.hpp"

#include "flowboard/loader.hpp"
#include "flowboard/log.hpp"

// stub_bus.hpp is a private header inside sdk_stub/src/, accessed directly
// so we can subscribe to the bus from the test.
#include "../../sdk_stub/src/stub_bus.hpp"

TEST_CASE("Mount altitude alert example raises an alarm via stub bus") {
    flowboard::log::init();

    // Pinned to the dedicated e2e fixture rather than the user-facing example,
    // which is free to evolve for demonstration. The fixture keeps the
    // Client-based alarm-raising topology this test asserts against.
    std::ifstream f("tests/integration/fixtures/e2e-mount-alert.json");
    REQUIRE(f.good());
    nlohmann::json doc;
    f >> doc;

    auto loaded = flowboard::config::load(doc);
    if (!loaded.errors.empty()) {
        for (auto const& e : loaded.errors) FAIL("loader error: ", e);
    }
    REQUIRE(loaded.graph);

    // Subscribe to CmdRaiseAlarm publishes on the bus before starting the graph.
    std::atomic<int> alarms{0};
    auto sub_id = onboardapi::stub::Bus::instance().subscribe(
        1, "MainAlert", "CmdRaiseAlarm",
        [&](void const*) { alarms.fetch_add(1); });

    loaded.graph->start();

    // Publish 10 position messages with Elevation > 25 degrees to trip the threshold.
    // MountPositionType::Elevation is AngleDegreesOptionalType (vector<double>).
    for (int i = 0; i < 10; ++i) {
        M_Mount::MountPositionType msg;
        msg.Elevation.push_back(45.0);  // 45 degrees > threshold of 25
        M_Mount::stub_publish_position(1, "MainMount", msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Allow the pipeline to drain.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    loaded.graph->stop();

    onboardapi::stub::Bus::instance().unsubscribe(sub_id);

    CHECK(alarms.load() > 0);
}
