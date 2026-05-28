// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace flowboard {

/// \file
/// \brief Latest-action-per-key mailbox and composite-key stringification helper for coalesce-to-newest semantics.

/// \brief Stringify one composite-key component. Works for enums (incl. enum class),
/// arithmetic types, std::string, and anything streamable to ostream.
template <class T>
inline std::string coalesce_key_part(T const& v) {
    if constexpr (std::is_enum_v<T>)
        return std::to_string(static_cast<long long>(v));
    else if constexpr (std::is_arithmetic_v<T>)
        return std::to_string(v);
    else {
        std::ostringstream os;
        os << v;
        return os.str();
    }
}

/// \brief Latest-action-per-key mailbox for "coalesce to newest" semantics.
/// Producers call put() with a key + action (replacing any prior action for the
/// same key, so intermediate values are dropped). put() returns true exactly
/// when the mailbox was idle (caller should schedule one drain). drain() runs on
/// the consumer's single worker thread and executes each key's latest action.
class CoalescingMailbox {
public:
    /// \brief Store the latest action for a key; returns true iff the mailbox was idle and a drain should be scheduled.
    bool put(std::string key, std::function<void()> action) {
        std::scoped_lock lock(mu_);
        pending_[std::move(key)] = std::move(action);
        if (scheduled_) return false;
        scheduled_ = true;
        return true;
    }

    /// \brief Execute each key's latest pending action on the consumer's worker thread.
    void drain() {
        std::map<std::string, std::function<void()>> batch;
        {
            std::scoped_lock lock(mu_);
            batch.swap(pending_);
            scheduled_ = false;
        }
        for (auto& [key, action] : batch) action();
    }

private:
    std::mutex mu_;
    std::map<std::string, std::function<void()>> pending_;
    bool scheduled_ = false;
};

}  // namespace flowboard
