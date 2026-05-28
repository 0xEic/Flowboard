// SPDX-License-Identifier: MIT
#include "flowboard/key_value_accumulator_registry.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

namespace flowboard {

namespace {

std::string make_key(std::string_view k, std::string_view v) {
    std::string s;
    s.reserve(k.size() + 1 + v.size());
    s.append(k);
    s.push_back('|');
    s.append(v);
    return s;
}

std::unordered_map<std::string, KeyValueAccumulatorFactoryFn>& table() {
    static std::unordered_map<std::string, KeyValueAccumulatorFactoryFn> t;
    return t;
}

std::mutex& mu() {
    static std::mutex m;
    return m;
}

}  // namespace

void register_key_value_accumulator_factory(
    std::string key_type_tag, std::string value_type_tag,
    KeyValueAccumulatorFactoryFn fn) {
    std::scoped_lock lock(mu());
    table()[make_key(key_type_tag, value_type_tag)] = fn;
}

KeyValueAccumulatorFactoryFn lookup_key_value_accumulator_factory(
    std::string_view key_type_tag, std::string_view value_type_tag) {
    std::scoped_lock lock(mu());
    auto& t = table();
    auto it = t.find(make_key(key_type_tag, value_type_tag));
    if (it == t.end()) return nullptr;
    return it->second;
}

std::vector<KeyValuePair> registered_key_value_pairs() {
    std::scoped_lock lock(mu());
    std::vector<KeyValuePair> out;
    out.reserve(table().size());
    for (auto const& [k, _] : table()) {
        auto sep = k.find('|');
        if (sep == std::string::npos) continue;
        out.push_back({k.substr(0, sep), k.substr(sep + 1)});
    }
    return out;
}

}  // namespace flowboard
