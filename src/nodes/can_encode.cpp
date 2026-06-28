// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/can_frame.hpp"
#include "builtin_types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace flowboard::nodes {
namespace {

class CanEncodeNode final : public Node {
public:
    CanEncodeNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Can.Encode"),
          auto_(cfg.value("autoTriggerOnNewInput", true)) {
        register_input(&id_);
        register_input(&data_);
        register_input(&ext_);
        register_input(&trig_);
        register_output(&out_);
    }

    void on_start() override {
        id_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v) last_id_ = *v; if (auto_) emit_frame(); });
        });
        data_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v) last_data_ = v; if (auto_) emit_frame(); });
        });
        ext_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v) last_ext_ = *v; if (auto_) emit_frame(); });
        });
        trig_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v && *v) emit_frame(); });
        });
    }

private:
    void emit_frame() {
        CanFrame f;
        f.id = last_id_;
        f.is_extended = last_ext_;
        if (last_data_) {
            f.data.reserve(last_data_->items.size());
            for (auto const& e : last_data_->items)
                f.data.push_back(static_cast<std::uint8_t>(e.is_number() ? (e.get<long long>() & 0xFF) : 0));
        }
        f.dlc = static_cast<std::uint8_t>(f.data.size());
        out_.emit(std::make_shared<const CanFrame>(std::move(f)));
    }

    bool auto_;
    std::uint32_t last_id_{0};
    bool last_ext_{false};
    std::shared_ptr<const ListValue> last_data_{};
    InputPort<std::uint32_t> id_{"id"};
    InputPort<ListValue>     data_{"data"};
    InputPort<bool>          ext_{"isExtended"};
    InputPort<bool>          trig_{"trigger"};
    OutputPort<CanFrame>     out_{"out"};
};

}  // namespace

OP_REGISTER_NODE_WITH_SCHEMA(
    "Can.Encode",
    CanEncodeNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object","title":"CAN Encode",
      "description":"Build a CanFrame from id (UInt32) + data (List<UInt8>) + isExtended. Emits on 'trigger' and (optionally) on any input.",
      "properties":{
        "autoTriggerOnNewInput":{"type":"boolean","title":"Auto-emit on input","default":true},
        "defaults":{"type":"object","title":"Per-input default values","default":{},"additionalProperties":true}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"autoTriggerOnNewInput":true})JSON")

}  // namespace flowboard::nodes
