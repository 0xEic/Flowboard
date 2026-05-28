// SPDX-License-Identifier: MIT
#pragma once
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

#include "flowboard/json_helpers.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"

namespace flowboard {

/// \file
/// \brief Node that reassembles Key/Value/IsRemoved port trios into a live flowboard::ListValue snapshot.

/// \brief Generic Key/Value accumulator. Consumes the three-port pattern produced by
/// every OnboardAPI Report*-style callback (Key, Value, IsRemoved) and emits a
/// `ListValue` snapshot of the live (non-removed) entries on every commit.
///
/// Correlation semantics. A generated `Report*` callback always passes all three
/// of its params on every call, so the node emits Key, Value and IsRemoved at a
/// strict 1:1:1 ratio (on upserts *and* removals). They fan out across three
/// independent SPSC edges, each drained by its own pump thread, so cross-port
/// arrival order at this accumulator is NOT preserved — the i-th Value may land
/// before or after the i-th Key. But each edge is individually FIFO, so the i-th
/// item on every port belongs to the same Report call. We therefore buffer each
/// port in its own FIFO deque (touched only on the worker thread) and, whenever
/// all three have a pending item, pop one from each to reassemble the trio and
/// commit it. This is robust to any cross-edge interleaving — in particular to
/// the bursts produced when a coalescing Report drain emits many keys at once.
///
/// Map order is preserved by insertion (std::map keyed on JSON-stringified key
/// for stable ordering; configurable to key-sorted via the `orderBy` config).
template <typename TKey, typename TValue>
class KeyValueAccumulator final : public Node {
public:
    KeyValueAccumulator(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.KeyValueAccumulator"),
          key_("key"),
          value_("value"),
          is_removed_("isRemoved"),
          list_("list"),
          element_type_tag_(cfg.value("elementTypeTag", std::string{})),
          order_by_key_(cfg.value("orderBy", std::string{"insertion"}) == "key"),
          emit_on_empty_(cfg.value("emitOnEmpty", true)) {
        register_input(&key_);
        register_input(&value_);
        register_input(&is_removed_);
        register_output(&list_);
    }

    void on_start() override {
        // Each sink runs on this node's single worker thread, so the three
        // deques are only ever touched there — no lock needed. try_commit()
        // reassembles complete trios by per-port FIFO position.
        key_.set_internal_sink([this](typename InputPort<TKey>::Value v) {
            enqueue([this, v] { keys_.push_back(v); try_commit(); });
        });
        value_.set_internal_sink([this](typename InputPort<TValue>::Value v) {
            enqueue([this, v] { values_.push_back(v); try_commit(); });
        });
        is_removed_.set_internal_sink([this](InputPort<bool>::Value flag) {
            enqueue([this, flag] { removed_.push_back(flag && *flag); try_commit(); });
        });
    }

private:
    // Reassemble and commit every complete (key, value, isRemoved) trio that has
    // arrived across the three FIFO ports.
    void try_commit() {
        while (!keys_.empty() && !values_.empty() && !removed_.empty()) {
            auto k = std::move(keys_.front());     keys_.pop_front();
            auto v = std::move(values_.front());   values_.pop_front();
            bool is_removed = removed_.front();    removed_.pop_front();
            commit(std::move(k), std::move(v), is_removed);
        }
    }

    void commit(std::shared_ptr<const TKey> k, std::shared_ptr<const TValue> v,
                bool is_removed) {
        if (!k) return;  // Defensive: a trio always carries a key.

        auto key_json = ::flowboard::to_json_or_passthrough(*k);
        std::string key_str = key_json.is_string() ? key_json.get<std::string>()
                                                   : key_json.dump();

        if (is_removed) {
            if (entries_.erase(key_str) == 0 && !emit_on_empty_) return;
        } else {
            if (!v) return;  // Upsert needs a value.
            auto value_json = ::flowboard::to_json_or_passthrough(*v);
            auto it = entries_.find(key_str);
            if (it != entries_.end()) {
                it->second.value = std::move(value_json);
            } else {
                entries_.emplace(key_str, Entry{key_json, std::move(value_json),
                                                next_order_++});
            }
        }

        auto snapshot = std::make_shared<ListValue>();
        snapshot->element_type_tag = element_type_tag_;
        snapshot->items.reserve(entries_.size());
        std::vector<Entry const*> ordered;
        ordered.reserve(entries_.size());
        for (auto const& [_, e] : entries_) ordered.push_back(&e);
        if (order_by_key_) {
            std::sort(ordered.begin(), ordered.end(),
                      [](Entry const* a, Entry const* b) { return a->key < b->key; });
        } else {
            std::sort(ordered.begin(), ordered.end(),
                      [](Entry const* a, Entry const* b) { return a->order < b->order; });
        }
        for (Entry const* e : ordered) {
            ::nlohmann::json item = ::nlohmann::json::object();
            item["key"]   = e->key;
            item["value"] = e->value;
            snapshot->items.push_back(std::move(item));
        }
        list_.emit(std::move(snapshot));
    }

    struct Entry {
        ::nlohmann::json key;
        ::nlohmann::json value;
        std::size_t      order;
    };

    InputPort<TKey>            key_;
    InputPort<TValue>          value_;
    InputPort<bool>            is_removed_;
    OutputPort<ListValue>      list_;

    std::string                element_type_tag_;
    bool                       order_by_key_;
    bool                       emit_on_empty_;

    // All four touched only on the worker thread (sink tasks + commit) → no lock.
    std::deque<std::shared_ptr<const TKey>>   keys_;
    std::deque<std::shared_ptr<const TValue>> values_;
    std::deque<bool>                          removed_;

    std::map<std::string, Entry>            entries_;
    std::size_t                             next_order_{0};
};

}  // namespace flowboard
