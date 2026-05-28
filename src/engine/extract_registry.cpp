// SPDX-License-Identifier: MIT
#include "flowboard/extract_registry.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

namespace flowboard {

namespace {
std::unordered_map<std::string, ExtractFactoryFn>& table() {
    static std::unordered_map<std::string, ExtractFactoryFn> t;
    return t;
}

std::mutex& mu() {
    static std::mutex m;
    return m;
}
}

void register_extract_factory(std::string type_name, ExtractFactoryFn fn) {
    std::scoped_lock lock(mu());
    table()[std::move(type_name)] = fn;
}

ExtractFactoryFn lookup_extract_factory(std::string_view type_name) {
    std::scoped_lock lock(mu());
    auto& t = table();
    auto it = t.find(std::string(type_name));
    if (it == t.end()) return nullptr;
    return it->second;
}

}  // namespace flowboard
