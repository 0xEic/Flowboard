// SPDX-License-Identifier: MIT
#include "flowboard/plugin_host.hpp"
#include "flowboard/plugin.hpp"
#include "flowboard/can_adapter.hpp"
#include "flowboard/log.hpp"

#include <spdlog/spdlog.h>
#include <system_error>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace flowboard {
namespace {

#if defined(_WIN32)
constexpr char const* kPluginExt = ".dll";

void* lib_open(fs::path const& p) {
    return reinterpret_cast<void*>(::LoadLibraryW(p.wstring().c_str()));
}
void* lib_symbol(void* h, char const* name) {
    return reinterpret_cast<void*>(::GetProcAddress(reinterpret_cast<HMODULE>(h), name));
}
void lib_close(void* h) { ::FreeLibrary(reinterpret_cast<HMODULE>(h)); }
std::string lib_error() { return "error code " + std::to_string(::GetLastError()); }

#else  // POSIX
#  if defined(__APPLE__)
constexpr char const* kPluginExt = ".dylib";
#  else
constexpr char const* kPluginExt = ".so";
#  endif

void* lib_open(fs::path const& p) { return ::dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* lib_symbol(void* h, char const* name) { return ::dlsym(h, name); }
void lib_close(void* h) { ::dlclose(h); }
std::string lib_error() { char const* e = ::dlerror(); return e ? e : "unknown error"; }
#endif

using abi_version_fn = int (*)();
using register_fn    = void (*)(NodeRegistry&);
using name_fn        = char const* (*)();

template <typename Fn>
Fn resolve(void* handle, char const* name) {
    return reinterpret_cast<Fn>(lib_symbol(handle, name));
}

}  // namespace

PluginHost::~PluginHost() {
    // Deliberately do NOT close the libraries: the registry still owns node
    // factories whose code lives inside them, and those factories outlive this
    // object (the registry is a static singleton). Letting the OS reclaim the
    // mappings at process exit is safer than risking a dangling call.
}

int PluginHost::load_directory(fs::path const& dir, NodeRegistry& registry) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        spdlog::info("plugins: directory {} not present — skipping", dir.string());
        return 0;
    }
    if (!fs::is_directory(dir, ec)) {
        spdlog::warn("plugins: {} is not a directory — skipping", dir.string());
        return 0;
    }

    int loaded = 0;
    for (auto const& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != kPluginExt) continue;

        fs::path const& path = entry.path();
        void* handle = lib_open(path);
        if (!handle) {
            spdlog::warn("plugins: failed to load {} ({})", path.string(), lib_error());
            continue;
        }

        auto abi = resolve<abi_version_fn>(handle, "flowboard_plugin_abi_version");
        if (!abi) {
            spdlog::warn("plugins: {} has no flowboard_plugin_abi_version — not a plugin, skipping",
                         path.filename().string());
            lib_close(handle);
            continue;
        }
        int const plugin_abi = abi();
        if (plugin_abi > FLOWBOARD_PLUGIN_ABI_VERSION) {
            spdlog::warn("plugins: {} requires ABI v{} but host supports v{} — skipping",
                         path.filename().string(), plugin_abi, FLOWBOARD_PLUGIN_ABI_VERSION);
            lib_close(handle);
            continue;
        }

        auto reg = resolve<register_fn>(handle, "flowboard_plugin_register");
        if (!reg) {
            spdlog::warn("plugins: {} has no flowboard_plugin_register — skipping",
                         path.filename().string());
            lib_close(handle);
            continue;
        }

        std::string plugin_label = path.filename().string();
        if (auto nm = resolve<name_fn>(handle, "flowboard_plugin_name")) {
            if (char const* n = nm()) plugin_label = n;
        }

        std::size_t const before = registry.registered_names().size();
        try {
            reg(registry);
        } catch (std::exception const& e) {
            spdlog::error("plugins: '{}' threw during registration: {}", plugin_label, e.what());
            handles_.push_back(handle);  // keep loaded; some nodes may be registered
            continue;
        }
        std::size_t const added = registry.registered_names().size() - before;

        // Optional ABI-2+ entry point: register CAN adapters. The plugin
        // receives a CanAdapterRegistry reference that vtable-dispatches into
        // the host's process-wide registry — sidesteps the cross-DLL
        // singleton dup problem that a free-function ABI would have.
        if (plugin_abi >= 2) {
            struct HostRegistrar : ::flowboard::CanAdapterRegistry {
                void add(std::unique_ptr<::flowboard::ICanAdapter> a) override {
                    ::flowboard::register_can_adapter(std::move(a));
                }
            } registrar;
            using can_reg_fn = void (*)(::flowboard::CanAdapterRegistry&);
            if (auto can_reg = resolve<can_reg_fn>(handle, "flowboard_plugin_register_can_adapter")) {
                try { can_reg(registrar); }
                catch (std::exception const& e) {
                    spdlog::error("plugins: '{}' CAN-adapter registration threw: {}",
                                  plugin_label, e.what());
                }
            }
        }

        handles_.push_back(handle);
        ++loaded;
        spdlog::info("plugins: loaded '{}' from {} ({} node type{})",
                     plugin_label, path.filename().string(), added, added == 1 ? "" : "s");
    }

    if (loaded > 0) spdlog::info("plugins: {} plugin(s) loaded from {}", loaded, dir.string());
    return loaded;
}

}  // namespace flowboard
