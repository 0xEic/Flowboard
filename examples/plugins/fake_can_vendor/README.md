# FakeVirtualVendor

A *fake* vendor CAN-adapter plugin — registers the adapter name `fakevendor`
into Flowboard's process-wide CAN adapter registry, but never talks to real
hardware. Every frame sent on a bus is echoed back to that bus's own RX
callback, and any other `Can.Bus` opened on the same channel sees the same
traffic. Behaves like a real CAN bus with loopback enabled.

## Why this is in `examples/`

Two purposes:

1. **Integration substrate.** Flowboard's CAN subsystem can be exercised
   end-to-end — DLL load, the ABI 2 plugin entry point, the registrar dispatch,
   `Can.Bus` open / send / receive, multi-client fan-out — without a CAN dongle
   or kernel CAN module. The in-tree `Virtual` adapter does the same work but
   isn't shaped like a plugin; this one is.

2. **Template for real vendor plugins.** PEAK PCAN-Basic, Kvaser CANlib, Vector
   XL, IXXAT VCI all follow the same layout: an `ICanAdapter` subclass returning
   `ICanBus` instances, registered via `FLOWBOARD_REGISTER_CAN_ADAPTER` from the
   plugin SDK. Copy this directory, rename, and replace the body of
   `FakeVendorBus::send()` plus the constructor with calls into the vendor's
   SDK. The plugin SDK contract is unchanged.

## Using it

The build drops `fake_can_vendor.{dll,so,dylib}` next to the `flowboard`
executable under `plugins/`, which is the default auto-load path. A fresh
`flowboard` will pick it up at startup; the log shows:

    plugins: loaded 'Fake Virtual Vendor' from fake_can_vendor.dll (0 node types)

After that any `Can.Bus` node configured with `"adapter": "fakevendor:<name>"`
opens a virtual channel. Frames sent on the channel echo back to the sender
and fan out to every other client on the same channel name.

Example graph fragment:

```json
{
  "id": "bus_a", "type": "Can.Bus",
  "config": { "adapter": "fakevendor:engine", "bitrate": 500000 }
}
```

## Writing your own vendor plugin

The whole contract is roughly 80 lines. The key pieces:

```cpp
#include "flowboard/plugin.hpp"
#include "flowboard/can_adapter.hpp"

class MyVendorBus : public flowboard::ICanBus {
public:
    MyVendorBus(/* vendor handle */ ..., flowboard::CanRxCallback cb)
        : cb_(std::move(cb)) {
        // Spawn a vendor-RX thread that calls cb_.on_frame(...) per frame.
    }
    ~MyVendorBus() override { /* shut down vendor handle + RX thread */ }

    bool send(flowboard::CanFrame const& f) override {
        // Convert f to vendor's frame type, call vendor's send.
        return /* success */;
    }
private:
    flowboard::CanRxCallback cb_;
};

class MyVendorAdapter : public flowboard::ICanAdapter {
public:
    std::string_view name() const override { return "myvendor"; }
    std::unique_ptr<flowboard::ICanBus> open(
            flowboard::CanBusConfig const& cfg, flowboard::CanRxCallback cb) override {
        // Open the vendor channel cfg.bus at cfg.bitrate, return MyVendorBus.
    }
};

FLOWBOARD_DECLARE_PLUGIN("My Vendor CAN")
extern "C" FLOWBOARD_PLUGIN_EXPORT
void flowboard_plugin_register(flowboard::NodeRegistry&) {}
FLOWBOARD_REGISTER_CAN_ADAPTER(MyVendorAdapter)
```

The CMake side is the four `set_target_properties` lines you see in
`CMakeLists.txt`. Link only `flowboard_plugin_sdk` — never `flowboard_engine`
— so you stay free of the OnboardAPI dependency.

## Caveats

- Channels persist in a process-wide map for the lifetime of the process. Fine
  for typical fixed channel names; if you spin up many uniquely-named channels
  in a long-running process the map will grow unbounded. Same behavior as the
  in-tree Virtual adapter.
- Echo-to-sender is deliberate so tests can see frames arrive on the same
  `Can.Bus` that sent them. A real vendor plugin usually has this as a
  configurable controller option.
