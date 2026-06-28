// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace flowboard {

/// \brief Engine-internal cross-platform serial-port abstraction.
class SerialPort {
public:
    struct Config {
        std::string  port;         // "COM3" / "/dev/ttyUSB0"
        std::uint32_t baud_rate    = 115200;
        std::uint8_t  data_bits    = 8;
        char          parity       = 'N';  // N/E/O/M/S
        std::uint8_t  stop_bits    = 1;    // 1 or 2 (1.5 not supported on all platforms)
        char          flow_control = 'N';  // N (none) / S (software) / H (hardware)
        std::uint32_t read_timeout_ms = 10;
    };

    static std::unique_ptr<SerialPort> open(Config const& cfg);
    virtual ~SerialPort() = default;
    SerialPort(SerialPort const&) = delete;
    SerialPort& operator=(SerialPort const&) = delete;

    /// \brief Read up to `max_bytes` bytes; blocks up to `read_timeout_ms`
    ///        from Config; returns the number of bytes read or -1 on error.
    virtual std::ptrdiff_t read(std::uint8_t* dst, std::size_t max_bytes) = 0;
    /// \brief Write all bytes; returns true on success.
    virtual bool write(std::uint8_t const* src, std::size_t n) = 0;
    /// \brief Close the port (idempotent). Unblocks any pending read.
    virtual void close() = 0;

protected:
    SerialPort() = default;
};

}  // namespace flowboard
