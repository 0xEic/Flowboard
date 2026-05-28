// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace flowboard;

TEST_CASE("Sinks.Log prints values it receives") {
    std::ostringstream os;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(os);
    auto logger = std::make_shared<spdlog::logger>("test", sink);
    spdlog::set_default_logger(logger);

    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Sinks.Log", "lg", {
        {"inputType", "flowboard::Double"},
        {"prefix", "altitude"}
    });
    REQUIRE(node);
    node->start();
    auto* in = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const double>(123.45));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    node->stop();
    logger->flush();
    CHECK(os.str().find("altitude") != std::string::npos);
    CHECK(os.str().find("123.45") != std::string::npos);
}

TEST_CASE("Sinks.Log with a configured path appends to that file") {
    auto path = std::filesystem::temp_directory_path() / "flowboard_logsink_test.log";
    std::error_code ec;
    std::filesystem::remove(path, ec);  // best-effort pre-clean

    // Scope the node so its ofstream closes before we read/remove the file —
    // on Windows you can't delete a file that another handle still has open.
    {
        auto& reg = NodeRegistry::instance();
        auto node = reg.create("Sinks.Log", "fl", {
            {"inputType", "flowboard::Int64"},
            {"prefix",    "n"},
            {"path",      path.string()},
        });
        REQUIRE(node);
        node->start();
        auto* in = dynamic_cast<InputPort<int64_t>*>(node->input("in"));
        REQUIRE(in);
        in->deliver(std::make_shared<const int64_t>(42));
        in->deliver(std::make_shared<const int64_t>(43));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        node->stop();
    }

    std::ifstream f(path);
    REQUIRE(f.good());
    std::stringstream buf; buf << f.rdbuf();
    auto content = buf.str();
    CHECK(content.find("fl: n = 42") != std::string::npos);
    CHECK(content.find("fl: n = 43") != std::string::npos);

    std::filesystem::remove(path, ec);  // best-effort post-clean
}
