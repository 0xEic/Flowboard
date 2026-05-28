// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <nlohmann/json.hpp>
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/type_tag.hpp"

/// \file
/// \brief Templates backing the list-creation nodes (Transform.List.Build /
/// Transform.List.Accumulate). Element type is bound at construction; the
/// elements are serialised into the JSON-backed flowboard::ListValue.

namespace flowboard {

// JSON for one element. Primitives convert directly; struct element types use
// the one-arg `to_json(StructType const&)` overload pipegen emits in
// <Module>_types.hpp, found via ADL when the template is instantiated for a
// struct (the generated _nodes.cpp that instantiates it includes that header).
template <typename Elem>
inline ::nlohmann::json list_elem_json(Elem const& v) {
    if constexpr (std::is_arithmetic_v<Elem> || std::is_same_v<Elem, std::string>)
        return ::nlohmann::json(v);
    else
        return to_json(v);
}

/// \brief Assemble a fixed-size list from N typed `item<i>` inputs. Emits the
/// list automatically once every item has a value (and re-emits whenever any
/// item changes), and also on an explicit `trigger` pulse. Auto-emitting on
/// completeness avoids depending on producer emission ordering: upstream nodes
/// that pulse a "changed" signal *before* delivering their value (e.g. the
/// generated Factory.* nodes) would otherwise trigger an empty build.
template <typename Elem>
class ListBuildT final : public Node {
public:
    ListBuildT(std::string id, int count)
        : Node(std::move(id), "Transform.List.Build"), trig_("trigger"), out_("out") {
        if (count < 0) count = 0;
        for (int i = 0; i < count; ++i) {
            auto p = std::make_unique<InputPort<Elem>>("item" + std::to_string(i));
            register_input(p.get());
            items_.push_back(std::move(p));
        }
        last_.resize(items_.size());
        register_input(&trig_);
        register_output(&out_);
    }
    void on_start() override {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            items_[i]->set_internal_sink([this, i](typename InputPort<Elem>::Value v) {
                enqueue([this, i, v] { last_[i] = v; if (complete()) emit(); });
            });
        }
        trig_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v && *v) emit(); });
        });
    }
private:
    bool complete() const {
        for (auto const& l : last_) if (!l) return false;
        return true;
    }
    void emit() {
        ListValue lv;
        lv.element_type_tag = std::string(type_tag_v<Elem>);
        for (auto const& l : last_)
            if (l) lv.items.push_back(list_elem_json<Elem>(*l));
        out_.emit(std::make_shared<const ListValue>(std::move(lv)));
    }
    InputPort<bool>       trig_;
    OutputPort<ListValue> out_;
    std::vector<std::unique_ptr<InputPort<Elem>>> items_;
    std::vector<std::shared_ptr<const Elem>>      last_;
};

/// \brief Append each received `item` into a growing buffer (optionally capped,
/// dropping oldest); emit the running list when `trigger` pulses, clear on `reset`.
template <typename Elem>
class ListAccumulateT final : public Node {
public:
    ListAccumulateT(std::string id, int cap)
        : Node(std::move(id), "Transform.List.Accumulate"),
          item_("item"), trig_("trigger"), reset_("reset"), out_("out"),
          cap_(cap < 0 ? 0 : static_cast<std::size_t>(cap)) {
        register_input(&item_);
        register_input(&trig_);
        register_input(&reset_);
        register_output(&out_);
    }
    void on_start() override {
        item_.set_internal_sink([this](typename InputPort<Elem>::Value v) {
            enqueue([this, v] {
                if (!v) return;
                buf_.push_back(list_elem_json<Elem>(*v));
                if (cap_ > 0 && buf_.size() > cap_)
                    buf_.erase(buf_.begin(), buf_.begin() + (buf_.size() - cap_));
            });
        });
        trig_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v && *v) emit(); });
        });
        reset_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v && *v) buf_.clear(); });
        });
    }
private:
    void emit() {
        ListValue lv;
        lv.element_type_tag = std::string(type_tag_v<Elem>);
        lv.items = buf_;
        out_.emit(std::make_shared<const ListValue>(std::move(lv)));
    }
    InputPort<Elem>       item_;
    InputPort<bool>       trig_, reset_;
    OutputPort<ListValue> out_;
    std::vector<::nlohmann::json> buf_;
    std::size_t cap_;
};

}  // namespace flowboard
