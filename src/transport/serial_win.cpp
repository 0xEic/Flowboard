// SPDX-License-Identifier: MIT
#include "transport/serial_port.hpp"

#if defined(_WIN32)

#include <spdlog/spdlog.h>
#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace flowboard {
namespace {

// The handle is opened OVERLAPPED so the reader thread and the writer (node
// worker) issue concurrent I/O without serializing on the handle. With a
// synchronous handle, a blocking ReadFile and a WriteFile on the same handle
// starve each other — which stalls a port that both transmits and receives
// (e.g. a com0com loopback where flowboard owns both ends). Each direction
// uses its own OVERLAPPED + manual-reset event so they never clash.
class WinSerial : public SerialPort {
public:
    WinSerial(HANDLE h, std::uint32_t read_timeout_ms)
        : h_(h), read_timeout_ms_(read_timeout_ms),
          rd_evt_(CreateEvent(nullptr, TRUE, FALSE, nullptr)),
          wr_evt_(CreateEvent(nullptr, TRUE, FALSE, nullptr)) {}
    ~WinSerial() override {
        close();
        if (rd_evt_) CloseHandle(rd_evt_);
        if (wr_evt_) CloseHandle(wr_evt_);
    }

    std::ptrdiff_t read(std::uint8_t* dst, std::size_t max_bytes) override {
        if (h_ == INVALID_HANDLE_VALUE) return -1;
        OVERLAPPED ov{}; ov.hEvent = rd_evt_;
        ResetEvent(rd_evt_);
        DWORD n = 0;
        if (!ReadFile(h_, dst, static_cast<DWORD>(max_bytes), &n, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return -1;
            // The COMMTIMEOUTS make the read complete within read_timeout_ms with
            // whatever arrived (0 if none); the +100 ms is slack plus a cancel fallback.
            DWORD wait_ms = read_timeout_ms_ <= MAXDWORD - 100 ? read_timeout_ms_ + 100 : MAXDWORD;
            if (WaitForSingleObject(rd_evt_, wait_ms) == WAIT_TIMEOUT)
                CancelIoEx(h_, &ov);
            if (!GetOverlappedResult(h_, &ov, &n, TRUE)) {
                // ERROR_OPERATION_ABORTED: our close() cancel (-1, tear down) vs a
                // timeout cancel (0, just no data this poll).
                if (GetLastError() == ERROR_OPERATION_ABORTED)
                    return closing_.load() ? -1 : 0;
                return -1;
            }
        }
        return static_cast<std::ptrdiff_t>(n);
    }
    bool write(std::uint8_t const* src, std::size_t n) override {
        if (h_ == INVALID_HANDLE_VALUE) return false;
        OVERLAPPED ov{}; ov.hEvent = wr_evt_;
        ResetEvent(wr_evt_);
        DWORD written = 0;
        if (!WriteFile(h_, src, static_cast<DWORD>(n), &written, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(wr_evt_, 1000) == WAIT_TIMEOUT) {
                CancelIoEx(h_, &ov);
                GetOverlappedResult(h_, &ov, &written, TRUE);
                return false;
            }
            if (!GetOverlappedResult(h_, &ov, &written, TRUE)) return false;
        }
        return written == n;
    }
    void close() override {
        if (h_ != INVALID_HANDLE_VALUE) {
            closing_.store(true);
            CancelIoEx(h_, nullptr);
            CloseHandle(h_);
            h_ = INVALID_HANDLE_VALUE;
        }
    }
private:
    HANDLE h_;
    std::uint32_t read_timeout_ms_;
    HANDLE rd_evt_;
    HANDLE wr_evt_;
    std::atomic<bool> closing_{false};
};

}  // namespace

std::unique_ptr<SerialPort> SerialPort::open(Config const& cfg) {
    // Always use \\.\COMx so port numbers > 9 work.
    std::string full = "\\\\.\\" + cfg.port;
    HANDLE h = CreateFileA(full.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        spdlog::error("serial: CreateFile({}) failed ({})", cfg.port, GetLastError());
        return nullptr;
    }
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        spdlog::error("serial: GetCommState({}) failed ({})", cfg.port, GetLastError());
        CloseHandle(h); return nullptr;
    }
    dcb.BaudRate = cfg.baud_rate;
    dcb.ByteSize = cfg.data_bits;
    dcb.StopBits = (cfg.stop_bits == 2) ? TWOSTOPBITS : ONESTOPBIT;
    switch (cfg.parity) {
        case 'E': dcb.Parity = EVENPARITY;  dcb.fParity = TRUE; break;
        case 'O': dcb.Parity = ODDPARITY;   dcb.fParity = TRUE; break;
        case 'M': dcb.Parity = MARKPARITY;  dcb.fParity = TRUE; break;
        case 'S': dcb.Parity = SPACEPARITY; dcb.fParity = TRUE; break;
        default:  dcb.Parity = NOPARITY;    dcb.fParity = FALSE; break;
    }
    if (cfg.flow_control == 'H') {
        dcb.fOutxCtsFlow = TRUE; dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    } else if (cfg.flow_control == 'S') {
        dcb.fInX = TRUE; dcb.fOutX = TRUE;
    }
    if (!SetCommState(h, &dcb)) {
        spdlog::error("serial: SetCommState({}) failed ({})", cfg.port, GetLastError());
        CloseHandle(h); return nullptr;
    }
    // Return as soon as any buffered bytes are available, otherwise wait up to
    // read_timeout_ms then return what arrived (possibly 0). ReadIntervalTimeout
    // = MAXDWORD with a *zero* multiplier is the documented "read what's there"
    // pattern; a non-zero multiplier would make the total timeout scale with the
    // 1024-byte request size (≈ MAXDWORD * 1024), so ReadFile would block until
    // the whole buffer filled and never return small frames as they trickle in.
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = cfg.read_timeout_ms;
    to.WriteTotalTimeoutConstant   = 1000;
    SetCommTimeouts(h, &to);
    return std::make_unique<WinSerial>(h, cfg.read_timeout_ms);
}

}  // namespace flowboard

#endif  // _WIN32
