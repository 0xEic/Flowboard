// SPDX-License-Identifier: MIT
#include "flowboard/log.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace flowboard::log {

void init(spdlog::level::level_enum level) {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_pattern("%H:%M:%S.%e [%^%l%$] [%n] %v");
    auto logger = std::make_shared<spdlog::logger>("flowboard", sink);
    logger->set_level(level);
    spdlog::set_default_logger(logger);
}

}  // namespace flowboard::log
