// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/error_budget.hpp"
#include <chrono>
#include <thread>

using flowboard::ErrorBudget;
using namespace std::chrono_literals;

TEST_CASE("budget below limit returns false") {
    ErrorBudget b{3, 50ms};
    CHECK_FALSE(b.note_failure_and_check_exceeded());
    CHECK_FALSE(b.note_failure_and_check_exceeded());
}

TEST_CASE("budget hits limit at exactly N failures") {
    ErrorBudget b{3, 200ms};
    CHECK_FALSE(b.note_failure_and_check_exceeded());
    CHECK_FALSE(b.note_failure_and_check_exceeded());
    CHECK(b.note_failure_and_check_exceeded());
}

TEST_CASE("budget forgets old failures past the window") {
    ErrorBudget b{2, 30ms};
    b.note_failure();
    std::this_thread::sleep_for(40ms);
    CHECK_FALSE(b.note_failure_and_check_exceeded());
    CHECK(b.note_failure_and_check_exceeded());
}
