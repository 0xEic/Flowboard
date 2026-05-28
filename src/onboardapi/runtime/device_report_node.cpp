// SPDX-License-Identifier: MIT
// OnboardApi.DeviceReport — a port-less node that, while the graph runs, opens
// an OnboardAPI Device service and publishes this application's version + build
// identity and current runtime status. Placing it on the canvas is the whole
// interaction: there are no ports to wire.
//
// The live Device service is real-SDK only (the stub SDK's Service API differs
// and carries no DDS transport), mirroring live_discovery.cpp. Under the stub
// build the node still registers — so graphs load and it appears in the palette
// — but logs the assembled report instead of publishing it.
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "flowboard/app_info.hpp"
#include "flowboard/log.hpp"
#include "flowboard/node.hpp"
#include "flowboard/registry.hpp"
#include "version_info.hpp"

#if defined(FLOWBOARD_REAL_SDK)
#  include <optional>
#  include "flowboard/onboardapi_discovery.hpp"
#  include <onboardapi/api/Device.hpp>
#  include <onboardapi/type/Device.hpp>
#  include <onboardapi/type/Common.hpp>
#endif

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace flowboard::nodes {

namespace {

std::string host_name() {
#if defined(_WIN32)
    char buf[256];
    DWORD n = sizeof(buf);
    if (GetComputerNameA(buf, &n)) return std::string(buf, n);
    return "unknown";
#else
    char buf[256];
    if (::gethostname(buf, sizeof(buf)) == 0) return std::string(buf);
    return "unknown";
#endif
}

long process_id() {
#if defined(_WIN32)
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(::getpid());
#endif
}

const char* os_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

}  // namespace

class DeviceReportNode final : public Node
#if defined(FLOWBOARD_REAL_SDK)
                             , public ::M_Device::IService
#endif
{
public:
    DeviceReportNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "OnboardApi.DeviceReport"),
          domain_id_(cfg.value("domainId", 1)),
          service_name_(cfg.value("serviceName", std::string{"Flowboard"})),
          device_name_(cfg.value("deviceName", std::string{"Flowboard"})),
          description_(cfg.value("description", std::string{"Flowboard workflow engine"})),
          heartbeat_sec_(cfg.value("heartbeatSec", 5)),
          is_enabled_(true) {}

    // The collected facts shared by both builds (version/build, dependencies,
    // runtime, process). Key/value strings; the real-SDK build maps these to
    // PropertyType AdditionalInfo, the stub build logs them.
    std::vector<std::pair<std::string, std::string>> collect_facts() const {
        std::vector<std::pair<std::string, std::string>> f;
        auto add = [&](std::string k, std::string v) { f.emplace_back(std::move(k), std::move(v)); };

        add("version", FLOWBOARD_VERSION_STRING);
        add("git.commit", FLOWBOARD_GIT_COMMIT);
        add("git.commitShort", FLOWBOARD_GIT_COMMIT_SHORT);
        add("git.branch", FLOWBOARD_GIT_BRANCH);
        add("git.describe", FLOWBOARD_GIT_DESCRIBE);
        add("git.dirty", FLOWBOARD_GIT_DIRTY ? "true" : "false");
        add("build.date", FLOWBOARD_BUILD_DATE);
        add("build.type", FLOWBOARD_BUILD_TYPE);
        add("build.compiler", FLOWBOARD_COMPILER);
        add("sdk.mode", FLOWBOARD_SDK_MODE);

        for (auto const& d : kDependencies) add(std::string("dep.") + d.name, d.version);

        auto snap = AppInfo::instance().get();
        add("workflow.file", snap.workflow_file);
        if (!snap.workflow_name.empty())        add("workflow.name", snap.workflow_name);
        if (!snap.workflow_description.empty())  add("workflow.description", snap.workflow_description);
        add("workflow.nodeCount", std::to_string(snap.node_count));
        add("workflow.edgeCount", std::to_string(snap.edge_count));
        add("control.enabled", snap.control_enabled ? "true" : "false");
        if (snap.control_enabled) {
            add("control.host", snap.bind_host);
            add("control.port", std::to_string(snap.control_port));
        }
        add("uptime.seconds", std::to_string(AppInfo::instance().uptime_seconds()));

        add("process.pid", std::to_string(process_id()));
        add("process.host", host_name());
        add("process.os", os_name());

        add("dds.domain", std::to_string(domain_id_));
        add("service.name", service_name_);
        add("device.name", device_name_);
        return f;
    }

#if defined(FLOWBOARD_REAL_SDK)
    void on_start() override {
        disco_token_ = OnboardApiDiscovery::instance().open(
            domain_id_, "M_Device", "Service", service_name_);
        handle_.emplace(::M_Device::Service::create<int, DeviceReportNode>(
            domain_id_, service_name_, *this));
        // Publish on the worker thread so every SDK call is serialised there.
        enqueue([this] { publish_all(); });
        if (heartbeat_sec_ > 0) start_heartbeat();
        spdlog::info("[{}] OnboardAPI Device service '{}' up on domain {}",
                     std::string(id()), service_name_, domain_id_);
    }

    void on_stop() override {
        // stop() has already joined the worker before calling on_stop(), so
        // tearing down here cannot race with publish_all().
        if (heartbeat_.joinable()) { heartbeat_.request_stop(); heartbeat_.join(); }
        handle_.reset();
        OnboardApiDiscovery::instance().close(disco_token_);
    }

    // IService — log-only command handling (see node doc). Acknowledge liveness
    // and enable/disable by re-reporting; never act on shutdown/restart.
    void CmdIsDeviceAlive() override { enqueue([this] { publish_all(); }); }
    void CmdEnableDevice(bool IsEnabled) override {
        is_enabled_.store(IsEnabled);
        spdlog::info("[{}] CmdEnableDevice({}) — re-reporting status", std::string(id()), IsEnabled);
        enqueue([this] { publish_all(); });
    }
    void CmdShutdownDevice() override { spdlog::info("[{}] CmdShutdownDevice received — ignored (log-only)", std::string(id())); }
    void CmdRestartDevice() override  { spdlog::info("[{}] CmdRestartDevice received — ignored (log-only)", std::string(id())); }
    void CmdSetDeviceMode(::M_Common::OperationModeType const&) override {}
    void CmdSetValue(::std::string const&, ::M_Common::PropertyType const&) override {}
    void CmdSetValueIncrementally(::std::string const&, ::M_Common::PropertyType const&) override {}
    void RequestValue(::std::string const&) override {}

private:
    void start_heartbeat() {
        heartbeat_ = std::jthread([this](std::stop_token st) {
            while (!st.stop_requested()) {
                for (int i = 0; i < heartbeat_sec_ * 5 && !st.stop_requested(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (st.stop_requested()) break;
                enqueue([this] { publish_all(); });
            }
        });
    }

    void publish_all() {
        if (!handle_) return;
        (*handle_)->ReportDeviceSettings(build_settings());
        (*handle_)->ReportDeviceStatus(build_status(), /*IsRemoved=*/false);
    }

    ::M_Device::DeviceSettingsType build_settings() const {
        ::M_Device::DeviceSettingsType s;
        s.DeviceName  = device_name_;
        s.Description = description_;

        ::M_Device::SoftwareVersionType sw;
        sw.ModuleName = "Flowboard";
        sw.Major = FLOWBOARD_VERSION_MAJOR;
        sw.Minor = FLOWBOARD_VERSION_MINOR;
        sw.Patch = FLOWBOARD_VERSION_PATCH;
        sw.Tag   = FLOWBOARD_GIT_BRANCH;
        sw.VcsId = FLOWBOARD_GIT_COMMIT;
        sw.MaterialRevision = FLOWBOARD_GIT_DESCRIBE;
        for (auto const& d : kDependencies)
            sw.ChildModuleNames.push_back(std::string(d.name) + " " + d.version);
        s.SwVersionList.push_back(std::move(sw));

        for (auto const& [k, v] : collect_facts()) {
            ::M_Common::PropertyType p;
            p.Name = k;
            p.StrValue = v;
            s.AdditionalInfo.push_back(std::move(p));
        }

        s.IsSetDeviceModeSupported  = false;
        s.IsShutdownDeviceSupported = false;
        s.IsRestartDeviceSupported  = false;
        return s;
    }

    ::M_Device::DeviceStatusType build_status() const {
        ::M_Device::DeviceStatusType st;
        st.IsEnabled = is_enabled_.load();
        // OperationStatus left default-constructed; richer mode/fault reporting
        // is a future extension (needs a live node-state hook into the graph).
        return st;
    }

    std::optional<::M_Device::Service> handle_;
    OnboardApiDiscovery::Token         disco_token_{0};
    std::jthread                       heartbeat_;
#else  // stub SDK: register but stay inert (no DDS transport available)
    void on_start() override {
        spdlog::warn("[{}] OnboardApi.DeviceReport: live Device service requires the real "
                     "OnboardAPI SDK; node is inert under the stub build. Report would be:",
                     std::string(id()));
        for (auto const& [k, v] : collect_facts())
            spdlog::info("[{}]   {} = {}", std::string(id()), k, v);
    }
#endif

private:
    int               domain_id_;
    std::string       service_name_;
    std::string       device_name_;
    std::string       description_;
    int               heartbeat_sec_;
    std::atomic<bool> is_enabled_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "OnboardApi.DeviceReport",
    DeviceReportNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"Device Report",
      "description":"Port-less node. While the graph runs it opens an OnboardAPI Device service (real SDK) that reports this application's version, build/git identity, dependency versions and runtime status.",
      "properties":{
        "domainId":{"type":"integer","title":"DDS domain","default":1,"minimum":0},
        "serviceName":{"type":"string","title":"Service name","default":"Flowboard"},
        "deviceName":{"type":"string","title":"Device name","default":"Flowboard"},
        "description":{"type":"string","title":"Description","default":"Flowboard workflow engine"},
        "heartbeatSec":{"type":"integer","title":"Heartbeat (s)","default":5,"minimum":0,
          "description":"Re-publish settings + status every N seconds (refreshes uptime). 0 disables the heartbeat."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"domainId":1,"serviceName":"Flowboard","deviceName":"Flowboard","description":"Flowboard workflow engine","heartbeatSec":5})JSON")

}  // namespace flowboard::nodes
