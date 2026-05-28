// SPDX-License-Identifier: MIT
//
// End-to-end test driver for tests/integration/fixtures/e2e-mount-alert.json.
//
// Intended to run as a separate process from flowboard.exe. Both processes
// link the same OnboardAPI SDK and exchange messages via that SDK's pub/sub.
//
//   ┌─────────────────────────┐  M_Mount Reports   ┌──────────────────────────┐
//   │ this driver             │ ─────────────────▶ │ flowboard.exe         │
//   │ (publisher + subscriber)│                    │ (loads example graph)    │
//   │                         │ ◀──────────────────│                          │
//   └─────────────────────────┘  M_Alert Cmds      └──────────────────────────┘
//
// Behaviour:
//   1. Subscribes to M_Alert::CmdRaiseAlarm on domain/service from argv.
//   2. Publishes N MountPositionType reports with Elevation > threshold.
//   3. Waits for the engine pipeline to drain.
//   4. Exits 0 if at least one CmdRaiseAlarm arrived with the SubsystemName
//      and Measure fields set by the example's Factory, else exits 1.
//
// Stub SDK note: the in-tree stub SDK uses a *process-local* singleton bus,
// so cross-process exchange is impossible. With -DFLOWBOARD_USE_STUB_SDK=ON
// the ctest harness exits early with code 77 (ctest SKIP) and this driver
// is not invoked. The code below targets the stub API surface so it compiles
// cleanly today; when linking against the real OnboardAPI SDK the publish
// path may need adapting to the real Service::publishReportMountPosition (or
// equivalent) API.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "M_Common_types.hpp"
#include "M_Mount_types.hpp"
#include "M_Alert_types.hpp"

#if defined(FLOWBOARD_HAS_STUB_SDK)
#include "onboardapi/M_Mount.hpp"
#include "onboardapi/M_Alert.hpp"
#include "../../sdk_stub/src/stub_bus.hpp"
#else
#include <onboardapi/api/Mount.hpp>
#include <onboardapi/api/Alert.hpp>
#endif

namespace {

struct Args {
    int         domain        = 1;
    std::string mount_service = "MainMount";
    std::string alert_service = "MainAlert";
    int         pulses        = 10;
    int         pulse_ms      = 20;
    int         drain_ms      = 1000;
    double      elevation_deg = 45.0;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto take = [&]() -> std::string {
            if (i + 1 >= argc) return {};
            return argv[++i];
        };
        if      (k == "--domain")        a.domain        = std::atoi(take().c_str());
        else if (k == "--mount-service") a.mount_service = take();
        else if (k == "--alert-service") a.alert_service = take();
        else if (k == "--pulses")        a.pulses        = std::atoi(take().c_str());
        else if (k == "--pulse-ms")      a.pulse_ms      = std::atoi(take().c_str());
        else if (k == "--drain-ms")      a.drain_ms      = std::atoi(take().c_str());
        else if (k == "--elevation")     a.elevation_deg = std::atof(take().c_str());
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    Args const args = parse_args(argc, argv);

    std::atomic<int>             alarm_count{0};
    std::atomic<bool>             matched{false};

#if defined(FLOWBOARD_HAS_STUB_SDK)
    // Subscribe to M_Alert::CmdRaiseAlarm via the stub bus directly. The
    // ctest harness will not invoke this binary in stub mode (skip 77) but
    // we keep the code path compiling so the driver builds with either SDK.
    int sub_id = onboardapi::stub::Bus::instance().subscribe(
        args.domain, args.alert_service, "CmdRaiseAlarm",
        [&](void const* raw) {
            auto const* c = static_cast<M_Alert::AlarmConditionInfoType const*>(raw);
            alarm_count.fetch_add(1);
            if (c && c->SubsystemName == "Mount" && c->Measure == "Elevation")
                matched.store(true);
        });
#else
    // Real SDK: act as the Alert Service to receive CmdRaiseAlarm from the engine.
    struct AlertSubscriber : ::M_Alert::IService {
        AlertSubscriber(std::atomic<int>* c, std::atomic<bool>* m)
            : count(c), matched(m) {}
        std::atomic<int>* count;
        std::atomic<bool>* matched;
        void CmdRaiseAlarm(::M_Alert::AlarmConditionInfoType const& c) override {
            count->fetch_add(1);
            if (c.SubsystemName == "Mount" && c.Measure == "Elevation")
                matched->store(true);
        }
    };
    AlertSubscriber alert_cb(&alarm_count, &matched);
    auto alert_service = ::M_Alert::Service::create<int, AlertSubscriber>(
        args.domain, args.alert_service, alert_cb);

    // Real SDK: act as the Mount Service to publish ReportMountPosition.
    // Service::create's callbacks parameter is the IService Cmd receiver
    // (the engine isn't sending Mount Cmds in this scenario, so an empty
    // derived class is fine). Publishing Reports happens through the
    // Service value's operator->() (M_Mount::IClient proxy).
    struct MountServiceCallbacks : ::M_Mount::IService {};
    MountServiceCallbacks mount_cb;
    auto mount_service = ::M_Mount::Service::create<int, MountServiceCallbacks>(
        args.domain, args.mount_service, mount_cb);

    // Brief settle so DDS discovery sees the engine's Mount.Client.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif

    // Publish the configured number of MountPositionType reports.
    // MountPositionType::Elevation is AngleDegreesOptionalType (vector<double>).
    for (int i = 0; i < args.pulses; ++i) {
        M_Mount::MountPositionType msg;
        msg.Elevation.push_back(args.elevation_deg);
#if defined(FLOWBOARD_HAS_STUB_SDK)
        M_Mount::stub_publish_position(args.domain, args.mount_service, msg);
#else
        mount_service->ReportMountPosition(msg);
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(args.pulse_ms));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(args.drain_ms));

#if defined(FLOWBOARD_HAS_STUB_SDK)
    onboardapi::stub::Bus::instance().unsubscribe(sub_id);
#endif

    std::cout << "[driver] alarms_received=" << alarm_count.load()
              << " content_matched=" << (matched.load() ? "yes" : "no") << "\n";

    if (alarm_count.load() == 0) {
        std::cerr << "[driver] FAIL: no alarms received from engine\n";
        return 1;
    }
    if (!matched.load()) {
        std::cerr << "[driver] FAIL: alarm content did not match expected "
                     "SubsystemName=Mount Measure=Elevation\n";
        return 1;
    }
    return 0;
}
