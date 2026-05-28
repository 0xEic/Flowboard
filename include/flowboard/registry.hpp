// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

/// \file
/// \brief Process-wide node-type registry and self-registration macros.

namespace flowboard {

class Node;

/// \brief Singleton mapping a node type name to its factory and property schema.
///
/// Populated at static-init time via the `OP_REGISTER_NODE` macro family. The
/// loader uses create() to instantiate nodes by name; the control plane uses
/// schema_for() to drive the web UI's property forms. Thread-safe.
class NodeRegistry {
public:
    /// \brief Builds a node from an id and its JSON config.
    using Factory = std::function<std::unique_ptr<Node>(std::string id, nlohmann::json const&)>;

    /// \brief A node type's property-form schema and default values, as JSON text.
    struct NodeSchema {
        std::string schema_json;    ///< JSON Schema for the node's properties.
        std::string defaults_json;  ///< Default property values.
    };

    /// \brief Access the process-wide registry.
    static NodeRegistry& instance();

    /// \brief Register a factory for \p type_name.
    void register_type(std::string type_name, Factory factory);
    /// \brief Instantiate a node by type name.
    /// \throws std::runtime_error if \p type_name is not registered.
    std::unique_ptr<Node> create(std::string_view type_name,
                                  std::string id,
                                  nlohmann::json const& config) const;
    /// \brief All registered type names (backs the `/api/types` catalog).
    std::vector<std::string> registered_names() const;

    /// \brief Register the property schema for \p type_name.
    void register_schema(std::string type_name, NodeSchema schema);
    /// \brief The property schema for \p type_name, if one was registered.
    std::optional<NodeSchema> schema_for(std::string_view type_name) const;

private:
    NodeRegistry() = default;
    mutable std::mutex mu_;
    std::unordered_map<std::string, Factory> factories_;
    std::unordered_map<std::string, NodeSchema> schemas_;
};

/// \brief Static-init helper that registers a node factory on construction.
struct AutoRegister {
    AutoRegister(std::string type_name, NodeRegistry::Factory f) {
        NodeRegistry::instance().register_type(std::move(type_name), std::move(f));
    }
};

/// \brief Static-init helper that registers a node property schema on construction.
struct AutoRegisterSchema {
    AutoRegisterSchema(std::string type_name, char const* schema, char const* defaults) {
        NodeRegistry::instance().register_schema(std::move(type_name),
            NodeRegistry::NodeSchema{schema ? schema : "{}", defaults ? defaults : "{}"});
    }
};

}  // namespace flowboard

/// \brief Register a Node subclass under a string type name.
/// \param TypeNameStr String literal type name (e.g. `"Transform.Threshold"`).
/// \param ClassType   The Node subclass; must accept `(std::string, json const&)`.
#define OP_REGISTER_NODE(TypeNameStr, ClassType)                                       \
    static const ::flowboard::AutoRegister _op_auto_register_##ClassType{           \
        TypeNameStr,                                                                   \
        [](std::string id, nlohmann::json const& cfg)                                  \
            -> std::unique_ptr<::flowboard::Node> {                                 \
            return std::make_unique<ClassType>(std::move(id), cfg);                    \
        }};

/// \brief Like #OP_REGISTER_NODE but also registers a property-form schema.
#define OP_REGISTER_NODE_WITH_SCHEMA(TypeNameStr, ClassType, SchemaJson, DefaultsJson) \
    OP_REGISTER_NODE(TypeNameStr, ClassType)                                            \
    static const ::flowboard::AutoRegisterSchema                                     \
        _op_auto_register_schema_##ClassType{TypeNameStr, SchemaJson, DefaultsJson};

/// \brief Register a factory lambda (not a class) plus a property-form schema.
/// \param UniqueTag A token making the generated static names unique.
#define OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(TypeNameStr, FactoryLambda, SchemaJson, DefaultsJson, UniqueTag) \
    static const ::flowboard::AutoRegister                                                                \
        _op_auto_register_lambda_##UniqueTag{TypeNameStr, FactoryLambda};                                    \
    static const ::flowboard::AutoRegisterSchema                                                          \
        _op_auto_register_schema_lambda_##UniqueTag{TypeNameStr, SchemaJson, DefaultsJson};
