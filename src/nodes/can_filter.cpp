// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/can_frame.hpp"
#include "builtin_types.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace flowboard::nodes {

enum class FilterMode { AcceptAll, AcceptIds, RejectIds, IdRange, Mask };

inline FilterMode parse_mode(std::string_view s) {
    if (s == "acceptIds") return FilterMode::AcceptIds;
    if (s == "rejectIds") return FilterMode::RejectIds;
    if (s == "idRange")   return FilterMode::IdRange;
    if (s == "mask")      return FilterMode::Mask;
    return FilterMode::AcceptAll;
}

class CanFilterNode : public Node {
public:
    CanFilterNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Can.Filter"),
          in_("in"), out_("out"),
          mode_(parse_mode(cfg.value("mode", std::string{"acceptAll"}))) {
        if (auto it = cfg.find("ids"); it != cfg.end() && it->is_array())
            for (auto const& v : *it) ids_.insert(v.get<std::uint32_t>());
        id_min_   = cfg.value("idMin",   std::uint32_t{0});
        id_max_   = cfg.value("idMax",   std::uint32_t{0x1FFFFFFFu});
        id_mask_  = cfg.value("idMask",  std::uint32_t{0});
        id_match_ = cfg.value("idMatch", std::uint32_t{0});
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<CanFrame>::Value v) {
            enqueue([this, v] { if (passes(v->id)) out_.emit(v); });
        });
    }

private:
    bool passes(std::uint32_t id) const {
        switch (mode_) {
            case FilterMode::AcceptAll: return true;
            case FilterMode::AcceptIds: return ids_.count(id) > 0;
            case FilterMode::RejectIds: return ids_.count(id) == 0;
            case FilterMode::IdRange:   return id >= id_min_ && id <= id_max_;
            case FilterMode::Mask:      return (id & id_mask_) == id_match_;
        }
        return true;
    }

    InputPort<CanFrame>  in_;
    OutputPort<CanFrame> out_;
    FilterMode           mode_;
    std::unordered_set<std::uint32_t> ids_;
    std::uint32_t        id_min_, id_max_, id_mask_, id_match_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Can.Filter",
    CanFilterNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "mode":{"type":"string","title":"Mode",
          "enum":["acceptAll","acceptIds","rejectIds","idRange","mask"],
          "default":"acceptAll"},
        "ids":{"type":"array","title":"IDs",
          "items":{"type":"integer","minimum":0},"default":[]},
        "idMin":{"type":"integer","title":"ID min","minimum":0,"default":0},
        "idMax":{"type":"integer","title":"ID max","minimum":0,"default":536870911},
        "idMask":{"type":"integer","title":"ID mask","minimum":0,"default":0},
        "idMatch":{"type":"integer","title":"ID match","minimum":0,"default":0}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"mode":"acceptAll","ids":[],"idMin":0,"idMax":536870911,"idMask":0,"idMatch":0})JSON"
)

}  // namespace flowboard::nodes
