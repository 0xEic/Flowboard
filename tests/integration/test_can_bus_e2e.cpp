// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/can_frame.hpp"
#include "flowboard/can_adapter.hpp"
#include "flowboard/plugin_host.hpp"
#include "flowboard/builtin_types.hpp"   // OutputPort<bool> needs the bool type tag

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

using namespace flowboard;

TEST_CASE("Can.Bus loops frames through virtual adapter") {
    auto& reg = NodeRegistry::instance();
    auto a = reg.create("Can.Bus", "a",
        {{"adapter", "virtual:loop_e2e"}, {"bitrate", 500000}});
    auto b = reg.create("Can.Bus", "b",
        {{"adapter", "virtual:loop_e2e"}, {"bitrate", 500000}});
    REQUIRE(a);
    REQUIRE(b);

    std::atomic<int> seen{0};
    auto* b_rx = dynamic_cast<OutputPort<CanFrame>*>(b->output("rx"));
    REQUIRE(b_rx);
    b_rx->attach_sink([&seen](std::shared_ptr<const CanFrame>){ seen.fetch_add(1); });

    a->start();
    b->start();
    // Wait for both buses to come up (Can.Bus opens via a worker thread).
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto* a_tx = dynamic_cast<InputPort<CanFrame>*>(a->input("tx"));
    REQUIRE(a_tx);
    CanFrame f; f.id = 0x321; f.dlc = 3; f.data = {1, 2, 3};
    a_tx->deliver(std::make_shared<const CanFrame>(f));

    for (int i = 0; i < 100 && seen.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    CHECK(seen.load() >= 1);
    b->stop();
    a->stop();
}

TEST_CASE("Can.Bus listenOnly drops tx and reports sent=false") {
    auto& reg = NodeRegistry::instance();
    auto listener = reg.create("Can.Bus", "lo",
        {{"adapter", "virtual:listenonly_test"}, {"bitrate", 500000},
         {"listenOnly", true}});
    auto peer = reg.create("Can.Bus", "peer",
        {{"adapter", "virtual:listenonly_test"}, {"bitrate", 500000}});
    REQUIRE(listener);
    REQUIRE(peer);

    std::atomic<int> peer_rx{0};
    auto* peer_rx_port = dynamic_cast<OutputPort<CanFrame>*>(peer->output("rx"));
    REQUIRE(peer_rx_port);
    peer_rx_port->attach_sink([&peer_rx](std::shared_ptr<const CanFrame>){ peer_rx.fetch_add(1); });

    std::atomic<int> sent_falses{0};
    std::atomic<int> sent_trues{0};
    auto* lo_sent = dynamic_cast<OutputPort<bool>*>(listener->output("sent"));
    REQUIRE(lo_sent);
    lo_sent->attach_sink([&](std::shared_ptr<const bool> v){
        if (v && *v) sent_trues.fetch_add(1); else sent_falses.fetch_add(1);
    });

    listener->start();
    peer->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto* lo_tx = dynamic_cast<InputPort<CanFrame>*>(listener->input("tx"));
    REQUIRE(lo_tx);
    CanFrame f; f.id = 0x555; f.dlc = 1; f.data = {0xAB};
    lo_tx->deliver(std::make_shared<const CanFrame>(f));

    // Drop happens on the node worker; wait briefly for it to propagate.
    for (int i = 0; i < 50 && sent_falses.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    CHECK(sent_falses.load() == 1);   // drop reported
    CHECK(sent_trues.load() == 0);    // no successful send
    CHECK(peer_rx.load() == 0);       // peer never saw the frame

    peer->stop();
    listener->stop();
}

TEST_CASE("FakeVirtualVendor plugin: multi-client echo on a shared channel") {
    // The fake_can_vendor plugin is built into <exe>/plugins/ alongside
    // hello_plugin (the auto-loaded dir). Test binary doesn't auto-load, so
    // we point PluginHost at the dir explicitly.
    namespace fs = std::filesystem;
    auto plugins_dir = fs::weakly_canonical(
        fs::path("build-stub") / "src" / "Release" / "plugins");
    if (!fs::exists(plugins_dir)) {
        plugins_dir = fs::weakly_canonical(fs::path("plugins"));
        if (!fs::exists(plugins_dir)) {
            MESSAGE("skipping: plugins dir not found");
            return;
        }
    }
    PluginHost host;
    host.load_directory(plugins_dir);
    REQUIRE(find_can_adapter("fakevendor") != nullptr);

    auto& reg = NodeRegistry::instance();
    auto sender = reg.create("Can.Bus", "fv_sender",
        {{"adapter", "fakevendor:test_chan"}, {"bitrate", 500000}});
    auto peer = reg.create("Can.Bus", "fv_peer",
        {{"adapter", "fakevendor:test_chan"}, {"bitrate", 500000}});
    REQUIRE(sender);
    REQUIRE(peer);

    std::atomic<int> sender_rx{0}, peer_rx{0};
    auto* sender_rx_port = dynamic_cast<OutputPort<CanFrame>*>(sender->output("rx"));
    auto* peer_rx_port   = dynamic_cast<OutputPort<CanFrame>*>(peer->output("rx"));
    REQUIRE(sender_rx_port);
    REQUIRE(peer_rx_port);
    sender_rx_port->attach_sink([&sender_rx](std::shared_ptr<const CanFrame>){ sender_rx.fetch_add(1); });
    peer_rx_port  ->attach_sink([&peer_rx  ](std::shared_ptr<const CanFrame>){ peer_rx.fetch_add(1); });

    sender->start();
    peer->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto* sender_tx = dynamic_cast<InputPort<CanFrame>*>(sender->input("tx"));
    REQUIRE(sender_tx);
    CanFrame f; f.id = 0x456; f.dlc = 2; f.data = {0xCA, 0xFE};
    sender_tx->deliver(std::make_shared<const CanFrame>(f));

    for (int i = 0; i < 100 && (sender_rx.load() == 0 || peer_rx.load() == 0); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    CHECK(sender_rx.load() >= 1);   // echo back to sender (loopback semantics)
    CHECK(peer_rx.load() >= 1);     // peer on same channel also sees it

    peer->stop();
    sender->stop();
}

TEST_CASE("PluginHost loads echo_can_adapter and registers 'echo'") {
    // The build places the echo plugin next to the flowboard exe under
    // test_plugins/. Path is relative to the test binary's location.
    namespace fs = std::filesystem;
    // cmake runs tests with WORKING_DIRECTORY = CMAKE_SOURCE_DIR (project root),
    // so this project-root-relative path is the primary heuristic.
    auto test_bin_dir = fs::path("build-stub") / "src" / "Release" / "test_plugins";
    test_bin_dir = fs::weakly_canonical(test_bin_dir);
    if (!fs::exists(test_bin_dir)) {
        // Heuristic: try relative to CWD too (some test runners cd elsewhere).
        test_bin_dir = fs::weakly_canonical(fs::path("test_plugins"));
        if (!fs::exists(test_bin_dir)) {
            MESSAGE("skipping: plugins dir not found");
            return;
        }
    }
    PluginHost host;
    int n = host.load_directory(test_bin_dir);
    CHECK(n >= 1);
    CHECK(find_can_adapter("echo") != nullptr);
}
