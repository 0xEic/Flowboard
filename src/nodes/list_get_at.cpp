// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

// Emits `value` as a single-element ListValue (or empty when out-of-range) so
// downstream consumers stay in the dynamic-element world. To drill into the
// element, follow with a Transform.Extract (planned) or Transform.List.GetAt
// chain.
class ListGetAt final : public Node {
public:
    ListGetAt(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.List.GetAt"),
          in_("in"), value_("value"), is_present_("isPresent"),
          index_(cfg.value("index", std::int64_t{0})) {
        register_input(&in_);
        register_output(&value_);
        register_output(&is_present_);
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] {
                auto out = std::make_shared<ListValue>();
                out->element_type_tag = v->element_type_tag;
                auto n = static_cast<std::int64_t>(v->items.size());
                std::int64_t i = index_;
                if (i < 0) i += n;
                bool present = (i >= 0 && i < n);
                if (present) out->items.push_back(v->items[static_cast<std::size_t>(i)]);
                is_present_.emit(std::make_shared<const bool>(present));
                value_     .emit(std::move(out));
            });
        });
    }

private:
    InputPort<ListValue>   in_;
    OutputPort<ListValue>  value_;
    OutputPort<bool>       is_present_;
    std::int64_t           index_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.List.GetAt", ListGetAt,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "index":{"type":"integer","title":"Index","default":0,
                 "description":"Zero-based index. Negative values count from the end (-1 = last)."}
      },
      "additionalProperties":false
    })",
    R"({"index":0})")

}  // namespace flowboard::nodes
