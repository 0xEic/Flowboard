// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <limits>
#include <memory>
#include <semaphore>
#include <thread>
#include <rigtorp/SPSCQueue.h>

/// \file
/// \brief Bounded lock-free SPSC edge connecting one output port to one input.

namespace flowboard {

/// \brief What an Edge does when its ring is full.
enum class BackpressurePolicy {
    Block,      ///< Producer spins/yields until space frees up; never drops.
    DropOldest  ///< Drop the oldest queued message to admit the newest; lossy.
};

/// \brief A bounded single-producer/single-consumer message ring.
///
/// Wraps a lock-free rigtorp::SPSCQueue. Exactly one producer (the upstream
/// OutputPort sink) calls push(), and exactly one consumer (the edge's pump
/// thread) calls try_pop(); violating that invariant breaks the SPSC contract.
/// See \ref adr_backpressure for the policy rationale.
/// \tparam T The message type carried across the edge.
template <typename T>
class Edge {
public:
    /// \brief Immutable, shareable message value type.
    using Value = std::shared_ptr<const T>;

    /// \brief Semaphore type used to wake a downstream consumer on push.
    using NodeEvent = std::counting_semaphore<std::numeric_limits<std::ptrdiff_t>::max()>;

    /// \brief Construct an edge.
    /// \param capacity         Ring capacity (number of buffered messages).
    /// \param policy           Backpressure behavior when the ring is full.
    /// \param downstream_event Optional semaphore released on each push to wake
    ///                         the consumer; may be `nullptr`.
    Edge(std::size_t capacity, BackpressurePolicy policy,
         NodeEvent* downstream_event)
        : queue_(capacity), policy_(policy), event_(downstream_event) {}

    /// \brief Enqueue a value (called by the producing OutputPort sink).
    ///
    /// Under BackpressurePolicy::Block this spins until space is available;
    /// under BackpressurePolicy::DropOldest it evicts the oldest message and
    /// increments the dropped() counter.
    void push(Value v) {
        if (policy_ == BackpressurePolicy::Block) {
            while (!queue_.try_push(v)) std::this_thread::yield();
        } else {
            // DropOldest: try once; on failure, pop the oldest and retry.
            if (!queue_.try_push(v)) {
                Value discarded;
                queue_.front();   // ensure front exists before pop
                queue_.pop();
                ++dropped_;
                queue_.try_push(v);
            }
        }
        pushed_.fetch_add(1, std::memory_order_relaxed);
        if (event_) event_->release();
    }

    /// \brief Dequeue the oldest value if present (called by the pump thread).
    /// \param out Receives the popped value on success.
    /// \return `true` if a value was dequeued, `false` if the ring was empty.
    bool try_pop(Value& out) {
        if (auto* front = queue_.front()) {
            out = std::move(*front);
            queue_.pop();
            return true;
        }
        return false;
    }

    /// \brief Increment the pushed counter only (when delivery bypasses the ring).
    void record_push() { pushed_.fetch_add(1, std::memory_order_relaxed); }

    /// \brief Total messages pushed onto this edge.
    std::size_t pushed()  const { return pushed_.load(std::memory_order_relaxed); }
    /// \brief Total messages dropped under BackpressurePolicy::DropOldest.
    std::size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    rigtorp::SPSCQueue<Value> queue_;
    BackpressurePolicy policy_;
    NodeEvent* event_;
    std::atomic<std::size_t> pushed_{0};
    std::atomic<std::size_t> dropped_{0};
};

}  // namespace flowboard
