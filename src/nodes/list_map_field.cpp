// SPDX-License-Identifier: MIT
//
// Transform.List.MapField — manipulate one field of every list element.
// Select a dot-path field; apply a number formula or a string operation to it,
// writing the result back into each element.
#include <memory>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/list_ops.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/manipulate_ops.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

class ListMapField final : public Node {
public:
    ListMapField(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.List.MapField"),
          in_("in"), out_("out"),
          field_(cfg.value("field", std::string{})),
          value_type_(cfg.value("valueType", std::string{"number"})),
          formula_(cfg.value("formula", std::string{"x"})),
          mode_(cfg.value("mode", std::string{"replace"})),
          find_(cfg.value("find", std::string{})),
          replace_(cfg.value("replace", std::string{})),
          text_(cfg.value("text", std::string{})) {
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] {
                auto out = std::make_shared<ListValue>(*v);  // copy items + tag
                for (auto& item : out->items) {
                    auto cur = list_ops::resolve_path(item, field_);
                    if (value_type_ == "string") {
                        if (cur.is_string()) {
                            auto r = ::flowboard::apply_string_op(
                                cur.get<std::string>(), mode_, find_, replace_, text_);
                            list_ops::set_path(item, field_, r);
                        }
                    } else {  // number
                        if (cur.is_number()) {
                            double x = cur.get<double>();
                            double r = ::flowboard::eval_formula(formula_, x, x);
                            list_ops::set_path(item, field_, r);
                        }
                    }
                }
                out_.emit(std::shared_ptr<const ListValue>(std::move(out)));
            });
        });
    }

private:
    InputPort<ListValue>  in_;
    OutputPort<ListValue> out_;
    std::string field_, value_type_, formula_, mode_, find_, replace_, text_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.List.MapField", ListMapField,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "field":{"type":"string","title":"Field path","default":"",
                 "description":"Dot-path of the element field to manipulate (e.g. value.Azimuth). Empty = the element itself."},
        "valueType":{"type":"string","title":"Value kind","enum":["number","string"],"default":"number"},
        "formula":{"type":"string","title":"Formula","default":"x",
                   "description":"Number mode: arithmetic expression in x (e.g. \"x * 2\")."},
        "mode":{"type":"string","title":"String mode","enum":["replace","set","prepend","append"],"default":"replace"},
        "find":{"type":"string","title":"Find","default":""},
        "replace":{"type":"string","title":"Replace with","default":""},
        "text":{"type":"string","title":"Text","default":""}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"field":"","valueType":"number","formula":"x","mode":"replace","find":"","replace":"","text":""})JSON")

}  // namespace flowboard::nodes
