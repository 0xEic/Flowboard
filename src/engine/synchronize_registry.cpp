// SPDX-License-Identifier: MIT
#include "flowboard/synchronize_registry.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

namespace flowboard {

namespace {
std::unordered_map<std::string, SynchronizeFactoryFn>& table() {
    static std::unordered_map<std::string, SynchronizeFactoryFn> t;
    return t;
}

std::mutex& mu() {
    static std::mutex m;
    return m;
}
}  // namespace

void register_synchronize_factory(std::string type_name, SynchronizeFactoryFn fn) {
    std::scoped_lock lock(mu());
    table()[std::move(type_name)] = fn;
}

SynchronizeFactoryFn lookup_synchronize_factory(std::string_view type_name) {
    std::scoped_lock lock(mu());
    auto& t = table();
    auto it = t.find(std::string(type_name));
    if (it == t.end()) return nullptr;
    return it->second;
}

std::vector<std::string> registered_synchronize_types() {
    std::scoped_lock lock(mu());
    std::vector<std::string> out;
    out.reserve(table().size());
    for (auto const& [k, _] : table()) out.push_back(k);
    return out;
}

}  // namespace flowboard
