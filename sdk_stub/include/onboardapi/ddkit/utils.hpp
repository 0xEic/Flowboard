// SPDX-License-Identifier: MIT
#pragma once
#include <optional>
#include <utility>

namespace ddkit::utils::types {

// Mimics the real SDK's Optional<T> shape. Used by some generated code paths.
template <typename T>
class Optional {
public:
    Optional() = default;
    explicit Optional(T v) : v_{std::move(v)} {}

    bool has_value() const { return v_.has_value(); }
    T const& value() const { return *v_; }
    T value_or(T d) const { return v_.value_or(std::move(d)); }
    void set(T v) { v_ = std::move(v); }
    void reset() { v_.reset(); }

private:
    std::optional<T> v_;
};

}  // namespace ddkit::utils::types
