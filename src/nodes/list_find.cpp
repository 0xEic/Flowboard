// SPDX-License-Identifier: MIT
#include <memory>
#include <string>
#include "builtin_types.hpp"
#include "flowboard/list_ops.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

// Emits the first matching element as a single-element ListValue plus an
// isFound bool. When no element matches, value is an empty ListValue.
class ListFind final : public Node {
public:
    ListFind(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.List.Find"),
          in_("in"), value_("value"), is_found_("isFound"),
          field_(cfg.value("field", std::string{})),
          op_(list_ops::parse_predicate_op(cfg.value("op", std::string{"eq"}))),
          target_(cfg.value("value", ::nlohmann::json{})) {
        register_input(&in_);
        register_output(&value_);
        register_output(&is_found_);
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] {
                auto out = std::make_shared<ListValue>();
                out->element_type_tag = v->element_type_tag;
                bool found = false;
                for (auto const& item : v->items) {
                    auto fv = list_ops::resolve_path(item, field_);
                    if (list_ops::eval_predicate(fv, op_, target_)) {
                        out->items.push_back(item);
                        found = true;
                        break;
                    }
                }
                is_found_.emit(std::make_shared<const bool>(found));
                value_   .emit(std::move(out));
            });
        });
    }

private:
    InputPort<ListValue>      in_;
    OutputPort<ListValue>     value_;
    OutputPort<bool>          is_found_;
    std::string               field_;
    list_ops::PredicateOp     op_;
    ::nlohmann::json          target_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.List.Find", ListFind,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["op"],
      "properties":{
        "field":{"type":"string","title":"Field path","default":"",
                 "description":"Dot path resolved against each element. Empty = match against whole element."},
        "op":   {"type":"string","title":"Predicate","default":"eq",
                 "enum":["eq","neq","lt","lte","gt","gte","contains"]},
        "value":{"title":"Compared value","default":null,
                 "description":"JSON value to compare against."}
      },
      "additionalProperties":false
    })",
    R"({"field":"","op":"eq","value":null})")

}  // namespace flowboard::nodes
