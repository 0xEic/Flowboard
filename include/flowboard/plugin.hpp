// SPDX-License-Identifier: MIT
#pragma once

/// \file
/// \brief Public SDK header for writing Flowboard node plugins.
///
/// A plugin is a shared library (`.dll` / `.so` / `.dylib`) dropped into the
/// host's plugin folder. At startup the host loads each library, checks its ABI
/// version, and calls its registration entry point, which adds the plugin's node
/// types to the process-wide NodeRegistry — after which they behave exactly like
/// built-in nodes (palette, property forms, live values, JSON graphs).
///
/// A plugin must export two C entry points (see the macros below):
///   - `int  flowboard_plugin_abi_version(void)` — return FLOWBOARD_PLUGIN_ABI_VERSION.
///   - `void flowboard_plugin_register(flowboard::NodeRegistry&)` — register nodes.
/// It may optionally export `const char* flowboard_plugin_name(void)`.
///
/// See docs/PLUGINS.md for the full guide and build/ABI requirements.

#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/builtin_types.hpp"

/// \brief Plugin ABI revision. The host refuses to load a plugin whose
/// flowboard_plugin_abi_version() does not match this exact value. Bump it
/// whenever the host/plugin binary contract changes incompatibly.
#define FLOWBOARD_PLUGIN_ABI_VERSION 2

/// \brief Marks a symbol for export from the plugin shared library.
#if defined(_WIN32)
#  define FLOWBOARD_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define FLOWBOARD_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/// \brief Emit the boilerplate plugin entry points (ABI version + name).
///
/// Place once at file scope in your plugin. You still provide the registration
/// function yourself:
/// \code
///   FLOWBOARD_DECLARE_PLUGIN("My Cool Nodes")
///
///   extern "C" FLOWBOARD_PLUGIN_EXPORT
///   void flowboard_plugin_register(flowboard::NodeRegistry& reg) {
///       OP_REGISTER_NODE_INTO(reg, "Plugin.Foo", FooNode);
///   }
/// \endcode
/// \param NameStr A human-readable plugin name (string literal), used in logs.
#define FLOWBOARD_DECLARE_PLUGIN(NameStr)                                       \
    extern "C" FLOWBOARD_PLUGIN_EXPORT int flowboard_plugin_abi_version(void) { \
        return FLOWBOARD_PLUGIN_ABI_VERSION;                                    \
    }                                                                          \
    extern "C" FLOWBOARD_PLUGIN_EXPORT char const* flowboard_plugin_name(void) {\
        return (NameStr);                                                       \
    }

/// \brief Register a Node subclass into an explicit registry.
///
/// Like #OP_REGISTER_NODE, but targets the registry the host hands to
/// flowboard_plugin_register() instead of the static singleton. \p ClassType
/// must be constructible from `(std::string id, nlohmann::json const& config)`.
#define OP_REGISTER_NODE_INTO(Registry, TypeNameStr, ClassType)                 \
    (Registry).register_type((TypeNameStr),                                     \
        [](std::string id, ::nlohmann::json const& cfg)                         \
            -> std::unique_ptr<::flowboard::Node> {                             \
            return std::make_unique<ClassType>(std::move(id), cfg);             \
        })

/// \brief Like #OP_REGISTER_NODE_INTO but also registers a property-form schema
/// (JSON Schema + defaults, as string literals) for the node.
#define OP_REGISTER_NODE_INTO_WITH_SCHEMA(Registry, TypeNameStr, ClassType, SchemaJson, DefaultsJson) \
    do {                                                                        \
        OP_REGISTER_NODE_INTO(Registry, TypeNameStr, ClassType);               \
        (Registry).register_schema((TypeNameStr),                              \
            ::flowboard::NodeRegistry::NodeSchema{                              \
                (SchemaJson) ? (SchemaJson) : "{}",                            \
                (DefaultsJson) ? (DefaultsJson) : "{}"});                      \
    } while (0)

// --- CanAdapter plugin entry point (ABI 2+) -------------------------------

/// \brief Optional plugin entry point. If exported by a plugin DLL, the host
///        calls this after flowboard_plugin_register so the plugin can register
///        one or more `ICanAdapter` implementations into the process-wide
///        adapter registry (see `flowboard/can_adapter.hpp`).
///
/// Example:
/// \code
///   #include "flowboard/can_adapter.hpp"
///   class MyAdapter : public ::flowboard::ICanAdapter { ... };
///
///   extern "C" FLOWBOARD_PLUGIN_EXPORT
///   void flowboard_plugin_register_can_adapter(
///           ::flowboard::CanAdapterRegistry& reg) {
///       reg.add(std::make_unique<MyAdapter>());
///   }
/// \endcode
///
/// The host passes a `flowboard::CanAdapterRegistry&` (defined in
/// `flowboard/can_adapter.hpp`) and the plugin calls `reg.add(...)`. Using a
/// vtable rather than a free function avoids the cross-DLL singleton
/// duplication that would happen if each plugin DLL linked its own copy of the
/// registry's static storage.
///
/// Convenience wrapper:
#define FLOWBOARD_REGISTER_CAN_ADAPTER(AdapterClass)                            \
    extern "C" FLOWBOARD_PLUGIN_EXPORT                                          \
    void flowboard_plugin_register_can_adapter(                                 \
            ::flowboard::CanAdapterRegistry& reg) {                             \
        reg.add(std::make_unique<AdapterClass>());                              \
    }
