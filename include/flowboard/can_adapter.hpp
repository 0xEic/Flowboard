// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "flowboard/can_frame.hpp"

/// \file
/// \brief Adapter / bus abstraction for CAN transport. In-tree backends
///        (SocketCAN, Virtual) register through this header; vendor adapters
///        ship as plugins via the plugin SDK's
///        `flowboard_plugin_register_can_adapter` entry point.

namespace flowboard {

/// \brief Per-bus configuration handed to ICanAdapter::open().
struct CanBusConfig {
    std::string   bus;           ///< Adapter-specific bus id (e.g. "can0", "0", USB serial).
    std::uint32_t bitrate     = 500'000;
    bool          listen_only = false;
    bool          fd          = false;     ///< Ignored by classical-only v1 backends.
    std::uint32_t fd_bitrate  = 0;
};

/// \brief Callbacks the adapter invokes from its own thread(s).
struct CanRxCallback {
    std::function<void(CanFrame const&)>      on_frame;   ///< One per RX frame.
    std::function<void(std::string_view msg)> on_error;   ///< Bus-off, restart, device removed, …
};

/// \brief Live bus handle returned by ICanAdapter::open(). Owns the OS resource;
///        the destructor must close it cleanly.
class ICanBus {
public:
    virtual ~ICanBus() = default;
    /// \brief Send a single frame on this bus. Returns false on transient failure
    ///        (the engine logs + emits `sent=false`); must NOT throw.
    virtual bool send(CanFrame const& frame) = 0;
};

/// \brief A named CAN backend (e.g. "socketcan", "virtual", "peak"). Registered
///        into the process-wide registry once and consulted by `Can.Bus`.
class ICanAdapter {
public:
    virtual ~ICanAdapter() = default;
    /// \brief Stable name appearing as the prefix in a node's `adapter` config
    ///        (e.g. "socketcan" in "socketcan:can0").
    virtual std::string_view name() const = 0;
    /// \brief Open a bus. \p cb is invoked from any thread; the engine
    ///        immediately re-queues onto the node worker. Returns nullptr on
    ///        failure (the engine reports + reconnects).
    virtual std::unique_ptr<ICanBus> open(CanBusConfig const& cfg,
                                          CanRxCallback cb) = 0;
};

// --- Process-wide registry ------------------------------------------------

/// \brief Register an adapter under its `name()`. Last writer wins.
void register_can_adapter(std::unique_ptr<ICanAdapter> adapter);

/// \brief Look up an adapter by name. Returns nullptr if not registered.
ICanAdapter* find_can_adapter(std::string_view name);

/// \brief All adapter names currently registered, in unspecified order.
std::vector<std::string> registered_adapter_names();

/// \brief Abstract registrar passed to plugin DLLs via
///        `flowboard_plugin_register_can_adapter(CanAdapterRegistry&)`. The host
///        defines the concrete impl; plugin calls `.add(...)`, which
///        vtable-dispatches into the host's process-wide registry, sidestepping
///        the cross-DLL static-singleton duplication problem (each DLL has its
///        own copy of free-function statics; vtable calls don't).
class CanAdapterRegistry {
public:
    virtual ~CanAdapterRegistry() = default;
    virtual void add(std::unique_ptr<ICanAdapter> adapter) = 0;
};

}  // namespace flowboard
