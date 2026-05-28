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

class ListFilter final : public Node {
public:
    ListFilter(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.List.Filter"),
          in_("in"), out_("out"),
          field_(cfg.value("field", std::string{})),
          op_(list_ops::parse_predicate_op(cfg.value("op", std::string{"eq"}))),
          target_(cfg.value("value", ::nlohmann::json{})) {
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] {
                auto out = std::make_shared<ListValue>();
                out->element_type_tag = v->element_type_tag;
                out->items.reserve(v->items.size());
                for (auto const& item : v->items) {
                    auto fv = list_ops::resolve_path(item, field_);
                    if (list_ops::eval_predicate(fv, op_, target_))
                        out->items.push_back(item);
                }
                out_.emit(std::move(out));
            });
        });
    }

private:
    InputPort<ListValue>      in_;
    OutputPort<ListValue>     out_;
    std::string               field_;
    list_ops::PredicateOp     op_;
    ::nlohmann::json          target_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.List.Filter", ListFilter,
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
                 "description":"JSON value to compare against. Strings compare lexicographically; numbers numerically; 'contains' requires both sides to be strings."}
      },
      "additionalProperties":false
    })",
    R"({"field":"","op":"eq","value":null})")

}  // namespace flowboard::nodes
