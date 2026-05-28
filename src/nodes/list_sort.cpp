// SPDX-License-Identifier: MIT
#include <algorithm>
#include <memory>
#include <string>
#include "builtin_types.hpp"
#include "flowboard/list_ops.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

class ListSort final : public Node {
public:
    ListSort(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.List.Sort"),
          in_("in"), out_("out"),
          field_(cfg.value("field", std::string{})),
          ascending_(cfg.value("ascending", true)) {
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] {
                auto out = std::make_shared<ListValue>();
                out->element_type_tag = v->element_type_tag;
                out->items = v->items;
                std::string const& path = field_;
                bool asc = ascending_;
                std::stable_sort(out->items.begin(), out->items.end(),
                    [&](::nlohmann::json const& a, ::nlohmann::json const& b) {
                        auto va = list_ops::resolve_path(a, path);
                        auto vb = list_ops::resolve_path(b, path);
                        int c = list_ops::compare_json(va, vb);
                        return asc ? (c < 0) : (c > 0);
                    });
                out_.emit(std::move(out));
            });
        });
    }

private:
    InputPort<ListValue>  in_;
    OutputPort<ListValue> out_;
    std::string           field_;
    bool                  ascending_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.List.Sort", ListSort,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "field":    {"type":"string","title":"Field path","default":"",
                     "description":"Dot path resolved against each element, e.g. 'key' or 'value.Label'. Empty = compare whole elements."},
        "ascending":{"type":"boolean","title":"Ascending","default":true}
      },
      "additionalProperties":false
    })",
    R"({"field":"","ascending":true})")

}  // namespace flowboard::nodes
