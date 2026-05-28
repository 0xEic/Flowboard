// SPDX-License-Identifier: MIT
//
// Transform.List.Combine — concatenate two lists (a then b) into one.
#include <memory>
#include <mutex>

#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

class ListCombine final : public Node {
public:
    ListCombine(std::string id, ::nlohmann::json const&)
        : Node(std::move(id), "Transform.List.Combine"),
          a_("a"), b_("b"), out_("out") {
        register_input(&a_);
        register_input(&b_);
        register_output(&out_);
    }

    void on_start() override {
        a_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] { a_cur_ = v; emit(); });
        });
        b_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] { b_cur_ = v; emit(); });
        });
    }

private:
    void emit() {
        auto out = std::make_shared<ListValue>();
        if (a_cur_) {
            out->items = a_cur_->items;
            out->element_type_tag = a_cur_->element_type_tag;
        }
        if (b_cur_) {
            out->items.insert(out->items.end(), b_cur_->items.begin(), b_cur_->items.end());
            if (out->element_type_tag.empty()) out->element_type_tag = b_cur_->element_type_tag;
        }
        out_.emit(std::shared_ptr<const ListValue>(std::move(out)));
    }

    InputPort<ListValue>  a_;
    InputPort<ListValue>  b_;
    OutputPort<ListValue> out_;
    std::shared_ptr<const ListValue> a_cur_;
    std::shared_ptr<const ListValue> b_cur_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.List.Combine", ListCombine,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object","properties":{},"additionalProperties":false
    })JSON",
    R"JSON({})JSON")

}  // namespace flowboard::nodes
