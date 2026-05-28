// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/extract_registry.hpp"
#include "flowboard/extract_nodes.hpp"
#include "builtin_types.hpp"

namespace {

struct Stub {
    double v;
};
inline ::nlohmann::json to_json(Stub const& s) {
    return ::nlohmann::json{{"v", s.v}};
}

std::unique_ptr<flowboard::Node> make_extract_test__Stub(std::string id, std::string field) {
    if (field == "v")
        return std::make_unique<flowboard::ExtractScalar<Stub, double>>(std::move(id), "v");
    return nullptr;
}

}  // anonymous

OP_DECLARE_TYPE(::Stub, "test::Stub")
OP_REGISTER_EXTRACT_FACTORY("test::Stub", &make_extract_test__Stub)

TEST_CASE("Transform.Extract: factory dispatches to registered struct factory") {
    auto& reg = flowboard::NodeRegistry::instance();
    auto node = reg.create("Transform.Extract", "ex1", {
        {"inputType", "test::Stub"},
        {"field",     "v"}
    });
    REQUIRE(node != nullptr);
    CHECK(node->output("out") != nullptr);
}

TEST_CASE("Transform.Extract: unknown inputType throws") {
    auto& reg = flowboard::NodeRegistry::instance();
    CHECK_THROWS(reg.create("Transform.Extract", "ex2", {
        {"inputType", "does::not::Exist"},
        {"field",     "v"}
    }));
}

TEST_CASE("Transform.Extract: unknown field for known inputType throws") {
    auto& reg = flowboard::NodeRegistry::instance();
    CHECK_THROWS(reg.create("Transform.Extract", "ex3", {
        {"inputType", "test::Stub"},
        {"field",     "no_such_field"}
    }));
}

TEST_CASE("real OnboardAPI struct has a registered extract factory") {
    auto fn = flowboard::lookup_extract_factory("M_Mount::MountPositionType");
    REQUIRE(fn != nullptr);
    auto node = fn("real1", "Elevation");
    REQUIRE(node != nullptr);
    CHECK(node->output("value")    != nullptr);
    CHECK(node->output("isFilled") != nullptr);
}
