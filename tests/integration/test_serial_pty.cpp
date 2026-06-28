// SPDX-License-Identifier: MIT
#if defined(__linux__)

#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/list_value.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <thread>
#include <unistd.h>

using namespace flowboard;

TEST_CASE("Serial.Port pumps bytes through a PTY pair") {
    int master_fd = -1, slave_fd = -1;
    char slave_name[256];
    REQUIRE(openpty(&master_fd, &slave_fd, slave_name, nullptr, nullptr) == 0);

    auto n = NodeRegistry::instance().create("Serial.Port", "s1",
        {{"port", std::string(slave_name)}, {"baudRate", 115200},
         {"readChunkSize", 64}, {"readTimeoutMs", 50}});
    REQUIRE(n);

    std::atomic<int> total_rx{0};
    auto* rx = dynamic_cast<OutputPort<ListValue>*>(n->output("rx"));
    REQUIRE(rx);
    rx->attach_sink([&](auto v){ total_rx.fetch_add(static_cast<int>(v->items.size())); });

    n->start();
    // Wait for opener to install the port.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    char msg[] = "hello-pty";
    ssize_t w = ::write(master_fd, msg, sizeof(msg) - 1);
    REQUIRE(w == sizeof(msg) - 1);

    for (int i = 0; i < 50 && total_rx.load() < 9; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    CHECK(total_rx.load() == 9);

    n->stop();
    ::close(master_fd);
    ::close(slave_fd);
}

#endif  // __linux__
