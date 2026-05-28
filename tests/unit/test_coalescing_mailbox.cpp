// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <algorithm>
#include <string>
#include <vector>
#include "flowboard/coalescing_mailbox.hpp"

using namespace flowboard;

TEST_CASE("CoalescingMailbox keeps only the latest action per key") {
    CoalescingMailbox mb;
    std::vector<std::string> ran;

    CHECK(mb.put("a", [&] { ran.push_back("a=1"); }) == true);
    CHECK(mb.put("a", [&] { ran.push_back("a=2"); }) == false);
    CHECK(mb.put("b", [&] { ran.push_back("b=1"); }) == false);

    mb.drain();
    CHECK(ran.size() == 2);
    CHECK(std::count(ran.begin(), ran.end(), std::string("a=2")) == 1);
    CHECK(std::count(ran.begin(), ran.end(), std::string("a=1")) == 0);
    CHECK(std::count(ran.begin(), ran.end(), std::string("b=1")) == 1);

    ran.clear();
    CHECK(mb.put("a", [&] { ran.push_back("a=3"); }) == true);
    mb.drain();
    CHECK(ran == std::vector<std::string>{"a=3"});
}

TEST_CASE("CoalescingMailbox drain on empty is a no-op") {
    CoalescingMailbox mb;
    mb.drain();
    CHECK(true);
}
