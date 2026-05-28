// SPDX-License-Identifier: MIT
#pragma once
#include <string_view>

namespace flowboard::config {

// Returns the JSON Schema as a string literal — embedded at build time.
std::string_view graph_schema_json();

}  // namespace flowboard::config
