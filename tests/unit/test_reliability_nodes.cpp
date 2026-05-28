// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include "M_Mount_types.hpp"
#include "onboardapi/M_Mount.hpp"

using namespace flowboard;

// ReportMountPosition is a Report* op on M_Mount::IClient with a single portable
// param (Pos : MountPositionType) and no Key* params, so the generated client
// node coalesces it to the latest value on the receive path (SDK callback ->
// out_ReportMountPosition_Pos). Firing 50 callbacks back-to-back before the
// worker drains should collapse to far fewer emits (>=1, <50).
TEST_CASE("Report coalesces to latest on receive") {
    auto node = NodeRegistry::instance().create("M_Mount.Client", "c", nlohmann::json::object());
    REQUIRE(node);
    auto* iclient = dynamic_cast<::M_Mount::IClient*>(node.get());
    REQUIRE(iclient);
    auto* out = dynamic_cast<OutputPort<::M_Mount::MountPositionType>*>(
        node->output("ReportMountPosition.Pos"));
    REQUIRE(out);

    std::atomic<int> count{0};
    out->attach_sink([&](std::shared_ptr<const ::M_Mount::MountPositionType>) {
        count.fetch_add(1);
    });

    node->start();
    ::M_Mount::MountPositionType pos{};
    for (int i = 0; i < 50; ++i) iclient->ReportMountPosition(pos);

    for (int i = 0; i < 100 && count.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(count.load() >= 1);
    CHECK(count.load() < 50);
    node->stop();
}
