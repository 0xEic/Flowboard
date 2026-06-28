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
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

namespace flowboard::nodes {
namespace {

inline std::unique_ptr<IInputPort> make_byte_input(
        std::string const& tag, std::string name,
        std::function<void(::nlohmann::json)> cb) {
    auto mk = [&]<typename T>() { return _make_input_with_json_sink<T>(std::move(name), std::move(cb)); };
    if (tag == "flowboard::Bool")   return mk.template operator()<bool>();
    if (tag == "flowboard::Char")   return mk.template operator()<char>();
    if (tag == "flowboard::UInt8")  return mk.template operator()<std::uint8_t>();
    if (tag == "flowboard::Int16")  return mk.template operator()<std::int16_t>();
    if (tag == "flowboard::UInt16") return mk.template operator()<std::uint16_t>();
    if (tag == "flowboard::Int32")  return mk.template operator()<std::int32_t>();
    if (tag == "flowboard::UInt32") return mk.template operator()<std::uint32_t>();
    if (tag == "flowboard::Int64")  return mk.template operator()<std::int64_t>();
    if (tag == "flowboard::UInt64") return mk.template operator()<std::uint64_t>();
    if (tag == "flowboard::Float")  return mk.template operator()<float>();
    if (tag == "flowboard::Double") return mk.template operator()<double>();
    return nullptr;
}

struct Field { std::string name; std::string type; std::size_t offset;
               bool has_bit = false; std::size_t bit_off = 0; };

class BytePackNode final : public Node {
public:
    BytePackNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Bytes.Pack"),
          little_(cfg.value("endian", std::string{"little"}) != "big"),
          length_(static_cast<std::size_t>(std::max(0, cfg.value("length", 0)))),
          auto_(cfg.value("autoTriggerOnNewInput", true)) {
        if (auto it = cfg.find("fields"); it != cfg.end() && it->is_array()) {
            for (auto const& f : *it) {
                if (!f.is_object()) continue;
                Field fld{ f.value("name", std::string{}),
                           f.value("type", std::string{"flowboard::UInt8"}),
                           static_cast<std::size_t>(std::max(0, f.value("offset", 0))) };
                if (auto b = f.find("bitOffset"); b != f.end() && b->is_number_integer()) {
                    fld.has_bit = true;
                    fld.bit_off = static_cast<std::size_t>(std::clamp(b->get<int>(), 0, 7));
                }
                if (fld.name.empty()) continue;
                if (byte_width(fld.type) == 0)
                    throw std::runtime_error("Bytes.Pack: unsupported field type '" + fld.type + "'");
                std::size_t idx = fields_.size();
                last_.emplace_back();
                auto p = make_byte_input(fld.type, fld.name,
                    [this, idx](::nlohmann::json j) {
                        enqueue([this, idx, j = std::move(j)]() mutable {
                            last_[idx] = std::move(j);
                            if (auto_) assemble();
                        });
                    });
                register_input(p.get());
                in_storage_.push_back(std::move(p));
                fields_.push_back(std::move(fld));
            }
        }
        if (!little_) {
            for (auto const& fl : fields_)
                if (fl.has_bit) {
                    spdlog::warn("[{}] Bytes.Pack: bitOffset is little/Intel only; "
                                 "endian=big is ignored for bit fields", std::string(Node::id()));
                    break;
                }
        }
        register_input(&trig_);
        register_output(&out_);
    }

    void on_start() override {
        trig_.set_internal_sink([this](InputPort<bool>::Value v) {
            enqueue([this, v] { if (v && *v) assemble(); });
        });
    }

private:
    void assemble() {
        std::vector<std::uint8_t> buf;
        if (length_ > 0) buf.assign(length_, 0);
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (last_[i].is_null()) continue;
            auto const& fld = fields_[i];
            if (fld.has_bit) {
                std::size_t bit_pos = fld.offset * 8 + fld.bit_off;
                std::size_t need = (bit_pos + bit_width(fld.type) + 7) / 8;
                if (length_ > 0 && need > length_) continue;
                bit_write(buf, bit_pos, fld.type, last_[i]);
            } else {
                if (length_ > 0 && fld.offset + byte_width(fld.type) > length_) continue;
                byte_write(buf, fld.offset, fld.type, last_[i], little_);
            }
        }
        if (length_ > 0 && buf.size() > length_) buf.resize(length_);
        auto lv = std::make_shared<ListValue>();
        lv->element_type_tag = "flowboard::UInt8";
        lv->items.reserve(buf.size());
        for (std::uint8_t b : buf) lv->items.push_back(::nlohmann::json(b));
        out_.emit(std::shared_ptr<const ListValue>(std::move(lv)));
    }

    bool little_; std::size_t length_; bool auto_;
    std::vector<Field> fields_;
    std::vector<::nlohmann::json> last_;
    std::vector<std::unique_ptr<IInputPort>> in_storage_;
    InputPort<bool>       trig_{"trigger"};
    OutputPort<ListValue> out_{"out"};
};

static auto _byte_pack_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    return std::make_unique<BytePackNode>(std::move(id), cfg);
};

}  // namespace

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Bytes.Pack",
    _byte_pack_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object","title":"Bytes Pack",
      "description":"Place typed primitive inputs at byte offsets into a List<UInt8>. Emits on 'trigger' and (optionally) on any input.",
      "properties":{
        "fields":{"type":"array","title":"Fields","default":[],
          "items":{"type":"object","required":["name","type","offset"],
            "properties":{
              "name":{"type":"string","title":"Name"},
              "type":{"type":"string","title":"Type","default":"flowboard::UInt8"},
              "offset":{"type":"integer","title":"Byte offset","minimum":0,"default":0},
              "bitOffset":{"type":"integer","title":"Bit offset within byte (0-7)","minimum":0,"maximum":7}},
            "additionalProperties":false}},
        "endian":{"type":"string","title":"Endian","enum":["little","big"],"default":"little"},
        "length":{"type":"integer","title":"Output length (0 = auto)","minimum":0,"default":0},
        "autoTriggerOnNewInput":{"type":"boolean","title":"Auto-emit on input","default":true},
        "defaults":{"type":"object","title":"Per-field default values","default":{},"additionalProperties":true}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"fields":[],"endian":"little","length":0,"autoTriggerOnNewInput":true})JSON",
    byte_pack
)

}  // namespace flowboard::nodes
