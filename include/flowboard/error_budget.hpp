// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <deque>
#include <mutex>

namespace flowboard {

/// \file
/// \brief Sliding-window failure counter that flags when failures exceed a limit within a time window.

/// \brief Thread-safe sliding-window failure counter that signals when the in-window failure count reaches a limit.
class ErrorBudget {
public:
    using clock      = std::chrono::steady_clock;
    using time_point = clock::time_point;

    ErrorBudget(std::size_t limit, std::chrono::milliseconds window)
        : limit_(limit), window_(window) {}

    /// \brief Record a failure. Return true iff the recorded failure caused
    /// the in-window count to reach `limit`.
    bool note_failure_and_check_exceeded() {
        std::scoped_lock lock(mu_);
        auto now = clock::now();
        while (!recent_.empty() && now - recent_.front() > window_)
            recent_.pop_front();
        recent_.push_back(now);
        return recent_.size() >= limit_;
    }

    /// \brief Record a failure without inspecting whether the limit was reached.
    void note_failure() {
        (void)note_failure_and_check_exceeded();
    }

    /// \brief Pure inspection (no side effect).
    bool exceeded() const {
        std::scoped_lock lock(mu_);
        auto now = clock::now();
        std::size_t count = 0;
        for (auto t : recent_) if (now - t <= window_) ++count;
        return count >= limit_;
    }

private:
    std::size_t limit_;
    std::chrono::milliseconds window_;
    mutable std::mutex mu_;
    std::deque<time_point> recent_;
};

}  // namespace flowboard
