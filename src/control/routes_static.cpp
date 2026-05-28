// SPDX-License-Identifier: MIT
#include "routes_static.hpp"
#include <crow.h>
#include <string>

namespace flowboard::control {

// Declared here; defined in the generated TU (web_assets.cpp).
std::string const* web_asset(std::string const& path);

static std::string content_type_for(std::string const& path) {
    auto ends = [&](char const* suf) {
        std::string s{suf};
        return path.size() >= s.size() &&
               path.compare(path.size() - s.size(), s.size(), s) == 0;
    };
    if (ends(".html"))  return "text/html; charset=utf-8";
    if (ends(".js"))    return "application/javascript; charset=utf-8";
    if (ends(".mjs"))   return "application/javascript; charset=utf-8";
    if (ends(".css"))   return "text/css; charset=utf-8";
    if (ends(".svg"))   return "image/svg+xml";
    if (ends(".png"))   return "image/png";
    if (ends(".jpg"))   return "image/jpeg";
    if (ends(".jpeg"))  return "image/jpeg";
    if (ends(".woff2")) return "font/woff2";
    if (ends(".woff"))  return "font/woff";
    if (ends(".json"))  return "application/json; charset=utf-8";
    if (ends(".ico"))   return "image/x-icon";
    return "application/octet-stream";
}

void register_static_routes(crow::SimpleApp& app) {
    // Serve index.html at root.
    CROW_ROUTE(app, "/")([] {
        auto* p = web_asset("/index.html");
        if (!p) {
            return crow::response{404, "text/plain",
                                  std::string{"web bundle not embedded"}};
        }
        return crow::response{200, "text/html; charset=utf-8", *p};
    });

    // Serve all other asset paths.
    // Crow routes /api/* and /ws before this catch-all because they are
    // registered first in server.cpp.
    CROW_ROUTE(app, "/<path>")([](std::string const& path) {
        auto* p = web_asset("/" + path);
        if (!p) return crow::response{404};
        return crow::response{200, content_type_for(path), *p};
    });
}

}  // namespace flowboard::control
