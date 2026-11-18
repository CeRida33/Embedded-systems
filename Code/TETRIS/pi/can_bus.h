#pragma once
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

class CanBus {
public:
  explicit CanBus(const char* ifname) {
    fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);  // non-blocking read/write
    ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (fd < 0 || ioctl(fd, SIOCGIFINDEX, &ifr) < 0) die(ifname);
    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) die(ifname);
  }

  // Wait for a frame, at most timeoutMs
  void wait(int timeoutMs) {
    pollfd p{fd, POLLIN, 0};
    poll(&p, 1, timeoutMs);
  }

  // false = queue empty
  bool read(can_frame& f) { return ::read(fd, &f, sizeof f) == ssize_t(sizeof f); }

  // false = send queue full (usually no other node ACKs)
  template <std::size_t N>
  bool send(uint32_t id, const std::array<uint8_t, N>& data) {
    static_assert(N <= 8, "classic CAN has max 8 data bytes");
    can_frame f{};
    f.can_id = id;
    f.can_dlc = N;
    std::memcpy(f.data, data.data(), N);
    return ::write(fd, &f, sizeof f) == ssize_t(sizeof f);
  }

private:
  int fd;

  static void die(const char* ifname) {
    std::fprintf(stderr, "Kunne ikke åpne %s (%s). Er CAN satt opp?\n  ip link set %s up type can bitrate 250000\n",
                 ifname, std::strerror(errno), ifname);
    std::exit(1);
  }
};
