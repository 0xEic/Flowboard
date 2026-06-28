// SPDX-License-Identifier: MIT
//
// SocketCAN adapter for Linux. Registers as "socketcan" at static init. Bus
// config: `bus` = interface name (e.g. "can0"), bitrate is informational only
// (the SocketCAN backend assumes the interface is already up at the desired
// bitrate via `ip link set can0 up type can bitrate 500000`).

#include "flowboard/can_adapter.hpp"

#if defined(__linux__)

#include <spdlog/spdlog.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace flowboard {
namespace {

class SocketCanBus : public ICanBus {
public:
    SocketCanBus(int fd, CanRxCallback cb)
        : fd_(fd), cb_(std::move(cb)),
          reader_([this](std::stop_token st){ read_loop(st); }) {}

    ~SocketCanBus() override {
        reader_.request_stop();
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        if (reader_.joinable()) reader_.join();
    }

    bool send(CanFrame const& f) override {
        struct can_frame raw{};
        raw.can_id = f.id & (f.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
        if (f.is_extended) raw.can_id |= CAN_EFF_FLAG;
        if (f.is_remote)   raw.can_id |= CAN_RTR_FLAG;
        raw.can_dlc = static_cast<__u8>(std::min<std::size_t>(f.data.size(), 8));
        std::memcpy(raw.data, f.data.data(), raw.can_dlc);
        ssize_t n = ::write(fd_, &raw, sizeof(raw));
        return n == sizeof(raw);
    }

private:
    void read_loop(std::stop_token st) {
        struct can_frame raw{};
        while (!st.stop_requested()) {
            ssize_t n = ::read(fd_, &raw, sizeof(raw));
            if (n < 0) {
                if (errno == EINTR) continue;
                if (cb_.on_error) cb_.on_error(std::strerror(errno));
                break;
            }
            if (n == 0) break;
            CanFrame f;
            f.is_extended = (raw.can_id & CAN_EFF_FLAG) != 0;
            f.is_remote   = (raw.can_id & CAN_RTR_FLAG) != 0;
            f.id          = raw.can_id & (f.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
            f.dlc         = raw.can_dlc;
            f.data.assign(raw.data, raw.data + raw.can_dlc);
            struct timespec ts{};
            if (::ioctl(fd_, SIOCGSTAMPNS, &ts) == 0)
                f.timestamp_ns = std::uint64_t(ts.tv_sec) * 1'000'000'000ULL + ts.tv_nsec;
            if (cb_.on_frame) cb_.on_frame(f);
        }
    }

    int           fd_;
    CanRxCallback cb_;
    std::jthread  reader_;
};

class SocketCanAdapter : public ICanAdapter {
public:
    std::string_view name() const override { return "socketcan"; }
    std::unique_ptr<ICanBus> open(CanBusConfig const& cfg, CanRxCallback cb) override {
        int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (fd < 0) {
            spdlog::error("socketcan: socket() failed: {}", std::strerror(errno));
            return nullptr;
        }
        struct ifreq ifr{};
        std::strncpy(ifr.ifr_name, cfg.bus.c_str(), IFNAMSIZ - 1);
        if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
            spdlog::error("socketcan: SIOCGIFINDEX({}) failed: {}", cfg.bus, std::strerror(errno));
            ::close(fd); return nullptr;
        }
        struct sockaddr_can addr{};
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            spdlog::error("socketcan: bind({}) failed: {}", cfg.bus, std::strerror(errno));
            ::close(fd); return nullptr;
        }
        // NOTE: cfg.listen_only cannot be enforced at the socket layer alone.
        // True listen-only mode lives in the CAN controller itself and must be
        // configured at the network interface level, e.g.
        //   ip link set can0 type can bitrate 500000 listen-only on
        // This adapter accepts the flag for API symmetry but does not act on it.
        (void)cfg.listen_only;
        return std::make_unique<SocketCanBus>(fd, std::move(cb));
    }
};

const bool _socketcan_registered = [] {
    register_can_adapter(std::make_unique<SocketCanAdapter>());
    return true;
}();

}  // namespace
}  // namespace flowboard

#endif  // __linux__
