// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"
#include "M_Common_types.hpp"  // M_Common::TimeType (tag/wire/tap registered by generated code)

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>

namespace flowboard::nodes {

// Emits the current wall-clock time whenever `trigger` arrives as true.
// The output port type follows the configured `unit`:
//   epoch_ms / epoch_s -> int64_t (milliseconds / seconds since the
//                                  Unix epoch)
//   iso8601            -> string  (UTC, e.g. "2026-05-17T22:30:15.123Z")
//
// Two output ports are registered (one int64_t, one string) so a downstream
// graph editor can pick whichever it needs without rewiring; only the one
// matching the configured unit ever emits.
class CurrentTimeNode : public Node {
public:
    enum class Unit { EpochMs, EpochS, Iso8601 };

    CurrentTimeNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Sources.CurrentTime"),
          trigger_("trigger"),
          out_epoch_("epoch"),
          out_iso_("iso"),
          out_common_("commonTime"),
          unit_(parse_unit(cfg.value("unit", std::string{"epoch_ms"}))),
          auto_trigger_(cfg.value("autoTrigger", false)),
          auto_ms_(cfg.value("autoTriggerMs", std::int64_t{1000})) {
        if (auto_ms_ <= 0) auto_ms_ = 1;
        register_input (&trigger_);
        register_output(&out_epoch_);
        register_output(&out_iso_);
        register_output(&out_common_);
    }

    void on_start() override {
        trigger_.set_internal_sink([this](InputPort<bool>::Value v) {
            enqueue([this, v] {
                if (!v || !*v) return;
                emit_now();
            });
        });
        if (auto_trigger_) {
            // Emit once immediately, then on the configured cadence. All emits
            // are funnelled through the node's worker queue so the output ports
            // stay single-producer.
            enqueue([this] { emit_now(); });
            auto_worker_ = std::jthread([this](std::stop_token st) {
                using namespace std::chrono;
                while (!st.stop_requested()) {
                    std::this_thread::sleep_for(milliseconds(auto_ms_));
                    if (st.stop_requested()) break;
                    enqueue([this] { emit_now(); });
                }
            });
        }
    }

    void on_stop() override {
        auto_worker_.request_stop();
        if (auto_worker_.joinable()) auto_worker_.join();
    }

private:
    static Unit parse_unit(std::string const& s) {
        if (s == "epoch_ms") return Unit::EpochMs;
        if (s == "epoch_s")  return Unit::EpochS;
        if (s == "iso8601")  return Unit::Iso8601;
        throw std::runtime_error("Sources.CurrentTime: unknown unit '" + s + "'");
    }

    void emit_now() {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto since_epoch = now.time_since_epoch();

        // CommonTime is always available regardless of the configured unit.
        {
            ::M_Common::TimeType t{};
            t.Seconds     = static_cast<std::int64_t>(duration_cast<seconds>(since_epoch).count());
            t.Nanoseconds = static_cast<std::int32_t>(
                duration_cast<nanoseconds>(since_epoch).count() % 1000000000LL);
            out_common_.emit(std::make_shared<const ::M_Common::TimeType>(t));
        }

        switch (unit_) {
            case Unit::EpochMs: {
                auto ms = duration_cast<milliseconds>(since_epoch).count();
                out_epoch_.emit(std::make_shared<const std::int64_t>(ms));
                break;
            }
            case Unit::EpochS: {
                auto s = duration_cast<seconds>(since_epoch).count();
                out_epoch_.emit(std::make_shared<const std::int64_t>(s));
                break;
            }
            case Unit::Iso8601: {
                auto secs = duration_cast<seconds>(since_epoch).count();
                auto ms_part = duration_cast<milliseconds>(since_epoch).count() % 1000;
                std::time_t tt = static_cast<std::time_t>(secs);
                std::tm utc{};
                #if defined(_WIN32)
                    gmtime_s(&utc, &tt);
                #else
                    gmtime_r(&tt, &utc);
                #endif
                std::ostringstream os;
                os << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
                   << '.' << std::setw(3) << std::setfill('0') << ms_part << 'Z';
                out_iso_.emit(std::make_shared<const std::string>(os.str()));
                break;
            }
        }
    }

    InputPort<bool>                      trigger_;
    OutputPort<std::int64_t>             out_epoch_;
    OutputPort<std::string>              out_iso_;
    OutputPort<::M_Common::TimeType>     out_common_;
    Unit                                 unit_;
    bool                                 auto_trigger_;
    std::int64_t                         auto_ms_;
    std::jthread                         auto_worker_;
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Sources.CurrentTime",
    [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
        return std::make_unique<CurrentTimeNode>(std::move(id), cfg);
    },
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "unit":{"type":"string","title":"Output unit",
                "enum":["epoch_ms","epoch_s","iso8601"],"default":"epoch_ms",
                "description":"epoch_ms / epoch_s: Int64 on the `epoch` port. iso8601: String on the `iso` port. The commonTime port (M_Common::TimeType) always emits."},
        "autoTrigger":{"type":"boolean","title":"Auto-trigger","default":false,
                "description":"Emit automatically (once on start, then on the interval below) without needing the trigger input."},
        "autoTriggerMs":{"type":"integer","title":"Auto-trigger every (ms)","minimum":1,"default":1000,
                "description":"Interval between automatic emissions when Auto-trigger is on."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"unit":"epoch_ms","autoTrigger":false,"autoTriggerMs":1000})JSON",
    currenttime
)

}  // namespace flowboard::nodes
