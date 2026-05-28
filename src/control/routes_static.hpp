// SPDX-License-Identifier: MIT
#pragma once
#include <crow.h>

namespace flowboard::control {
/// Register GET / and GET /<path> routes that serve the embedded web bundle.
void register_static_routes(crow::SimpleApp& app);
}  // namespace flowboard::control
