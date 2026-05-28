// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <memory>
#include <thread>
#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/onboardapi_discovery.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

using namespace flowboard;

namespace {
void wait() { std::this_thread::sleep_for(std::chrono::milliseconds(15)); }
}  // namespace

TEST_CASE("onboardapi_interface_type strips prefix and direction suffix") {
    CHECK(onboardapi_interface_type("M_Mount.Service") == "Mount");
    CHECK(onboardapi_interface_type("M_Alert.Client")  == "Alert");
    CHECK(onboardapi_interface_type("Mount")           == "Mount");
}

TEST_CASE("OnboardApiDiscovery open/close round-trips and filters by domain") {
    auto& disco = OnboardApiDiscovery::instance();
    // Use an isolated domain so concurrent endpoints don't interfere.
    constexpr int kDomain = 4242;
    auto t1 = disco.open(kDomain, "Mount", "Service", "turret");
    auto t2 = disco.open(kDomain, "Mount", "Client",  "gunner");
    auto t3 = disco.open(9999,    "Alert", "Service", "other");

    auto snap = disco.snapshot(kDomain);
    CHECK(snap.size() == 2);

    disco.close(t1);
    disco.close(t2);
    disco.close(t3);
    CHECK(disco.snapshot(kDomain).empty());
}

TEST_CASE("OnboardApi.Discovery emits sorted list of open endpoints on its domain") {
    auto& disco = OnboardApiDiscovery::instance();
    constexpr int kDomain = 4243;
    auto t1 = disco.open(kDomain, "Mount", "Service", "turret");
    auto t2 = disco.open(kDomain, "Mount", "Client",  "gunner");
    auto t3 = disco.open(7000,    "Alert", "Service", "elsewhere");  // other domain

    auto& reg  = NodeRegistry::instance();
    auto  node = reg.create("OnboardApi.Discovery", "disco",
                            ::nlohmann::json{{"domainId", kDomain}, {"source", "graph"}});
    REQUIRE(node);

    auto* out = dynamic_cast<OutputPort<ListValue>*>(node->output("out"));
    REQUIRE(out);
    std::shared_ptr<const ListValue> got;
    out->attach_sink([&](auto v) { got = v; });

    node->start();
    auto* trig = dynamic_cast<InputPort<bool>*>(node->input("trigger"));
    REQUIRE(trig);
    trig->deliver(std::make_shared<const bool>(true));
    wait();

    REQUIRE(got);
    REQUIRE(got->items.size() == 2);
    // Sorted by (interfaceType, direction): Client before Service.
    CHECK(got->items[0]["interfaceType"] == "Mount");
    CHECK(got->items[0]["direction"]     == "Client");
    CHECK(got->items[0]["serviceName"]   == "gunner");
    CHECK(got->items[0]["domainId"]      == kDomain);
    CHECK(got->items[1]["direction"]     == "Service");
    CHECK(got->items[1]["serviceName"]   == "turret");

    node->stop();
    disco.close(t1);
    disco.close(t2);
    disco.close(t3);
}

// End-to-end: a generated Service/Client node registers itself as open simply
// by being constructed (no start needed — the SDK handle is only built in
// on_start). Verifies the base-class hookup and interface-name derivation.
TEST_CASE("Generated Service/Client nodes register with discovery on construction") {
    auto& disco = OnboardApiDiscovery::instance();
    auto& reg   = NodeRegistry::instance();
    constexpr int kDomain = 7777;

    {
        auto svc = reg.create("M_Mount.Service", "mnt_svc",
                              ::nlohmann::json{{"domainId", kDomain}, {"serviceName", "turret"}});
        auto cli = reg.create("M_Mount.Client", "mnt_cli",
                              ::nlohmann::json{{"domainId", kDomain}, {"serviceName", "gunner"}});
        REQUIRE(svc);
        REQUIRE(cli);

        auto snap = disco.snapshot(kDomain);
        REQUIRE(snap.size() == 2);
        bool saw_service = false, saw_client = false;
        for (auto const& e : snap) {
            CHECK(e.interface_type == "Mount");
            if (e.direction == "Service") { saw_service = true; CHECK(e.service_name == "turret"); }
            if (e.direction == "Client")  { saw_client  = true; CHECK(e.service_name == "gunner"); }
        }
        CHECK(saw_service);
        CHECK(saw_client);
    }
    // Nodes destroyed → endpoints closed.
    CHECK(disco.snapshot(kDomain).empty());
}

TEST_CASE("scan_live returns empty with no scanner installed") {
    auto& disco = OnboardApiDiscovery::instance();
    disco.set_live_scanner(nullptr);
    CHECK_FALSE(disco.has_live_scanner());
    CHECK(disco.scan_live(1234).empty());
}

TEST_CASE("OnboardApi.Discovery (source=both) merges live endpoints not in the graph") {
    auto& disco = OnboardApiDiscovery::instance();
    constexpr int kDomain = 4244;
    auto t1 = disco.open(kDomain, "Mount", "Service", "turret");  // graph endpoint

    // Fake live scanner: re-reports the Mount service (so it is "both") plus an
    // Alert client that exists only on the bus (not in our graph).
    disco.set_live_scanner([](int domain) {
        std::vector<DiscoveredEndpoint> v;
        DiscoveredEndpoint a;
        a.domain_id = domain; a.interface_type = "Mount"; a.direction = "Service";
        a.service_name = "turret"; a.host_name = "host-a"; a.process_id = 42; a.actor_id = "A1";
        DiscoveredEndpoint b;
        b.domain_id = domain; b.interface_type = "Alert"; b.direction = "Client";
        b.service_name = "external"; b.host_name = "host-b"; b.process_id = 99; b.actor_id = "B2";
        v.push_back(a); v.push_back(b);
        return v;
    });
    CHECK(disco.has_live_scanner());

    auto& reg  = NodeRegistry::instance();
    auto  node = reg.create("OnboardApi.Discovery", "disco",
                            ::nlohmann::json{{"domainId", kDomain}, {"source", "both"}});
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<ListValue>*>(node->output("out"));
    std::shared_ptr<const ListValue> got;
    out->attach_sink([&](auto v) { got = v; });
    node->start();
    dynamic_cast<InputPort<bool>*>(node->input("trigger"))
        ->deliver(std::make_shared<const bool>(true));
    wait();

    REQUIRE(got);
    REQUIRE(got->items.size() == 2);
    // Sorted by (interfaceType, …): Alert before Mount.
    CHECK(got->items[0]["interfaceType"] == "Alert");
    CHECK(got->items[0]["direction"]     == "Client");
    CHECK(got->items[0]["serviceName"]   == "external");
    CHECK(got->items[0]["source"]        == "live");
    CHECK(got->items[0]["hostName"]      == "host-b");
    CHECK(got->items[0]["processId"]     == 99);
    // Mount service is in both graph and live.
    CHECK(got->items[1]["interfaceType"] == "Mount");
    CHECK(got->items[1]["source"]        == "both");
    CHECK(got->items[1]["hostName"]      == "host-a");

    node->stop();
    disco.set_live_scanner(nullptr);
    disco.close(t1);
}

TEST_CASE("OnboardApi.Discovery (source=graph) ignores the live scanner") {
    auto& disco = OnboardApiDiscovery::instance();
    constexpr int kDomain = 4245;
    disco.set_live_scanner([](int domain) {
        DiscoveredEndpoint b;
        b.domain_id = domain; b.interface_type = "Alert"; b.direction = "Client";
        b.service_name = "external";
        return std::vector<DiscoveredEndpoint>{b};
    });
    auto& reg  = NodeRegistry::instance();
    auto  node = reg.create("OnboardApi.Discovery", "disco",
                            ::nlohmann::json{{"domainId", kDomain}, {"source", "graph"}});
    auto* out = dynamic_cast<OutputPort<ListValue>*>(node->output("out"));
    std::shared_ptr<const ListValue> got;
    out->attach_sink([&](auto v) { got = v; });
    node->start();
    dynamic_cast<InputPort<bool>*>(node->input("trigger"))
        ->deliver(std::make_shared<const bool>(true));
    wait();
    REQUIRE(got);
    CHECK(got->items.empty());  // no graph endpoints on this domain; live ignored
    node->stop();
    disco.set_live_scanner(nullptr);
}

TEST_CASE("Catalog probe instances do not register as open endpoints") {
    auto& disco = OnboardApiDiscovery::instance();
    auto& reg   = NodeRegistry::instance();
    auto before = disco.snapshot().size();
    // "__probe__" is the sentinel id the control plane uses to inspect ports.
    auto probe = reg.create("M_Mount.Service", "__probe__", ::nlohmann::json::object());
    REQUIRE(probe);
    CHECK(disco.snapshot().size() == before);
}
