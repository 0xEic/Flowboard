// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

class ListSize final : public Node {
public:
    ListSize(std::string id, ::nlohmann::json const&)
        : Node(std::move(id), "Transform.List.Size"),
          in_("in"), count_("count"), is_empty_("isEmpty") {
        register_input(&in_);
        register_output(&count_);
        register_output(&is_empty_);
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v] {
                auto n = static_cast<std::int64_t>(v->items.size());
                count_   .emit(std::make_shared<const std::int64_t>(n));
                is_empty_.emit(std::make_shared<const bool>(n == 0));
            });
        });
    }

private:
    InputPort<ListValue>      in_;
    OutputPort<std::int64_t>  count_;
    OutputPort<bool>          is_empty_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.List.Size", ListSize,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{},
      "additionalProperties":false
    })",
    R"({})")

}  // namespace flowboard::nodes
