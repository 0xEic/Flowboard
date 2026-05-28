// SPDX-License-Identifier: MIT
#include "flowboard/wire_registry.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

namespace flowboard {

namespace {
std::unordered_map<std::string, WireFactoryFn>& table() {
    static std::unordered_map<std::string, WireFactoryFn> t;
    return t;
}
std::mutex& mu() {
    static std::mutex m;
    return m;
}
}  // namespace

void register_wire_factory(std::string type_tag, WireFactoryFn fn) {
    std::scoped_lock lock(mu());
    table()[std::move(type_tag)] = fn;
}

WireFactoryFn lookup_wire_factory(std::string_view type_tag) {
    std::scoped_lock lock(mu());
    auto& t = table();
    auto it = t.find(std::string(type_tag));
    if (it == t.end()) return nullptr;
    return it->second;
}

}  // namespace flowboard
