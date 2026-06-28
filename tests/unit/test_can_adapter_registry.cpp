// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/can_adapter.hpp"

#include <algorithm>

using namespace flowboard;

namespace {
class StubAdapter : public ICanAdapter {
public:
    explicit StubAdapter(std::string n) : name_(std::move(n)) {}
    std::string_view name() const override { return name_; }
    std::unique_ptr<ICanBus> open(CanBusConfig const&, CanRxCallback) override { return nullptr; }
private:
    std::string name_;
};
}

TEST_CASE("CanAdapter registry: register / lookup / unknown") {
    register_can_adapter(std::make_unique<StubAdapter>("test_stub_1"));
    auto* a = find_can_adapter("test_stub_1");
    REQUIRE(a != nullptr);
    CHECK(a->name() == "test_stub_1");
    CHECK(find_can_adapter("does_not_exist") == nullptr);
}

TEST_CASE("CanAdapter registry: last-writer-wins on name conflict") {
    register_can_adapter(std::make_unique<StubAdapter>("conflict"));
    auto* first = find_can_adapter("conflict");
    REQUIRE(first != nullptr);
    register_can_adapter(std::make_unique<StubAdapter>("conflict"));
    auto* second = find_can_adapter("conflict");
    REQUIRE(second != nullptr);
    CHECK(second != first);  // different instance
}

TEST_CASE("CanAdapter registry: registered_adapter_names enumerates") {
    register_can_adapter(std::make_unique<StubAdapter>("enum_test"));
    auto names = registered_adapter_names();
    CHECK(std::find(names.begin(), names.end(), std::string("enum_test")) != names.end());
}
