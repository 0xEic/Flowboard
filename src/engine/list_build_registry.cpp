// SPDX-License-Identifier: MIT
#include "flowboard/list_build_registry.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

namespace flowboard {

namespace {
struct Entry { ListElemFactoryFn build; ListElemFactoryFn accumulate; };
std::unordered_map<std::string, Entry>& table() {
    static std::unordered_map<std::string, Entry> t;
    return t;
}
std::mutex& mu() {
    static std::mutex m;
    return m;
}
}  // namespace

void register_list_elem_factories(std::string element_tag,
                                  ListElemFactoryFn build, ListElemFactoryFn accumulate) {
    std::scoped_lock lock(mu());
    table()[std::move(element_tag)] = Entry{build, accumulate};
}

ListElemFactoryFn lookup_list_build_factory(std::string_view element_tag) {
    std::scoped_lock lock(mu());
    auto it = table().find(std::string(element_tag));
    return it == table().end() ? nullptr : it->second.build;
}

ListElemFactoryFn lookup_list_accumulate_factory(std::string_view element_tag) {
    std::scoped_lock lock(mu());
    auto it = table().find(std::string(element_tag));
    return it == table().end() ? nullptr : it->second.accumulate;
}

}  // namespace flowboard
