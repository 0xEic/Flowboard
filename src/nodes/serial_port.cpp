// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/list_value.hpp"
#include "builtin_types.hpp"
#include "transport/serial_port.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace flowboard::nodes {

namespace {

SerialPort::Config make_config(nlohmann::json const& cfg) {
    SerialPort::Config c;
    c.port           = cfg.value("port",          std::string{});
    c.baud_rate      = cfg.value("baudRate",      std::uint32_t{115200});
    c.data_bits      = cfg.value("dataBits",      std::uint8_t{8});
    auto par = cfg.value("parity", std::string{"none"});
    c.parity = (par == "even" ? 'E' : par == "odd" ? 'O' :
                 par == "mark" ? 'M' : par == "space" ? 'S' : 'N');
    c.stop_bits      = cfg.value("stopBits",      std::uint8_t{1});
    auto fc = cfg.value("flowControl", std::string{"none"});
    c.flow_control = (fc == "software" ? 'S' : fc == "hardware" ? 'H' : 'N');
    c.read_timeout_ms = cfg.value("readTimeoutMs", std::uint32_t{10});
    return c;
}

}  // namespace

class SerialPortNode : public Node {
public:
    SerialPortNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Serial.Port"),
          tx_("tx"), rx_("rx"), connected_("connected"),
          cfg_(make_config(cfg)),
          chunk_size_(cfg.value("readChunkSize", std::size_t{1024})),
          auto_reconnect_(cfg.value("autoReconnect", true)),
          reconnect_ms_(cfg.value("reconnectIntervalMs", std::uint32_t{1000})) {
        register_input(&tx_);
        register_output(&rx_);
        register_output(&connected_);
    }

protected:
    void on_start() override {
        tx_.set_internal_sink([this](InputPort<ListValue>::Value v) {
            enqueue([this, v]{ do_send(*v); });
        });
        port_running_.store(true);
        opener_ = std::jthread([this](std::stop_token st){ open_loop(st); });
    }

    void on_stop() override {
        port_running_.store(false);
        port_alive_.store(false);
        if (opener_.joinable()) { opener_.request_stop(); opener_.join(); }
        if (reader_.joinable()) { reader_.request_stop(); reader_.join(); }
        // Node::stop() has already joined the node worker before calling on_stop(),
        // so tear down inline — an enqueue() here would never be drained. The reader
        // is joined first so it can never touch a port that is being closed.
        if (port_) { port_->close(); port_.reset(); }
    }

private:
    void do_send(ListValue const& lv) {
        if (!port_) return;
        std::vector<std::uint8_t> bytes;
        bytes.reserve(lv.items.size());
        for (auto const& j : lv.items) {
            if (j.is_number_integer()) bytes.push_back(static_cast<std::uint8_t>(j.get<int>() & 0xFF));
        }
        if (!port_->write(bytes.data(), bytes.size())) {
            spdlog::warn("[{}] Serial.Port: write failed", std::string(id()));
            enqueue([this]{ teardown_and_flip_disconnected(); });
        }
    }

    void emit_connected(bool v) { connected_.emit(std::make_shared<const bool>(v)); }

    void teardown_and_flip_disconnected() {
        if (port_) { port_->close(); port_.reset(); }
        port_alive_.store(false);
        emit_connected(false);
    }

    void read_loop(std::stop_token st, std::shared_ptr<SerialPort> port) {
        std::vector<std::uint8_t> buf(chunk_size_);
        while (!st.stop_requested() && port_alive_.load()) {
            // `port` is the reader's own strong ref, so the SerialPort cannot be
            // destroyed under us even if a concurrent teardown resets port_.
            std::ptrdiff_t n = port->read(buf.data(), buf.size());
            if (n < 0) {
                enqueue([this]{ teardown_and_flip_disconnected(); });
                return;
            }
            if (n == 0) continue;
            auto chunk = std::make_shared<ListValue>();
            chunk->element_type_tag = "flowboard::UInt8";
            chunk->items.reserve(static_cast<std::size_t>(n));
            for (std::ptrdiff_t i = 0; i < n; ++i)
                chunk->items.push_back(buf[i]);
            enqueue([this, chunk]{ rx_.emit(std::shared_ptr<const ListValue>(chunk)); });
        }
    }

    void open_loop(std::stop_token st) {
        while (!st.stop_requested() && port_running_.load()) {
            auto opened = SerialPort::open(cfg_);
            if (!opened) {
                emit_connected(false);
                if (!auto_reconnect_) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms_));
                continue;
            }
            auto sp = std::shared_ptr<SerialPort>(std::move(opened));
            enqueue([this, sp]() mutable {
                port_ = std::move(sp);
                port_alive_.store(true);
                emit_connected(true);
            });
            // Wait briefly for the install to land before spawning the reader.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (reader_.joinable()) reader_.join();
            reader_ = std::jthread([this, sp](std::stop_token sst){ read_loop(sst, sp); });
            while (!st.stop_requested() && port_running_.load() && port_alive_.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (reader_.joinable()) { reader_.request_stop(); reader_.join(); }
            if (!auto_reconnect_) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms_));
        }
    }

    InputPort<ListValue>   tx_;
    OutputPort<ListValue>  rx_;
    OutputPort<bool>       connected_;
    SerialPort::Config     cfg_;
    std::size_t            chunk_size_;
    bool                   auto_reconnect_;
    std::uint32_t          reconnect_ms_;

    std::shared_ptr<SerialPort> port_;             // mutated only via enqueue (Node worker)
    std::atomic<bool>           port_alive_{false};// cross-thread alive flag for read_loop / open_loop
    std::atomic<bool>           port_running_{false};
    std::jthread                opener_;
    std::jthread                reader_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Serial.Port",
    SerialPortNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["port"],
      "properties":{
        "port":{"type":"string","title":"Port",
          "description":"COMx on Windows, /dev/ttyUSBx or /dev/ttyS0 on Linux/macOS."},
        "baudRate":{"type":"integer","title":"Baud rate","minimum":50,"default":115200},
        "dataBits":{"type":"integer","title":"Data bits","enum":[5,6,7,8],"default":8},
        "parity":{"type":"string","title":"Parity","enum":["none","even","odd","mark","space"],"default":"none"},
        "stopBits":{"type":"integer","title":"Stop bits","enum":[1,2],"default":1},
        "flowControl":{"type":"string","title":"Flow control","enum":["none","software","hardware"],"default":"none"},
        "readChunkSize":{"type":"integer","title":"Read chunk (bytes)","minimum":1,"default":1024},
        "readTimeoutMs":{"type":"integer","title":"Read timeout (ms)","minimum":1,"default":10},
        "autoReconnect":{"type":"boolean","title":"Auto-reconnect","default":true},
        "reconnectIntervalMs":{"type":"integer","title":"Reconnect interval (ms)","minimum":50,"default":1000}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"port":"","baudRate":115200,"dataBits":8,"parity":"none","stopBits":1,"flowControl":"none","readChunkSize":1024,"readTimeoutMs":10,"autoReconnect":true,"reconnectIntervalMs":1000})JSON"
)

}  // namespace flowboard::nodes
