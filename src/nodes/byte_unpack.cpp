// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/byte_codec.hpp"
#include "flowboard/port_factory_registry.hpp"
#include "builtin_types.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

namespace flowboard::nodes {
namespace {

struct OutField {
    std::string name; std::string type; std::size_t offset;
    bool has_bit = false; std::size_t bit_off = 0;
    IOutputPort* port; EmitFromJsonFn emit;
};

inline bool make_byte_output(std::string const& tag, std::string name,
        std::unique_ptr<IOutputPort>& port_out, EmitFromJsonFn& emit_out) {
    auto mk = [&]<typename T>() {
        port_out = _make_output<T>(std::move(name));
        emit_out = &_emit_from_json<T>;
    };
    if (tag == "flowboard::Bool")   { mk.template operator()<bool>();          return true; }
    if (tag == "flowboard::Char")   { mk.template operator()<char>();          return true; }
    if (tag == "flowboard::UInt8")  { mk.template operator()<std::uint8_t>();  return true; }
    if (tag == "flowboard::Int16")  { mk.template operator()<std::int16_t>();  return true; }
    if (tag == "flowboard::UInt16") { mk.template operator()<std::uint16_t>(); return true; }
    if (tag == "flowboard::Int32")  { mk.template operator()<std::int32_t>();  return true; }
    if (tag == "flowboard::UInt32") { mk.template operator()<std::uint32_t>(); return true; }
    if (tag == "flowboard::Int64")  { mk.template operator()<std::int64_t>();  return true; }
    if (tag == "flowboard::UInt64") { mk.template operator()<std::uint64_t>(); return true; }
    if (tag == "flowboard::Float")  { mk.template operator()<float>();         return true; }
    if (tag == "flowboard::Double") { mk.template operator()<double>();        return true; }
    return false;
}

class ByteUnpackNode final : public Node {
public:
    ByteUnpackNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Bytes.Unpack"),
          little_(cfg.value("endian", std::string{"little"}) != "big") {
        register_input(&in_);
        if (auto it = cfg.find("fields"); it != cfg.end() && it->is_array()) {
            for (auto const& f : *it) {
                if (!f.is_object()) continue;
                std::string name = f.value("name", std::string{});
                std::string type = f.value("type", std::string{"flowboard::UInt8"});
                std::size_t offset = static_cast<std::size_t>(std::max(0, f.value("offset", 0)));
                if (name.empty()) continue;
                if (byte_width(type) == 0)
                    throw std::runtime_error("Bytes.Unpack: unsupported field type '" + type + "'");
                bool has_bit = false; std::size_t bit_off = 0;
                if (auto b = f.find("bitOffset"); b != f.end() && b->is_number_integer()) {
                    has_bit = true;
                    bit_off = static_cast<std::size_t>(std::clamp(b->get<int>(), 0, 7));
                }
                std::unique_ptr<IOutputPort> port; EmitFromJsonFn emit = nullptr;
                make_byte_output(type, name, port, emit);
                OutField of{ name, type, offset, has_bit, bit_off, port.get(), emit };
                register_output(port.get());
                out_storage_.push_back(std::move(port));
                fields_.push_back(of);
            }
        }
        if (!little_) {
            for (auto const& fl : fields_)
                if (fl.has_bit) {
                    spdlog::warn("[{}] Bytes.Unpack: bitOffset is little/Intel only; "
                                 "endian=big is ignored for bit fields", std::string(Node::id()));
                    break;
                }
        }
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] { if (v) decode(*v); });
        });
    }

private:
    void decode(ListValue const& lv) {
        std::vector<std::uint8_t> buf;
        buf.reserve(lv.items.size());
        for (auto const& e : lv.items)
            buf.push_back(static_cast<std::uint8_t>(e.is_number() ? (e.get<long long>() & 0xFF) : 0));
        for (auto const& f : fields_) {
            auto val = f.has_bit ? bit_read(buf, f.offset * 8 + f.bit_off, f.type)
                                 : byte_read(buf, f.offset, f.type, little_);
            if (val) f.emit(f.port, *val);
        }
    }

    bool little_;
    std::vector<OutField> fields_;
    std::vector<std::unique_ptr<IOutputPort>> out_storage_;
    InputPort<ListValue> in_{"in"};
};

static auto _byte_unpack_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    return std::make_unique<ByteUnpackNode>(std::move(id), cfg);
};

}  // namespace

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Bytes.Unpack",
    _byte_unpack_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object","title":"Bytes Unpack",
      "description":"Decode typed primitive fields from a List<UInt8> at byte offsets; emits each field on its output when a byte list arrives.",
      "properties":{
        "fields":{"type":"array","title":"Fields","default":[],
          "items":{"type":"object","required":["name","type","offset"],
            "properties":{
              "name":{"type":"string","title":"Name"},
              "type":{"type":"string","title":"Type","default":"flowboard::UInt8"},
              "offset":{"type":"integer","title":"Byte offset","minimum":0,"default":0},
              "bitOffset":{"type":"integer","title":"Bit offset within byte (0-7)","minimum":0,"maximum":7}},
            "additionalProperties":false}},
        "endian":{"type":"string","title":"Endian","enum":["little","big"],"default":"little"}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"fields":[],"endian":"little"})JSON",
    byte_unpack
)

}  // namespace flowboard::nodes
