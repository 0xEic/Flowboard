// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <vector>
#include "flowboard/registry.hpp"

/// \file
/// \brief Loads node plugins (shared libraries) from a directory at startup.

namespace flowboard {

/// \brief Discovers and loads Flowboard node plugins.
///
/// Each loaded library is ABI-checked and its `flowboard_plugin_register` entry
/// point is called against the given NodeRegistry. Loaded libraries are kept
/// mapped for the lifetime of this object (and, in practice, the process): node
/// factories registered by a plugin point into its code, so unloading early
/// would dangle them.
class PluginHost {
public:
    PluginHost() = default;
    ~PluginHost();
    PluginHost(PluginHost const&) = delete;
    PluginHost& operator=(PluginHost const&) = delete;

    /// \brief Load every plugin shared library directly inside \p dir.
    ///
    /// A non-existent directory is not an error (returns 0). Libraries that
    /// fail to load, mismatch the ABI version, or lack the entry point are
    /// skipped with a warning.
    /// \param dir      Directory to scan (non-recursive).
    /// \param registry Registry the plugins add their node types to.
    /// \return Number of plugins successfully loaded.
    int load_directory(std::filesystem::path const& dir,
                       NodeRegistry& registry = NodeRegistry::instance());

private:
    std::vector<void*> handles_;  ///< Native library handles, kept open.
};

}  // namespace flowboard
