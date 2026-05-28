// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/edge.hpp"
#include "builtin_types.hpp"
#include <memory>
#include <semaphore>

using flowboard::Edge;
using flowboard::BackpressurePolicy;

TEST_CASE("Edge push-pop preserves order under capacity") {
    Edge<int64_t>::NodeEvent sem{0};
    Edge<int64_t> e{4, BackpressurePolicy::Block, &sem};
    for (int64_t i = 1; i <= 4; ++i) e.push(std::make_shared<const int64_t>(i));
    std::shared_ptr<const int64_t> v;
    for (int64_t i = 1; i <= 4; ++i) {
        REQUIRE(e.try_pop(v));
        CHECK(*v == i);
    }
    CHECK_FALSE(e.try_pop(v));
}

TEST_CASE("Edge drop_oldest policy discards oldest when full") {
    Edge<int64_t>::NodeEvent sem{0};
    Edge<int64_t> e{2, BackpressurePolicy::DropOldest, &sem};
    e.push(std::make_shared<const int64_t>(1));
    e.push(std::make_shared<const int64_t>(2));
    e.push(std::make_shared<const int64_t>(3));  // should drop the 1
    std::shared_ptr<const int64_t> v;
    REQUIRE(e.try_pop(v)); CHECK(*v == 2);
    REQUIRE(e.try_pop(v)); CHECK(*v == 3);
    CHECK_FALSE(e.try_pop(v));
}

TEST_CASE("Edge signals the consumer semaphore on push") {
    Edge<int64_t>::NodeEvent sem{0};
    Edge<int64_t> e{4, BackpressurePolicy::Block, &sem};
    e.push(std::make_shared<const int64_t>(99));
    CHECK(sem.try_acquire());
}
