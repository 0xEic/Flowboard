// SPDX-License-Identifier: MIT
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"

namespace flowboard {

NodeRegistry& NodeRegistry::instance() {
    static NodeRegistry r;
    return r;
}

void NodeRegistry::register_type(std::string type_name, Factory factory) {
    std::scoped_lock lock(mu_);
    factories_.insert_or_assign(std::move(type_name), std::move(factory));
}

std::unique_ptr<Node> NodeRegistry::create(std::string_view type_name,
                                            std::string id,
                                            nlohmann::json const& config) const {
    std::scoped_lock lock(mu_);
    auto it = factories_.find(std::string(type_name));
    if (it == factories_.end()) return nullptr;
    return it->second(std::move(id), config);
}

std::vector<std::string> NodeRegistry::registered_names() const {
    std::scoped_lock lock(mu_);
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (auto const& [k, _] : factories_) names.push_back(k);
    return names;
}

void NodeRegistry::register_schema(std::string type_name, NodeRegistry::NodeSchema schema) {
    std::scoped_lock lock(mu_);
    schemas_.insert_or_assign(std::move(type_name), std::move(schema));
}

std::optional<NodeRegistry::NodeSchema> NodeRegistry::schema_for(std::string_view type_name) const {
    std::scoped_lock lock(mu_);
    auto it = schemas_.find(std::string(type_name));
    if (it == schemas_.end()) return std::nullopt;
    return it->second;
}

}  // namespace flowboard
