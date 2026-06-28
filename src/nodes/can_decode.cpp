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

class CanDecodeNode final : public Node {
public:
    CanDecodeNode(std::string id, ::nlohmann::json const&)
        : Node(std::move(id), "Can.Decode") {
        register_input(&frame_);
        register_output(&id_);
        register_output(&data_);
        register_output(&ext_);
        register_output(&dlc_);
    }

    void on_start() override {
        frame_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v) emit_parts(*v); });
        });
    }

private:
    void emit_parts(CanFrame const& f) {
        id_.emit(std::make_shared<const std::uint32_t>(f.id));
        ext_.emit(std::make_shared<const bool>(f.is_extended));
        dlc_.emit(std::make_shared<const std::uint8_t>(f.dlc));
        auto lv = std::make_shared<ListValue>();
        lv->element_type_tag = "flowboard::UInt8";
        lv->items.reserve(f.data.size());
        for (std::uint8_t b : f.data) lv->items.push_back(::nlohmann::json(b));
        data_.emit(std::shared_ptr<const ListValue>(std::move(lv)));
    }

    InputPort<CanFrame>       frame_{"frame"};
    OutputPort<std::uint32_t> id_{"id"};
    OutputPort<ListValue>     data_{"data"};
    OutputPort<bool>          ext_{"isExtended"};
    OutputPort<std::uint8_t>  dlc_{"dlc"};
};

}  // namespace

OP_REGISTER_NODE_WITH_SCHEMA(
    "Can.Decode",
    CanDecodeNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object","title":"CAN Decode",
      "description":"Split a CanFrame into id (UInt32), data (List<UInt8>), isExtended (Bool), dlc (UInt8).",
      "properties":{},
      "additionalProperties":false
    })JSON",
    R"JSON({})JSON")

}  // namespace flowboard::nodes
