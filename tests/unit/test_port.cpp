// SPDX-License-Identifier: MIT
#include <ostream>  // MSVC: string_view operator<< requires complete ostream before doctest
#include <doctest/doctest.h>
#include "flowboard/port.hpp"
#include "builtin_types.hpp"

TEST_CASE("InputPort reports its name and type tag") {
    flowboard::InputPort<double> in{"value"};
    CHECK(in.name() == "value");
    CHECK(in.type_tag() == "flowboard::Double");
}

TEST_CASE("OutputPort reports its name and type tag") {
    flowboard::OutputPort<bool> out{"trigger"};
    CHECK(out.name() == "trigger");
    CHECK(out.type_tag() == "flowboard::Bool");
}

TEST_CASE("OutputPort emits to attached sinks") {
    flowboard::OutputPort<int64_t> out{"n"};
    int64_t received = 0;
    out.attach_sink([&](std::shared_ptr<const int64_t> v) { received = *v; });
    out.emit(std::make_shared<const int64_t>(42));
    CHECK(received == 42);
}

TEST_CASE("OutputPort fans out to multiple sinks") {
    flowboard::OutputPort<int64_t> out{"n"};
    int64_t a = 0, b = 0;
    out.attach_sink([&](std::shared_ptr<const int64_t> v) { a = *v; });
    out.attach_sink([&](std::shared_ptr<const int64_t> v) { b = *v; });
    out.emit(std::make_shared<const int64_t>(7));
    CHECK(a == 7);
    CHECK(b == 7);
}
