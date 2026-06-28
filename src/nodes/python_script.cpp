// SPDX-License-Identifier: MIT
//
// Python.Script — runs a user-authored Python script in a dedicated subprocess.
// The user declares typed input and output ports in the node's config; the
// script defines `def on_input(port, value): ...` and calls `emit(port, value)`
// to send values to outputs. The host marshals values as JSON over the child's
// stdin/stdout. Crashes in the script can't take down the host.

#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/port_factory_registry.hpp"
#include "builtin_types.hpp"
#include "engine/subprocess.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace flowboard::nodes {

namespace {

// The wrapper Python program. It hosts the user's script, exposes `inputs` and
// `emit`, and pumps a JSON-lines protocol on stdin/stdout. Kept compact so it
// can be embedded verbatim in the binary.
constexpr char const* kPythonWrapper = R"PYWRAP(
import sys
import json
import threading
import traceback

inputs = {}
_emit_lock = threading.Lock()

def emit(port, value):
    if not isinstance(port, str):
        sys.stderr.write("emit: port must be str, got {0}\n".format(type(port).__name__))
        sys.stderr.flush()
        return
    payload = json.dumps({"port": port, "value": value}, separators=(",", ":"))
    with _emit_lock:
        sys.stdout.write(payload + "\n")
        sys.stdout.flush()

def _main():
    if len(sys.argv) < 2:
        sys.stderr.write("flowboard-python: missing user script path\n"); sys.exit(2)
    try:
        with open(sys.argv[1], "r", encoding="utf-8") as fh:
            src = fh.read()
        exec(compile(src, sys.argv[1], "exec"), globals())
    except Exception:
        sys.stderr.write("flowboard-python: error loading user script:\n")
        traceback.print_exc()
        sys.exit(3)

    on_input_fn = globals().get("on_input")
    if not callable(on_input_fn):
        sys.stderr.write("flowboard-python: script must define on_input(port, value)\n")
        sys.exit(4)

    for line in sys.stdin:
        s = line.strip()
        if not s:
            continue
        try:
            msg = json.loads(s)
        except Exception as e:
            sys.stderr.write("flowboard-python: bad input json: {0}\n".format(e))
            sys.stderr.flush()
            continue
        port = msg.get("port")
        value = msg.get("value")
        inputs[port] = value
        try:
            on_input_fn(port, value)
        except Exception:
            sys.stderr.write("flowboard-python: on_input raised:\n")
            traceback.print_exc()
            sys.stderr.flush()

if __name__ == "__main__":
    _main()
)PYWRAP";

std::filesystem::path make_temp_path(std::string_view kind, std::string_view ext) {
    static std::mutex mu;
    static std::random_device rd;
    std::lock_guard lk(mu);
    std::string suffix;
    suffix.reserve(12);
    for (int i = 0; i < 12; ++i) suffix.push_back("0123456789abcdef"[rd() & 0xF]);
    return std::filesystem::temp_directory_path() /
           ("flowboard_" + std::string(kind) + "_" + suffix + std::string(ext));
}

// Materialize the wrapper script to a temp file once per process. Same content
// is fine to share across all Python.Script nodes, so do it lazily.
std::filesystem::path const& wrapper_path() {
    static std::once_flag flag;
    static std::filesystem::path path;
    std::call_once(flag, []{
        path = make_temp_path("pywrap", ".py");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << kPythonWrapper;
    });
    return path;
}

// --- value <-> JSON marshalling -------------------------------------------
// Most types round-trip via nlohmann's built-in converters; a couple need
// special handling so Python sees the natural shape (char as a 1-char str,
// ListValue as a bare list).

template <typename T>
inline nlohmann::json value_to_json(T const& v) {
    if constexpr (std::is_same_v<T, char>) {
        return std::string(1, v);
    } else if constexpr (std::is_same_v<T, ::flowboard::ListValue>) {
        return v.items;  // hand Python a bare list, not the {items, tag} object
    } else {
        return v;
    }
}

template <typename T>
inline T value_from_json(nlohmann::json const& j) {
    if constexpr (std::is_same_v<T, char>) {
        if (j.is_string()) { auto s = j.get<std::string>(); return s.empty() ? char(0) : s[0]; }
        if (j.is_number()) return static_cast<char>(j.get<int>());
        return char(0);
    } else if constexpr (std::is_same_v<T, ::flowboard::ListValue>) {
        // Accept either a bare array (idiomatic Python list) or the canonical
        // {items, elementTypeTag} object form.
        if (j.is_array())
            return ::flowboard::ListValue{j.get<std::vector<nlohmann::json>>(), ""};
        return j.get<::flowboard::ListValue>();
    } else {
        return j.get<T>();
    }
}

// Runtime tag -> compile-time type dispatch for every supported port type.
// Invokes `f.operator()<T>()` (C++20 templated lambda) for the matching tag and
// returns true; returns false if no tag matched. Single source of truth for the
// type list — add_input, add_output, and dispatch_emit all flow through it.
template <typename F>
bool dispatch_type(std::string_view tag, F&& f) {
    if (tag == "flowboard::Bool")   { f.template operator()<bool>();          return true; }
    if (tag == "flowboard::Int64")  { f.template operator()<std::int64_t>();  return true; }
    if (tag == "flowboard::Double") { f.template operator()<double>();        return true; }
    if (tag == "flowboard::String") { f.template operator()<std::string>();   return true; }
    if (tag == "flowboard::List")   { f.template operator()<::flowboard::ListValue>(); return true; }
    if (tag == "flowboard::Float")  { f.template operator()<float>();         return true; }
    if (tag == "flowboard::Int32")  { f.template operator()<std::int32_t>();  return true; }
    if (tag == "flowboard::UInt32") { f.template operator()<std::uint32_t>(); return true; }
    if (tag == "flowboard::Int16")  { f.template operator()<std::int16_t>();  return true; }
    if (tag == "flowboard::UInt16") { f.template operator()<std::uint16_t>(); return true; }
    if (tag == "flowboard::UInt8")  { f.template operator()<std::uint8_t>();  return true; }
    if (tag == "flowboard::UInt64") { f.template operator()<std::uint64_t>(); return true; }
    if (tag == "flowboard::Char")   { f.template operator()<char>();          return true; }
    return false;
}

constexpr char const* kSupportedTypesHelp =
    "(use flowboard::Bool, Int64, Double, String, List, Float, "
    "Int32, UInt32, Int16, UInt16, UInt8, UInt64, or Char)";

}  // namespace

// Runs a user-authored Python script in a dedicated subprocess.
class PythonScriptNode final : public Node {
public:
    PythonScriptNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Python.Script"),
          script_(cfg.value("script", std::string{})),
          python_exe_(cfg.value("pythonExe", std::string{})) {
        if (auto it = cfg.find("inputs"); it != cfg.end() && it->is_array()) {
            for (auto const& spec : *it) {
                if (!spec.is_object()) continue;
                auto name = spec.value("name", std::string{});
                auto type = spec.value("type", std::string{"flowboard::Double"});
                if (name.empty()) continue;
                add_input(type, name);
            }
        }
        if (auto it = cfg.find("outputs"); it != cfg.end() && it->is_array()) {
            for (auto const& spec : *it) {
                if (!spec.is_object()) continue;
                auto name = spec.value("name", std::string{});
                auto type = spec.value("type", std::string{"flowboard::Double"});
                if (name.empty()) continue;
                add_output(type, name);
            }
        }
    }

protected:
    void on_start() override {
        // Write the user's source to a temp file once per start.
        user_script_path_ = make_temp_path("script", ".py");
        {
            std::ofstream out(user_script_path_, std::ios::binary | std::ios::trunc);
            out << script_;
        }

        auto python = python_executable();
        proc_ = Subprocess::spawn(python, {"-u", wrapper_path().string(), user_script_path_.string()});
        if (!proc_) {
            spdlog::error("[{}] Python.Script: failed to spawn '{}' — is python installed and on PATH?",
                          std::string(id()), python.string());
            return;
        }
        spdlog::info("[{}] Python.Script: spawned {} (inputs={}, outputs={})",
                     std::string(id()), python.string(), in_storage_.size(), out_storage_.size());
        reader_out_ = std::thread([this]{ read_stdout_loop(); });
        reader_err_ = std::thread([this]{ read_stderr_loop(); });
    }

    void on_stop() override {
        if (proc_) {
            proc_->close_stdin();
            auto rc = proc_->wait(std::chrono::seconds(2));
            if (!rc) proc_->terminate();
        }
        if (reader_out_.joinable()) reader_out_.join();
        if (reader_err_.joinable()) reader_err_.join();
        proc_.reset();
        if (!user_script_path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(user_script_path_, ec);
        }
    }

private:
    template <typename T>
    void add_input_typed(std::string const& name) {
        auto p = std::make_unique<InputPort<T>>(name);
        auto* raw = p.get();
        register_input(raw);
        raw->set_internal_sink([this, name](typename InputPort<T>::Value v) {
            nlohmann::json j = value_to_json<T>(*v);
            enqueue([this, name, j = std::move(j)]() mutable { send_to_python(name, j); });
        });
        in_storage_.emplace_back(std::move(p));
    }

    template <typename T>
    void add_output_typed(std::string const& name) {
        auto p = std::make_unique<OutputPort<T>>(name);
        auto* raw = p.get();
        register_output(raw);
        out_by_name_.emplace(name, raw);
        out_storage_.emplace_back(std::move(p));
    }

    void add_input(std::string const& tag, std::string const& name) {
        // Fast path: built-in types get their custom marshalling (string-of-1
        // for Char, bare array for List, plain nlohmann for the rest).
        if (dispatch_type(tag, [&]<typename T>{ add_input_typed<T>(name); })) return;
        // Fall back to the per-type port factory registry — populated by
        // pipegen for every OnboardAPI struct.
        if (auto* fact = lookup_port_factory(tag)) {
            auto p = fact->make_input(name, [this, name](nlohmann::json j) {
                enqueue([this, name, j = std::move(j)]() mutable { send_to_python(name, j); });
            });
            register_input(p.get());
            in_storage_.emplace_back(std::move(p));
            return;
        }
        throw std::runtime_error(
            "Python.Script: unsupported input type '" + tag +
            "' (built-in scalars/List, or any OnboardAPI struct tag)");
    }

    void add_output(std::string const& tag, std::string const& name) {
        if (dispatch_type(tag, [&]<typename T>{ add_output_typed<T>(name); })) return;
        if (auto* fact = lookup_port_factory(tag)) {
            auto p = fact->make_output(name);
            out_by_name_.emplace(name, p.get());
            register_output(p.get());
            out_storage_.emplace_back(std::move(p));
            return;
        }
        throw std::runtime_error(
            "Python.Script: unsupported output type '" + tag +
            "' (built-in scalars/List, or any OnboardAPI struct tag)");
    }

    void send_to_python(std::string const& name, nlohmann::json const& v) {
        if (!proc_) return;
        nlohmann::json msg = {{"port", name}, {"value", v}};
        if (!proc_->write_line(msg.dump())) {
            spdlog::warn("[{}] Python.Script: stdin write failed (child exited?)", std::string(id()));
            proc_.reset();
        }
    }

    void dispatch_emit(std::string const& port_name, nlohmann::json const& v) {
        auto it = out_by_name_.find(port_name);
        if (it == out_by_name_.end()) {
            spdlog::warn("[{}] Python.Script: unknown output port '{}'",
                         std::string(id()), port_name);
            return;
        }
        auto* port = it->second;
        auto tag  = port->type_tag();
        try {
            // Fast path: built-in types use the same custom marshalling as
            // value_to_json above (string-of-1 for Char, bare array for List).
            if (dispatch_type(tag, [&]<typename T>{
                    static_cast<OutputPort<T>*>(port)
                        ->emit(std::make_shared<const T>(value_from_json<T>(v)));
                })) return;
        } catch (std::exception const& e) {
            spdlog::warn("[{}] Python.Script: cannot coerce value for output '{}' ({}): {}",
                         std::string(id()), port_name, std::string(tag), e.what());
            return;
        }
        // Fall back to the port-factory registry for codegen-emitted structs.
        if (auto* fact = lookup_port_factory(std::string(tag))) {
            if (!fact->emit_from_json(port, v))
                spdlog::warn("[{}] Python.Script: cannot coerce value for output '{}' ({})",
                             std::string(id()), port_name, std::string(tag));
            return;
        }
        spdlog::warn("[{}] Python.Script: output '{}' has unsupported type tag '{}'",
                     std::string(id()), port_name, std::string(tag));
    }

    void read_stdout_loop() {
        std::string line;
        while (proc_ && proc_->read_stdout_line(line)) {
            try {
                auto j = nlohmann::json::parse(line);
                auto port = j.value("port", std::string{});
                if (port.empty()) continue;
                dispatch_emit(port, j.contains("value") ? j.at("value") : nlohmann::json{});
            } catch (std::exception const& e) {
                spdlog::warn("[{}] Python.Script: malformed stdout line: {}",
                             std::string(id()), e.what());
            }
        }
    }

    void read_stderr_loop() {
        std::string line;
        while (proc_ && proc_->read_stderr_line(line)) {
            if (!line.empty()) spdlog::warn("[{}] python: {}", std::string(id()), line);
        }
    }

    std::filesystem::path python_executable() const {
        if (!python_exe_.empty()) return python_exe_;
        if (auto e = std::getenv("FLOWBOARD_PYTHON"); e && *e) return e;
#if defined(_WIN32)
        return "python";
#else
        return "python3";
#endif
    }

    std::string script_;
    std::string python_exe_;
    std::vector<std::unique_ptr<IInputPort>>  in_storage_;
    std::vector<std::unique_ptr<IOutputPort>> out_storage_;
    std::unordered_map<std::string, IOutputPort*> out_by_name_;
    std::unique_ptr<Subprocess> proc_;
    std::filesystem::path user_script_path_;
    std::thread reader_out_;
    std::thread reader_err_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Python.Script",
    PythonScriptNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputs","outputs","script"],
      "properties":{
        "inputs":{
          "type":"array","title":"Inputs",
          "items":{
            "type":"object","required":["name","type"],
            "properties":{
              "name":{"type":"string","title":"Name"},
              "type":{"type":"string","title":"Type tag",
                "default":"flowboard::Double",
                "description":"flowboard built-in (Bool/Int64/Int32/Int16/UInt8/UInt16/UInt32/UInt64/Double/Float/String/Char/List), or any OnboardAPI struct tag (e.g. M_Common::TimeType). See /api/types for the full list."}
            },
            "additionalProperties":false
          },
          "default":[]
        },
        "outputs":{
          "type":"array","title":"Outputs",
          "items":{
            "type":"object","required":["name","type"],
            "properties":{
              "name":{"type":"string","title":"Name"},
              "type":{"type":"string","title":"Type tag",
                "default":"flowboard::Double",
                "description":"flowboard built-in (Bool/Int64/Int32/Int16/UInt8/UInt16/UInt32/UInt64/Double/Float/String/Char/List), or any OnboardAPI struct tag (e.g. M_Common::TimeType). See /api/types for the full list."}
            },
            "additionalProperties":false
          },
          "default":[]
        },
        "script":{
          "type":"string","title":"Python script","format":"python",
          "description":"Define on_input(port, value); call emit('port_name', value) to send to outputs. The dict 'inputs' holds the most recent value per input port.",
          "default":"def on_input(port, value):\n    # use emit('out_port_name', value) to send to outputs\n    pass\n"
        },
        "pythonExe":{
          "type":"string","title":"Python executable","default":"",
          "description":"Override the python executable (default: 'python' on Windows, 'python3' on Unix; or set $FLOWBOARD_PYTHON)."
        }
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputs":[],"outputs":[],"script":"def on_input(port, value):\n    pass\n","pythonExe":""})JSON"
)

}  // namespace flowboard::nodes
