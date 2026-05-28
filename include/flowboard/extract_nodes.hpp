// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/json_helpers.hpp"
#include "flowboard/list_value.hpp"

namespace flowboard {

/// \file
/// \brief Transform.Extract node templates that pull scalar, optional, or list fields out of an input value.

/// \brief Convert a JSON value back into a typed C++ value.
/// For enums the JSON value is the underlying integer (see flowboard::to_json_or_passthrough).
template <typename T>
inline T extract_typed(::nlohmann::json const& j) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<T>(j.template get<std::underlying_type_t<T>>());
    } else {
        return j.template get<T>();
    }
}

/// \brief Extracts a single named scalar field from the input and emits it as a typed value.
template <class TIn, class TOut>
class ExtractScalar : public Node {
public:
    ExtractScalar(std::string id, std::string field)
        : Node(std::move(id), "Transform.Extract"),
          in_("in"), out_("out"), field_(std::move(field)) {
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<TIn>::Value v) {
            enqueue([this, v]{
                auto j = ::flowboard::to_json_or_passthrough(*v);
                auto fv = j.at(field_);
                out_.emit(std::make_shared<const TOut>(extract_typed<TOut>(fv)));
            });
        });
    }
private:
    InputPort<TIn>   in_;
    OutputPort<TOut> out_;
    std::string      field_;
};

/// \brief Extracts an optional (sequence<T, 1>) field, emitting its filled flag and, when present, the element value.
template <class TIn, class TElem>
class ExtractOptional : public Node {
public:
    ExtractOptional(std::string id, std::string field)
        : Node(std::move(id), "Transform.Extract"),
          in_("in"), value_("value"), is_filled_("isFilled"),
          field_(std::move(field)) {
        register_input(&in_);
        register_output(&value_);
        register_output(&is_filled_);
    }
    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<TIn>::Value v) {
            enqueue([this, v]{
                auto j      = ::flowboard::to_json_or_passthrough(*v);
                auto fv     = j.at(field_);
                bool filled = !fv.empty();
                is_filled_.emit(std::make_shared<const bool>(filled));
                if (filled)
                    value_.emit(std::make_shared<const TElem>(extract_typed<TElem>(fv[0])));
            });
        });
    }
private:
    InputPort<TIn>     in_;
    OutputPort<TElem>  value_;
    OutputPort<bool>   is_filled_;
    std::string        field_;
};

/// \brief List-kind field extractor. Emits the whole field as a single first-class
/// `flowboard::List` value (rather than fanning out one element at a time),
/// so downstream Transform.List.* nodes can sort/filter/find/index it. Each
/// list element is carried as JSON; the ListValue's element_type_tag records
/// the element type for UI/logging hints. TElem is retained for that tag.
template <class TIn, class TElem>
class ExtractList : public Node {
public:
    ExtractList(std::string id, std::string field)
        : Node(std::move(id), "Transform.Extract"),
          in_("in"), list_("list"), field_(std::move(field)) {
        register_input(&in_);
        register_output(&list_);
    }
    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<TIn>::Value v) {
            enqueue([this, v]{
                auto j  = ::flowboard::to_json_or_passthrough(*v);
                auto fv = j.at(field_);
                auto out = std::make_shared<ListValue>();
                out->element_type_tag = type_tag_v<TElem>;
                if (fv.is_array())
                    for (auto const& el : fv) out->items.push_back(el);
                list_.emit(std::move(out));
            });
        });
    }
private:
    InputPort<TIn>          in_;
    OutputPort<ListValue>   list_;
    std::string             field_;
};

}  // namespace flowboard
