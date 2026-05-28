// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/list_build.hpp"
#include "flowboard/list_build_registry.hpp"
#include "flowboard/list_value.hpp"
#include "builtin_types.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace flowboard::nodes {

// --- shared primitive dispatch ---------------------------------------------
// List.Build and List.Accumulate take typed element inputs, so the element type
// must be a compile-time type. We dispatch over the primitive set (the same set
// builtin_types declares). Struct element types would plug in via a per-struct
// codegen registry; for authored struct lists use Transform.List.Constant.
template <template <typename> class NodeT, typename... Args>
std::unique_ptr<Node> make_for_primitive(std::string const& tag, std::string id, Args... args) {
    if (tag == "flowboard::Bool")   return std::make_unique<NodeT<bool>>(std::move(id), args...);
    if (tag == "flowboard::Char")   return std::make_unique<NodeT<char>>(std::move(id), args...);
    if (tag == "flowboard::UInt8")  return std::make_unique<NodeT<std::uint8_t>>(std::move(id), args...);
    if (tag == "flowboard::Int16")  return std::make_unique<NodeT<std::int16_t>>(std::move(id), args...);
    if (tag == "flowboard::UInt16") return std::make_unique<NodeT<std::uint16_t>>(std::move(id), args...);
    if (tag == "flowboard::Int32")  return std::make_unique<NodeT<std::int32_t>>(std::move(id), args...);
    if (tag == "flowboard::UInt32") return std::make_unique<NodeT<std::uint32_t>>(std::move(id), args...);
    if (tag == "flowboard::Int64")  return std::make_unique<NodeT<std::int64_t>>(std::move(id), args...);
    if (tag == "flowboard::UInt64") return std::make_unique<NodeT<std::uint64_t>>(std::move(id), args...);
    if (tag == "flowboard::Float")  return std::make_unique<NodeT<float>>(std::move(id), args...);
    if (tag == "flowboard::Double") return std::make_unique<NodeT<double>>(std::move(id), args...);
    if (tag == "flowboard::String") return std::make_unique<NodeT<std::string>>(std::move(id), args...);
    return nullptr;
}

// --- Transform.List.Build ---------------------------------------------------

static auto _list_build_factory = [](std::string id, nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("elementType", std::string{"flowboard::Double"});
    int count = cfg.value("count", 2);
    auto node = make_for_primitive<ListBuildT>(t, id, count);
    if (!node)
        if (auto fn = lookup_list_build_factory(t)) node = fn(std::move(id), count);
    if (!node)
        throw std::runtime_error("Transform.List.Build: unsupported element type '" + t + "'");
    return node;
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.List.Build",
    _list_build_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"List Build",
      "description":"Assembles a list from N typed item inputs. Emits the list when 'trigger' pulses true (items not yet received are skipped). Primitive element types.",
      "required":["elementType","count"],
      "properties":{
        "elementType":{"type":"string","title":"Element type","default":"flowboard::Double",
          "description":"Any primitive or OnboardAPI struct type tag (e.g. M_Foo::BarType)."},
        "count":{"type":"integer","title":"Item count","minimum":0,"maximum":64,"default":2,
          "description":"Number of item inputs (item0..itemN-1)."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"elementType":"flowboard::Double","count":2})JSON",
    list_build
)

// --- Transform.List.Accumulate ----------------------------------------------

static auto _list_accum_factory = [](std::string id, nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("elementType", std::string{"flowboard::Double"});
    int cap = cfg.value("maxItems", 0);
    auto node = make_for_primitive<ListAccumulateT>(t, id, cap);
    if (!node)
        if (auto fn = lookup_list_accumulate_factory(t)) node = fn(std::move(id), cap);
    if (!node)
        throw std::runtime_error("Transform.List.Accumulate: unsupported element type '" + t + "'");
    return node;
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.List.Accumulate",
    _list_accum_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"List Accumulate",
      "description":"Appends each received 'item' into a growing list. Emits the running list when 'trigger' pulses true; 'reset' clears it. Primitive element types.",
      "required":["elementType"],
      "properties":{
        "elementType":{"type":"string","title":"Element type","default":"flowboard::Double",
          "description":"Any primitive or OnboardAPI struct type tag (e.g. M_Foo::BarType)."},
        "maxItems":{"type":"integer","title":"Max items","minimum":0,"default":0,
          "description":"Cap the buffer, dropping oldest when exceeded. 0 = unbounded."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"elementType":"flowboard::Double","maxItems":0})JSON",
    list_accumulate
)

// --- Transform.List.Constant ------------------------------------------------
// Emits a literal list authored in config. Carries JSON, so the element type can
// be ANYTHING (primitive or OnboardAPI struct) — no typed ports needed.
class ListConstant final : public Node {
public:
    ListConstant(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.List.Constant"),
          trig_("trigger"), out_("out"),
          auto_trigger_(cfg.value("autoTriggerOnInit", true)) {
        value_ = std::make_shared<const ListValue>(build(cfg));
        register_input(&trig_);
        register_output(&out_);
    }
    void on_start() override {
        trig_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v && *v) out_.emit(value_); });
        });
        if (auto_trigger_) enqueue([this] { out_.emit(value_); });
    }
private:
    static ListValue build(nlohmann::json const& cfg) {
        ListValue lv;
        lv.element_type_tag = cfg.value("elementType", std::string{});
        if (auto it = cfg.find("values"); it != cfg.end() && it->is_array())
            lv.items = it->get<std::vector<nlohmann::json>>();
        return lv;
    }
    InputPort<bool>          trig_;
    OutputPort<ListValue>    out_;
    std::shared_ptr<const ListValue> value_;
    bool auto_trigger_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.List.Constant",
    ListConstant,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"List Constant",
      "description":"Emits a fixed list authored here. Element type can be any primitive or OnboardAPI struct; 'values' is a JSON array of elements. Re-emits on 'trigger'.",
      "properties":{
        "elementType":{"type":"string","title":"Element type",
          "description":"Type tag of the elements (e.g. flowboard::Double or M_Foo::BarType). Informational + drives downstream list element-type checks."},
        "values":{"type":"array","title":"Values","description":"JSON array of elements.","items":{}},
        "autoTriggerOnInit":{"type":"boolean","title":"Auto-trigger on init","default":true}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"elementType":"flowboard::Double","values":[],"autoTriggerOnInit":true})JSON")

}  // namespace flowboard::nodes
