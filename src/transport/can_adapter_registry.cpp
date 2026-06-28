// SPDX-License-Identifier: MIT
#include "flowboard/can_adapter.hpp"
#include <mutex>
#include <unordered_map>

namespace flowboard {
namespace {

std::mutex& mu() { static std::mutex m; return m; }

std::unordered_map<std::string, std::unique_ptr<ICanAdapter>>& table() {
    static std::unordered_map<std::string, std::unique_ptr<ICanAdapter>> t;
    return t;
}

}  // namespace

void register_can_adapter(std::unique_ptr<ICanAdapter> adapter) {
    if (!adapter) return;
    std::string key(adapter->name());
    std::scoped_lock lk(mu());
    table().insert_or_assign(std::move(key), std::move(adapter));
}

ICanAdapter* find_can_adapter(std::string_view name) {
    std::scoped_lock lk(mu());
    auto& t = table();
    auto it = t.find(std::string(name));
    return it == t.end() ? nullptr : it->second.get();
}

std::vector<std::string> registered_adapter_names() {
    std::scoped_lock lk(mu());
    auto& t = table();
    std::vector<std::string> out;
    out.reserve(t.size());
    for (auto const& [k, _] : t) out.push_back(k);
    return out;
}

}  // namespace flowboard
