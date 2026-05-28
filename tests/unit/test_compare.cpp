// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace flowboard;

TEST_CASE("Compare emits true when a > b (after both sides seen)") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Compare", "c1", {
        {"inputType", "flowboard::Double"},
        {"op", ">"}
    });
    REQUIRE(node);
    std::atomic<int> last{-1};
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { last.store(*v ? 1 : 0); });
    node->start();
    auto* a = dynamic_cast<InputPort<double>*>(node->input("a"));
    auto* b = dynamic_cast<InputPort<double>*>(node->input("b"));
    REQUIRE(a); REQUIRE(b);
    a->deliver(std::make_shared<const double>(3.0));
    // Only one side seen, no emission yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(last.load() == -1);
    b->deliver(std::make_shared<const double>(2.0));
    for (int i = 0; i < 100 && last.load() == -1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(last.load() == 1);
    node->stop();
}

TEST_CASE("Compare Int32 == matches the new primitive types") {
    auto node = NodeRegistry::instance().create("Transform.Compare", "ci", {
        {"inputType", "flowboard::Int32"}, {"op", "=="}
    });
    REQUIRE(node);
    std::atomic<int> last{-1};
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { last.store(*v ? 1 : 0); });
    node->start();
    auto* a = dynamic_cast<InputPort<std::int32_t>*>(node->input("a"));
    auto* b = dynamic_cast<InputPort<std::int32_t>*>(node->input("b"));
    REQUIRE(a); REQUIRE(b);
    a->deliver(std::make_shared<const std::int32_t>(5));
    b->deliver(std::make_shared<const std::int32_t>(5));
    for (int i = 0; i < 100 && last.load() == -1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(last.load() == 1);
    node->stop();
}

namespace {
int run_str_compare(std::string op, std::string a_val, std::string b_val) {
    auto node = NodeRegistry::instance().create("Transform.Compare", "cs", {
        {"inputType", "flowboard::String"}, {"op", op}
    });
    REQUIRE(node);
    std::atomic<int> last{-1};
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { last.store(*v ? 1 : 0); });
    node->start();
    auto* a = dynamic_cast<InputPort<std::string>*>(node->input("a"));
    auto* b = dynamic_cast<InputPort<std::string>*>(node->input("b"));
    REQUIRE(a); REQUIRE(b);
    a->deliver(std::make_shared<const std::string>(std::move(a_val)));
    b->deliver(std::make_shared<const std::string>(std::move(b_val)));
    for (int i = 0; i < 100 && last.load() == -1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    int r = last.load();
    node->stop();
    return r;
}
}  // namespace

TEST_CASE("Compare String supports typical string operations") {
    CHECK(run_str_compare("equals",     "abc", "abc") == 1);
    CHECK(run_str_compare("equals",     "abc", "abd") == 0);
    CHECK(run_str_compare("notEquals",  "abc", "abd") == 1);
    CHECK(run_str_compare("contains",   "hello world", "lo w") == 1);
    CHECK(run_str_compare("contains",   "hello world", "xyz") == 0);
    CHECK(run_str_compare("startsWith", "filename.txt", "file") == 1);
    CHECK(run_str_compare("startsWith", "filename.txt", "name") == 0);
    CHECK(run_str_compare("endsWith",   "filename.txt", ".txt") == 1);
    CHECK(run_str_compare("endsWith",   "filename.txt", ".csv") == 0);
}
