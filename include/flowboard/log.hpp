// SPDX-License-Identifier: MIT
#pragma once
#include <spdlog/spdlog.h>

/// \file
/// \brief Thin wrapper over spdlog for initializing and accessing the default logger.

namespace flowboard::log {

/// \brief Initialize the default logger at the given severity level.
void init(spdlog::level::level_enum level = spdlog::level::info);

/// \brief Access the default spdlog logger.
inline spdlog::logger& get() { return *spdlog::default_logger(); }

}  // namespace flowboard::log
