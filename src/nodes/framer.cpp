// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/list_value.hpp"
#include "builtin_types.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace flowboard::nodes {

namespace {

std::vector<std::uint8_t> parse_delimiter(std::string const& s, bool is_hex) {
    std::vector<std::uint8_t> out;
    if (!is_hex) { out.assign(s.begin(), s.end()); return out; }
    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
        int h = hexval(s[i]), l = hexval(s[i+1]);
        if (h < 0 || l < 0) return {};
        out.push_back(static_cast<std::uint8_t>((h << 4) | l));
    }
    return out;
}

std::shared_ptr<ListValue> bytes_to_listvalue(std::uint8_t const* p, std::size_t n) {
    auto lv = std::make_shared<ListValue>();
    lv->element_type_tag = "flowboard::UInt8";
    lv->items.reserve(n);
    for (std::size_t i = 0; i < n; ++i) lv->items.push_back(p[i]);
    return lv;
}

}  // namespace

class FramerDelimiterNode : public Node {
public:
    FramerDelimiterNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Framer.Delimiter"),
          in_("in"), out_("out"),
          delim_(parse_delimiter(
                    cfg.value("delimiter", std::string{"\n"}),
                    cfg.value("delimiterIsHex", false))),
          include_(cfg.value("includeDelimiter", false)),
          max_frame_(cfg.value("maxFrameSize", std::size_t{65536})) {
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        acc_.clear();
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v]{ ingest(*v); });
        });
    }

    void on_stop() override { acc_.clear(); }

private:
    void ingest(ListValue const& lv) {
        for (auto const& j : lv.items)
            if (j.is_number_integer()) acc_.push_back(static_cast<std::uint8_t>(j.get<int>() & 0xFF));
        if (delim_.empty()) return;
        for (;;) {
            auto it = std::search(acc_.begin(), acc_.end(), delim_.begin(), delim_.end());
            if (it == acc_.end()) {
                if (acc_.size() > max_frame_) {
                    auto now = std::chrono::steady_clock::now();
                    if (now - last_warn_ > std::chrono::seconds(1)) {
                        spdlog::warn("[{}] Framer.Delimiter: dropping oversize frame ({} > {})",
                                     std::string(id()), acc_.size(), max_frame_);
                        last_warn_ = now;
                    }
                    acc_.clear();
                }
                return;
            }
            std::size_t frame_len = static_cast<std::size_t>(std::distance(acc_.begin(), it))
                                    + (include_ ? delim_.size() : 0);
            if (frame_len > max_frame_) {
                spdlog::warn("[{}] Framer.Delimiter: dropping oversize frame ({} > {})",
                             std::string(id()), frame_len, max_frame_);
            } else {
                out_.emit(bytes_to_listvalue(acc_.data(), frame_len));
            }
            acc_.erase(acc_.begin(), it + static_cast<std::ptrdiff_t>(delim_.size()));
        }
    }

    InputPort<ListValue>           in_;
    OutputPort<ListValue>          out_;
    std::vector<std::uint8_t>      delim_;
    bool                           include_;
    std::size_t                    max_frame_;
    std::vector<std::uint8_t>      acc_;
    std::chrono::steady_clock::time_point last_warn_{};
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Framer.Delimiter",
    FramerDelimiterNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["delimiter"],
      "properties":{
        "delimiter":{"type":"string","title":"Delimiter","default":"\n"},
        "delimiterIsHex":{"type":"boolean","title":"Delimiter is hex","default":false},
        "includeDelimiter":{"type":"boolean","title":"Include delimiter in frame","default":false},
        "maxFrameSize":{"type":"integer","title":"Max frame size","minimum":1,"default":65536}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"delimiter":"\n","delimiterIsHex":false,"includeDelimiter":false,"maxFrameSize":65536})JSON"
)

class FramerLengthPrefixNode : public Node {
public:
    FramerLengthPrefixNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Framer.LengthPrefix"),
          in_("in"), out_("out"),
          prefix_size_(cfg.value("prefixSize", std::size_t{2})),
          little_endian_(cfg.value("littleEndian", true)),
          includes_prefix_(cfg.value("lengthIncludesPrefix", false)),
          max_frame_(cfg.value("maxFrameSize", std::size_t{65536})) {
        if (prefix_size_ != 1 && prefix_size_ != 2 && prefix_size_ != 4)
            throw std::runtime_error("Framer.LengthPrefix: prefixSize must be 1, 2, or 4");
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        acc_.clear();
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v]{ ingest(*v); });
        });
    }
    void on_stop() override { acc_.clear(); }

private:
    std::size_t read_length() const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < prefix_size_; ++i) {
            std::size_t shift = little_endian_ ? (i * 8) : ((prefix_size_ - 1 - i) * 8);
            n |= static_cast<std::size_t>(acc_[i]) << shift;
        }
        return n;
    }

    void ingest(ListValue const& lv) {
        for (auto const& j : lv.items)
            if (j.is_number_integer()) acc_.push_back(static_cast<std::uint8_t>(j.get<int>() & 0xFF));
        for (;;) {
            if (acc_.size() < prefix_size_) return;
            std::size_t len = read_length();
            std::size_t total = includes_prefix_ ? len : (prefix_size_ + len);
            if (includes_prefix_ && len < prefix_size_) {
                spdlog::warn("[{}] Framer.LengthPrefix: invalid length {} (< prefix {})",
                             std::string(id()), len, prefix_size_);
                acc_.erase(acc_.begin(), acc_.begin() + 1);
                continue;
            }
            if (total > max_frame_) {
                spdlog::warn("[{}] Framer.LengthPrefix: dropping oversize frame ({} > {})",
                             std::string(id()), total, max_frame_);
                acc_.erase(acc_.begin(), acc_.begin() + 1);
                continue;
            }
            if (acc_.size() < total) return;
            std::size_t body_off = prefix_size_;
            std::size_t body_len = total - prefix_size_;
            out_.emit(bytes_to_listvalue(acc_.data() + body_off, body_len));
            acc_.erase(acc_.begin(), acc_.begin() + static_cast<std::ptrdiff_t>(total));
        }
    }

    InputPort<ListValue>     in_;
    OutputPort<ListValue>    out_;
    std::size_t              prefix_size_;
    bool                     little_endian_;
    bool                     includes_prefix_;
    std::size_t              max_frame_;
    std::vector<std::uint8_t> acc_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Framer.LengthPrefix",
    FramerLengthPrefixNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "prefixSize":{"type":"integer","title":"Prefix size (bytes)","enum":[1,2,4],"default":2},
        "littleEndian":{"type":"boolean","title":"Little-endian","default":true},
        "lengthIncludesPrefix":{"type":"boolean","title":"Length includes prefix","default":false},
        "maxFrameSize":{"type":"integer","title":"Max frame size","minimum":1,"default":65536}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"prefixSize":2,"littleEndian":true,"lengthIncludesPrefix":false,"maxFrameSize":65536})JSON"
)

class FramerFixedNode : public Node {
public:
    FramerFixedNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Framer.Fixed"),
          in_("in"), out_("out"),
          frame_size_(cfg.value("frameSize", std::size_t{1})) {
        if (frame_size_ == 0)
            throw std::runtime_error("Framer.Fixed: frameSize must be >= 1");
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        acc_.clear();
        in_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v]{ ingest(*v); });
        });
    }
    void on_stop() override { acc_.clear(); }

private:
    void ingest(ListValue const& lv) {
        for (auto const& j : lv.items)
            if (j.is_number_integer()) acc_.push_back(static_cast<std::uint8_t>(j.get<int>() & 0xFF));
        while (acc_.size() >= frame_size_) {
            out_.emit(bytes_to_listvalue(acc_.data(), frame_size_));
            acc_.erase(acc_.begin(), acc_.begin() + static_cast<std::ptrdiff_t>(frame_size_));
        }
    }
    InputPort<ListValue>      in_;
    OutputPort<ListValue>     out_;
    std::size_t               frame_size_;
    std::vector<std::uint8_t> acc_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Framer.Fixed",
    FramerFixedNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["frameSize"],
      "properties":{
        "frameSize":{"type":"integer","title":"Frame size (bytes)","minimum":1,"default":1}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"frameSize":1})JSON"
)

}  // namespace flowboard::nodes
