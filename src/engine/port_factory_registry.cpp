// SPDX-License-Identifier: MIT
#include "flowboard/port_factory_registry.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

namespace flowboard {
namespace {

std::mutex& mu() { static std::mutex m; return m; }

std::unordered_map<std::string, PortFactoryEntry>& table() {
    static std::unordered_map<std::string, PortFactoryEntry> t;
    return t;
}

}  // namespace

void register_port_factory(std::string tag, PortFactoryEntry entry) {
    std::scoped_lock lk(mu());
    table().insert_or_assign(std::move(tag), entry);
}

PortFactoryEntry const* lookup_port_factory(std::string_view tag) {
    std::scoped_lock lk(mu());
    auto& t = table();
    auto it = t.find(std::string(tag));
    return it == t.end() ? nullptr : &it->second;
}

}  // namespace flowboard
