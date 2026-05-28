// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace flowboard::nodes {
namespace {

// --- primitive conversion helpers ------------------------------------------

// Human-readable rendering of a primitive value (for the ->String path).
// `decimals` controls floating-point precision (ignored for other types).
template <class TIn>
std::string value_label(TIn const& v, [[maybe_unused]] int decimals) {
    if constexpr (std::is_same_v<TIn, bool>)             return v ? "true" : "false";
    else if constexpr (std::is_same_v<TIn, char>)        return std::string(1, v);
    else if constexpr (std::is_same_v<TIn, std::string>) return v;
    else if constexpr (std::is_floating_point_v<TIn>) {
        std::ostringstream os; os << std::fixed << std::setprecision(decimals) << v; return os.str();
    } else                                               return std::to_string(v);  // integral
}

inline std::string apply_template(std::string const& templ, std::string const& label) {
    std::string out;
    out.reserve(templ.size() + label.size());
    std::size_t i = 0;
    while (i < templ.size()) {
        if (templ.compare(i, 7, "{value}") == 0) { out += label; i += 7; }
        else { out.push_back(templ[i]); ++i; }
    }
    return out;
}

template <class TOut>
TOut parse_string(std::string const& s) {
    if constexpr (std::is_same_v<TOut, std::string>) return s;
    else if constexpr (std::is_same_v<TOut, bool>)   return s == "true" || s == "1";
    else if constexpr (std::is_same_v<TOut, char>)   return s.empty() ? char(0) : s[0];
    else if constexpr (std::is_floating_point_v<TOut>) {
        try { return static_cast<TOut>(std::stod(s)); } catch (...) { return TOut(0); }
    } else {
        try {
            if constexpr (std::is_unsigned_v<TOut>) return static_cast<TOut>(std::stoull(s));
            else                                    return static_cast<TOut>(std::stoll(s));
        } catch (...) { return TOut(0); }
    }
}

template <class TOut, class TIn>
TOut convert_value(TIn const& v, std::string const& templ, int decimals) {
    if constexpr (std::is_same_v<TOut, std::string>)      return apply_template(templ, value_label(v, decimals));
    else if constexpr (std::is_same_v<TIn, std::string>)  return parse_string<TOut>(v);
    else if constexpr (std::is_same_v<TOut, bool>)        return v != TIn(0);
    else                                                  return static_cast<TOut>(v);
}

template <class TOut>
TOut json_to(nlohmann::json const& j) {
    if constexpr (std::is_same_v<TOut, std::string>) {
        if (j.is_string()) return j.get<std::string>();
        if (j.is_null())   return std::string{};
        return j.dump();
    } else if constexpr (std::is_same_v<TOut, bool>) {
        if (j.is_boolean()) return j.get<bool>();
        if (j.is_number())  return j.get<double>() != 0.0;
        if (j.is_string())  { auto s = j.get<std::string>(); return s == "true" || s == "1"; }
        return false;
    } else if constexpr (std::is_same_v<TOut, char>) {
        if (j.is_string()) { auto s = j.get<std::string>(); return s.empty() ? char(0) : s[0]; }
        if (j.is_number()) return static_cast<char>(j.get<long long>());
        return char(0);
    } else if constexpr (std::is_floating_point_v<TOut>) {
        if (j.is_number())  return static_cast<TOut>(j.get<double>());
        if (j.is_string())  return parse_string<TOut>(j.get<std::string>());
        if (j.is_boolean()) return j.get<bool>() ? TOut(1) : TOut(0);
        return TOut(0);
    } else {
        if (j.is_number_integer())  return static_cast<TOut>(j.get<long long>());
        if (j.is_number_unsigned()) return static_cast<TOut>(j.get<unsigned long long>());
        if (j.is_number())          return static_cast<TOut>(j.get<double>());
        if (j.is_string())          return parse_string<TOut>(j.get<std::string>());
        if (j.is_boolean())         return j.get<bool>() ? TOut(1) : TOut(0);
        return TOut(0);
    }
}

inline bool compare_op(double a, double b, std::string const& op) {
    if (op == "gt")  return a >  b;
    if (op == "gte") return a >= b;
    if (op == "lt")  return a <  b;
    if (op == "lte") return a <= b;
    if (op == "eq")  return a == b;
    if (op == "neq") return a != b;
    return false;
}

inline std::string transform_case(std::string s, std::string const& mode) {
    if (mode == "upper")      for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    else if (mode == "lower") for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    else if (mode == "trim") {
        auto b = s.find_first_not_of(" \t\n\r");
        auto e = s.find_last_not_of(" \t\n\r");
        s = (b == std::string::npos) ? std::string{} : s.substr(b, e - b + 1);
    }
    return s;
}

// --- node -------------------------------------------------------------------

// Converts one primitive input to one primitive output. The `mode` selects the
// transform; modes that don't apply to the concrete (TIn, TOut) pair are
// compiled out and fall through to plain conversion.
//   convert   : type-aware conversion (format / parse / cast)        [any]
//   lookup    : enum table {when -> value} + fallback                [int in]
//   scale     : out = in*scale + offset, optional clamp              [num->num]
//   threshold : out = (in OP threshold)                              [num->Bool]
//   boolmap   : true -> trueValue, false -> falseValue               [Bool in]
//   case      : uppercase / lowercase / trim                         [String->String]
template <class TIn, class TOut>
class ConvertNode final : public Node {
    static constexpr bool kInNum  = std::is_arithmetic_v<TIn>  && !std::is_same_v<TIn, bool>;
    static constexpr bool kOutNum = std::is_arithmetic_v<TOut> && !std::is_same_v<TOut, bool>;
public:
    ConvertNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Convert"),
          in_("in"), out_("out"),
          template_(cfg.value("template", std::string{"{value}"})),
          mode_(cfg.value("mode", std::string{"convert"})),
          decimals_(cfg.value("decimals", 6)) {
        register_input(&in_);
        register_output(&out_);
        if constexpr (std::is_integral_v<TIn>) {
            if (mode_ == "lookup") {
                for (auto const& m : cfg.value("mappings", nlohmann::json::array())) {
                    if (!m.contains("when")) continue;
                    table_[m["when"].get<long long>()] = json_to<TOut>(m.value("value", nlohmann::json{}));
                }
                fallback_ = json_to<TOut>(cfg.value("fallback", nlohmann::json{}));
            }
        }
        if constexpr (std::is_same_v<TIn, bool>) {
            true_val_  = json_to<TOut>(cfg.value("trueValue",  nlohmann::json{}));
            false_val_ = json_to<TOut>(cfg.value("falseValue", nlohmann::json{}));
        }
        if constexpr (kInNum && kOutNum) {
            scale_  = cfg.value("scale",  1.0);
            offset_ = cfg.value("offset", 0.0);
            clamp_  = cfg.value("clamp",  false);
            clamp_min_ = cfg.value("clampMin", 0.0);
            clamp_max_ = cfg.value("clampMax", 0.0);
        }
        if constexpr (kInNum && std::is_same_v<TOut, bool>) {
            threshold_ = cfg.value("threshold", 0.0);
            cmp_op_    = cfg.value("compareOp", std::string{"gt"});
        }
        if constexpr (std::is_same_v<TIn, std::string> && std::is_same_v<TOut, std::string>) {
            case_mode_ = cfg.value("textTransform", std::string{"none"});
        }
    }

    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<TIn>::Value v) {
            enqueue([this, v] {
                if constexpr (std::is_integral_v<TIn>) {
                    if (mode_ == "lookup") {
                        auto it = table_.find(static_cast<long long>(*v));
                        out_.emit(std::make_shared<const TOut>(it != table_.end() ? it->second : fallback_));
                        return;
                    }
                }
                if constexpr (std::is_same_v<TIn, bool>) {
                    if (mode_ == "boolmap") {
                        out_.emit(std::make_shared<const TOut>(*v ? true_val_ : false_val_));
                        return;
                    }
                }
                if constexpr (kInNum && kOutNum) {
                    if (mode_ == "scale") {
                        double r = static_cast<double>(*v) * scale_ + offset_;
                        if (clamp_) r = std::clamp(r, clamp_min_, clamp_max_);
                        out_.emit(std::make_shared<const TOut>(static_cast<TOut>(r)));
                        return;
                    }
                }
                if constexpr (kInNum && std::is_same_v<TOut, bool>) {
                    if (mode_ == "threshold") {
                        out_.emit(std::make_shared<const TOut>(compare_op(static_cast<double>(*v), threshold_, cmp_op_)));
                        return;
                    }
                }
                if constexpr (std::is_same_v<TIn, std::string> && std::is_same_v<TOut, std::string>) {
                    if (mode_ == "case") {
                        out_.emit(std::make_shared<const TOut>(transform_case(*v, case_mode_)));
                        return;
                    }
                }
                out_.emit(std::make_shared<const TOut>(convert_value<TOut>(*v, template_, decimals_)));
            });
        });
    }

private:
    InputPort<TIn>            in_;
    OutputPort<TOut>          out_;
    std::string               template_;
    std::string               mode_;
    int                       decimals_ = 6;
    // lookup
    std::map<long long, TOut> table_;
    TOut                      fallback_{};
    // boolmap
    TOut                      true_val_{};
    TOut                      false_val_{};
    // scale
    double                    scale_  = 1.0;
    double                    offset_ = 0.0;
    bool                      clamp_  = false;
    double                    clamp_min_ = 0.0;
    double                    clamp_max_ = 0.0;
    // threshold
    double                    threshold_ = 0.0;
    std::string               cmp_op_ = "gt";
    // case
    std::string               case_mode_ = "none";
};

#define OP_CONVERT_PRIMS(X)         \
    X("flowboard::Bool",   bool)          \
    X("flowboard::Char",   char)          \
    X("flowboard::UInt8",  std::uint8_t)  \
    X("flowboard::Int16",  std::int16_t)  \
    X("flowboard::UInt16", std::uint16_t) \
    X("flowboard::Int32",  std::int32_t)  \
    X("flowboard::UInt32", std::uint32_t) \
    X("flowboard::Int64",  std::int64_t)  \
    X("flowboard::UInt64", std::uint64_t) \
    X("flowboard::Float",  float)         \
    X("flowboard::Double", double)        \
    X("flowboard::String", std::string)

template <class TIn>
std::unique_ptr<Node> make_for_in(std::string id, nlohmann::json const& cfg, std::string const& outTag) {
#define X(tag, T) if (outTag == tag) return std::make_unique<ConvertNode<TIn, T>>(std::move(id), cfg);
    OP_CONVERT_PRIMS(X)
#undef X
    throw std::runtime_error("Transform.Convert: unsupported outputType: " + outTag);
}

std::unique_ptr<Node> convert_factory(std::string id, nlohmann::json const& cfg) {
    auto inTag  = cfg.value("inputType",  std::string{"flowboard::Double"});
    auto outTag = cfg.value("outputType", std::string{"flowboard::String"});
#define X(tag, T) if (inTag == tag) return make_for_in<T>(std::move(id), cfg, outTag);
    OP_CONVERT_PRIMS(X)
#undef X
    throw std::runtime_error("Transform.Convert: unsupported inputType: " + inTag);
}

constexpr char const* kConvertSchema = R"JSON({
  "$schema":"http://json-schema.org/draft-07/schema#",
  "type":"object",
  "required":["inputType","outputType"],
  "properties":{
    "inputType":{"type":"string","default":"flowboard::Double",
      "enum":["flowboard::Bool","flowboard::Char","flowboard::UInt8","flowboard::Int16","flowboard::UInt16","flowboard::Int32","flowboard::UInt32","flowboard::Int64","flowboard::UInt64","flowboard::Float","flowboard::Double","flowboard::String"]},
    "outputType":{"type":"string","default":"flowboard::String",
      "enum":["flowboard::Bool","flowboard::Char","flowboard::UInt8","flowboard::Int16","flowboard::UInt16","flowboard::Int32","flowboard::UInt32","flowboard::Int64","flowboard::UInt64","flowboard::Float","flowboard::Double","flowboard::String"]},
    "mode":{"type":"string","default":"convert",
      "enum":["convert","lookup","scale","threshold","boolmap","case"]},
    "template":{"type":"string","default":"{value}"},
    "decimals":{"type":"integer","default":6,"minimum":0,"maximum":15},
    "mappings":{"type":"array","items":{"type":"object","properties":{"when":{"type":"integer"},"value":{}}}},
    "fallback":{},
    "scale":{"type":"number","default":1},
    "offset":{"type":"number","default":0},
    "clamp":{"type":"boolean","default":false},
    "clampMin":{"type":"number"},
    "clampMax":{"type":"number"},
    "threshold":{"type":"number","default":0},
    "compareOp":{"type":"string","enum":["gt","gte","lt","lte","eq","neq"],"default":"gt"},
    "trueValue":{},
    "falseValue":{},
    "textTransform":{"type":"string","enum":["none","upper","lower","trim"],"default":"none"}
  },
  "additionalProperties":false
})JSON";
constexpr char const* kConvertDefaults =
    R"JSON({"inputType":"flowboard::Double","outputType":"flowboard::String","mode":"convert","template":"{value}"})JSON";

}  // namespace

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA("Transform.Convert", convert_factory, kConvertSchema, kConvertDefaults, convert)

}  // namespace flowboard::nodes
