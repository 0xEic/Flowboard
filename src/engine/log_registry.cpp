// SPDX-License-Identifier: MIT
#include "flowboard/log_registry.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

namespace flowboard {

namespace {
std::unordered_map<std::string, LogFactoryFn>& table() {
    static std::unordered_map<std::string, LogFactoryFn> t;
    return t;
}

std::mutex& mu() {
    static std::mutex m;
    return m;
}
}

void register_log_factory(std::string type_name, LogFactoryFn fn) {
    std::scoped_lock lock(mu());
    table()[std::move(type_name)] = fn;
}

LogFactoryFn lookup_log_factory(std::string_view type_name) {
    std::scoped_lock lock(mu());
    auto& t = table();
    auto it = t.find(std::string(type_name));
    if (it == t.end()) return nullptr;
    return it->second;
}

std::vector<std::string> registered_log_types() {
    std::scoped_lock lock(mu());
    std::vector<std::string> out;
    out.reserve(table().size());
    for (auto const& [k, _] : table()) out.push_back(k);
    return out;
}

}  // namespace flowboard
