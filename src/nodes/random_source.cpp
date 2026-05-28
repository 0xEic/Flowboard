// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

namespace flowboard::nodes {

// Sources.Random — emits a fresh random value of the configured primitive type
// each time `trigger`=true (and once on init when autoTriggerOnInit is set).
// The per-type generator is built from config in the factory below; this node
// just owns the RNG and the trigger plumbing. All emissions run on the node's
// single worker thread, so the RNG needs no locking.
template <typename T>
class RandomT final : public Node {
public:
    using Gen = std::function<T(std::mt19937&)>;
    RandomT(std::string id, Gen gen, bool auto_trigger)
        : Node(std::move(id), "Sources.Random"),
          trig_("trigger"), out_("out"),
          gen_(std::move(gen)), auto_trigger_(auto_trigger),
          rng_(std::random_device{}()) {
        register_input(&trig_);
        register_output(&out_);
    }
    void on_start() override {
        trig_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v && *v) emit_one(); });
        });
        if (auto_trigger_) enqueue([this] { emit_one(); });
    }

private:
    void emit_one() { out_.emit(std::make_shared<const T>(gen_(rng_))); }

    InputPort<bool> trig_;
    OutputPort<T>   out_;
    Gen             gen_;
    bool            auto_trigger_;
    std::mt19937    rng_;
};

namespace {

// [min,max] from config, ordered, defaulting to 0..100.
std::pair<double, double> range_of(nlohmann::json const& cfg) {
    double lo = cfg.value("min", 0.0);
    double hi = cfg.value("max", 100.0);
    if (lo > hi) std::swap(lo, hi);
    return {lo, hi};
}

std::string alphabet_of(nlohmann::json const& cfg) {
    std::string a;
    if (cfg.value("allowLowercase", true))  a += "abcdefghijklmnopqrstuvwxyz";
    if (cfg.value("allowUppercase", false)) a += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (cfg.value("allowDigits", false))    a += "0123456789";
    if (cfg.value("allowSymbols", false))   a += "!@#$%^&*()-_=+[]{};:,.<>?/";
    if (a.empty()) a = "abcdefghijklmnopqrstuvwxyz";  // never produce empty alphabet
    return a;
}

// Generator for a >=16-bit integer type (the set std::uniform_int_distribution
// accepts). The range is clamped to T's representable interval.
template <typename T>
std::function<T(std::mt19937&)> int_gen(nlohmann::json const& cfg) {
    auto [lo, hi] = range_of(cfg);
    lo = std::max(lo, static_cast<double>(std::numeric_limits<T>::min()));
    hi = std::min(hi, static_cast<double>(std::numeric_limits<T>::max()));
    T a = static_cast<T>(std::nearbyint(lo));
    T b = static_cast<T>(std::nearbyint(hi));
    if (a > b) std::swap(a, b);
    return [a, b](std::mt19937& rng) {
        std::uniform_int_distribution<T> d(a, b);
        return d(rng);
    };
}

// 8-bit ints / char: uniform_int_distribution is undefined for these, so draw
// in a wider type and cast.
template <typename T>
std::function<T(std::mt19937&)> small_int_gen(nlohmann::json const& cfg) {
    auto [lo, hi] = range_of(cfg);
    lo = std::max(lo, static_cast<double>(std::numeric_limits<T>::min()));
    hi = std::min(hi, static_cast<double>(std::numeric_limits<T>::max()));
    int a = static_cast<int>(std::nearbyint(lo));
    int b = static_cast<int>(std::nearbyint(hi));
    if (a > b) std::swap(a, b);
    return [a, b](std::mt19937& rng) {
        std::uniform_int_distribution<int> d(a, b);
        return static_cast<T>(d(rng));
    };
}

template <typename T>
std::function<T(std::mt19937&)> real_gen(nlohmann::json const& cfg) {
    auto [lo, hi] = range_of(cfg);
    return [lo, hi](std::mt19937& rng) {
        std::uniform_real_distribution<double> d(lo, hi);
        return static_cast<T>(d(rng));
    };
}

std::unique_ptr<Node> make_string(std::string id, nlohmann::json const& cfg, bool autotrig) {
    std::string alphabet = alphabet_of(cfg);
    int len = std::max(0, cfg.value("length", 8));
    auto gen = [alphabet, len](std::mt19937& rng) {
        std::uniform_int_distribution<std::size_t> d(0, alphabet.size() - 1);
        std::string s;
        s.reserve(static_cast<std::size_t>(len));
        for (int i = 0; i < len; ++i) s += alphabet[d(rng)];
        return s;
    };
    return std::make_unique<RandomT<std::string>>(std::move(id), std::move(gen), autotrig);
}

std::unique_ptr<Node> make_char(std::string id, nlohmann::json const& cfg, bool autotrig) {
    std::string alphabet = alphabet_of(cfg);
    auto gen = [alphabet](std::mt19937& rng) {
        std::uniform_int_distribution<std::size_t> d(0, alphabet.size() - 1);
        return alphabet[d(rng)];
    };
    return std::make_unique<RandomT<char>>(std::move(id), std::move(gen), autotrig);
}

std::unique_ptr<Node> make_bool(std::string id, nlohmann::json const& cfg, bool autotrig) {
    double p = std::clamp(cfg.value("trueProbability", 0.5), 0.0, 1.0);
    auto gen = [p](std::mt19937& rng) {
        std::bernoulli_distribution d(p);
        return d(rng);
    };
    return std::make_unique<RandomT<bool>>(std::move(id), std::move(gen), autotrig);
}

template <typename T, typename GenFn>
std::unique_ptr<Node> make(std::string id, nlohmann::json const& cfg, bool autotrig, GenFn gen_builder) {
    return std::make_unique<RandomT<T>>(std::move(id), gen_builder(cfg), autotrig);
}

}  // namespace

static auto _random_factory = [](std::string id, nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("outputType", std::string{"flowboard::Double"});
    bool autotrig = cfg.value("autoTriggerOnInit", true);

    if (t == "flowboard::Bool")   return make_bool(std::move(id), cfg, autotrig);
    if (t == "flowboard::String") return make_string(std::move(id), cfg, autotrig);
    if (t == "flowboard::Char")   return make_char(std::move(id), cfg, autotrig);
    if (t == "flowboard::Double") return make<double>      (std::move(id), cfg, autotrig, real_gen<double>);
    if (t == "flowboard::Float")  return make<float>       (std::move(id), cfg, autotrig, real_gen<float>);
    if (t == "flowboard::Int16")  return make<std::int16_t> (std::move(id), cfg, autotrig, int_gen<std::int16_t>);
    if (t == "flowboard::UInt16") return make<std::uint16_t>(std::move(id), cfg, autotrig, int_gen<std::uint16_t>);
    if (t == "flowboard::Int32")  return make<std::int32_t> (std::move(id), cfg, autotrig, int_gen<std::int32_t>);
    if (t == "flowboard::UInt32") return make<std::uint32_t>(std::move(id), cfg, autotrig, int_gen<std::uint32_t>);
    if (t == "flowboard::Int64")  return make<std::int64_t> (std::move(id), cfg, autotrig, int_gen<std::int64_t>);
    if (t == "flowboard::UInt64") return make<std::uint64_t>(std::move(id), cfg, autotrig, int_gen<std::uint64_t>);
    if (t == "flowboard::UInt8")  return make<std::uint8_t> (std::move(id), cfg, autotrig, small_int_gen<std::uint8_t>);
    throw std::runtime_error("Sources.Random: unsupported outputType '" + t + "'");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Sources.Random",
    _random_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"Random",
      "description":"Emits a random value of the chosen primitive type on each trigger. Number fields apply to numeric types; the letter-count + character-class flags apply to String and Char; trueProbability applies to Bool.",
      "required":["outputType"],
      "properties":{
        "outputType":{"type":"string","title":"Output type",
          "enum":["flowboard::Bool","flowboard::Int64","flowboard::Double","flowboard::String","flowboard::Float","flowboard::UInt64","flowboard::Int32","flowboard::UInt32","flowboard::Int16","flowboard::UInt16","flowboard::UInt8","flowboard::Char"],
          "default":"flowboard::Double"},
        "min":{"type":"number","title":"Min (numbers)","default":0,
          "description":"Lower bound for numeric types (inclusive). Clamped to the type's range."},
        "max":{"type":"number","title":"Max (numbers)","default":100,
          "description":"Upper bound for numeric types (inclusive). Clamped to the type's range."},
        "trueProbability":{"type":"number","title":"P(true) (Bool)","minimum":0,"maximum":1,"default":0.5,
          "description":"Probability of emitting true for the Bool type."},
        "length":{"type":"integer","title":"Letter count (String)","minimum":0,"default":8,
          "description":"Number of characters in a random String."},
        "allowLowercase":{"type":"boolean","title":"Allow lowercase","default":true},
        "allowUppercase":{"type":"boolean","title":"Allow uppercase","default":false},
        "allowDigits":{"type":"boolean","title":"Allow digits","default":false},
        "allowSymbols":{"type":"boolean","title":"Allow symbols","default":false},
        "autoTriggerOnInit":{"type":"boolean","title":"Auto-trigger on init","default":true,
          "description":"Emit one random value when the graph starts (in addition to on every trigger=true)."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"outputType":"flowboard::Double","min":0,"max":100,"trueProbability":0.5,"length":8,"allowLowercase":true,"allowUppercase":false,"allowDigits":false,"allowSymbols":false,"autoTriggerOnInit":true})JSON",
    random_source
)

}  // namespace flowboard::nodes
