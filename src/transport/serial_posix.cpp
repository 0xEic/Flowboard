// SPDX-License-Identifier: MIT
#include "transport/serial_port.hpp"

#if !defined(_WIN32)

#include <spdlog/spdlog.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace flowboard {
namespace {

speed_t baud_const(std::uint32_t b) {
    switch (b) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B0;
    }
}

class PosixSerial : public SerialPort {
public:
    explicit PosixSerial(int fd) : fd_(fd) {}
    ~PosixSerial() override { close(); }

    std::ptrdiff_t read(std::uint8_t* dst, std::size_t max_bytes) override {
        if (fd_ < 0) return -1;
        ssize_t n = ::read(fd_, dst, max_bytes);
        if (n < 0 && errno == EINTR) return 0;
        return static_cast<std::ptrdiff_t>(n);
    }
    bool write(std::uint8_t const* src, std::size_t n) override {
        while (n > 0) {
            ssize_t w = ::write(fd_, src, n);
            if (w < 0) { if (errno == EINTR) continue; return false; }
            src += w; n -= static_cast<std::size_t>(w);
        }
        return true;
    }
    void close() override {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }
private:
    int fd_;
};

}  // namespace

std::unique_ptr<SerialPort> SerialPort::open(Config const& cfg) {
    int fd = ::open(cfg.port.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        spdlog::error("serial: open({}) failed: {}", cfg.port, std::strerror(errno));
        return nullptr;
    }
    struct termios tty{};
    if (::tcgetattr(fd, &tty) != 0) {
        spdlog::error("serial: tcgetattr({}) failed: {}", cfg.port, std::strerror(errno));
        ::close(fd); return nullptr;
    }
    speed_t sp = baud_const(cfg.baud_rate);
    if (sp == B0) {
        spdlog::error("serial: unsupported baud rate {}", cfg.baud_rate);
        ::close(fd); return nullptr;
    }
    cfsetispeed(&tty, sp);
    cfsetospeed(&tty, sp);
    tty.c_cflag &= ~PARENB;
    if (cfg.parity == 'E') { tty.c_cflag |= PARENB; tty.c_cflag &= ~PARODD; }
    else if (cfg.parity == 'O') { tty.c_cflag |= PARENB; tty.c_cflag |= PARODD; }
    tty.c_cflag &= ~CSTOPB;
    if (cfg.stop_bits == 2) tty.c_cflag |= CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= (cfg.data_bits == 7 ? CS7 : CS8);
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;
    if (cfg.flow_control == 'H') tty.c_cflag |= CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    if (cfg.flow_control == 'S') tty.c_iflag |= (IXON | IXOFF);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_oflag &= ~OPOST;
    // Per-read timeout: VTIME in 1/10 sec, VMIN = 0 -> read returns after timeout
    // or as soon as any byte arrives, whichever first.
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = static_cast<cc_t>(std::max<std::uint32_t>(1, cfg.read_timeout_ms / 100));
    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        spdlog::error("serial: tcsetattr({}) failed: {}", cfg.port, std::strerror(errno));
        ::close(fd); return nullptr;
    }
    return std::make_unique<PosixSerial>(fd);
}

}  // namespace flowboard

#endif  // !_WIN32
