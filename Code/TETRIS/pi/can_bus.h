// can_bus.h – tynn innpakning rundt SocketCAN (rå CAN-socket mot f.eks. can0).
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
    // SOCK_NONBLOCK: read() og write() venter aldri. Hovedløkka venter bare i wait().
    fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (fd < 0 || ioctl(fd, SIOCGIFINDEX, &ifr) < 0) die(ifname);
    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) die(ifname);
  }

  // Vent til en ramme er mottatt, men aldri lenger enn timeoutMs.
  void wait(int timeoutMs) {
    pollfd p{fd, POLLIN, 0};
    poll(&p, 1, timeoutMs);
  }

  // Les én ramme. false = køen er tom.
  bool read(can_frame& f) { return ::read(fd, &f, sizeof f) == ssize_t(sizeof f); }

  // Send én ramme. false = sendekøen er full, typisk fordi ingen annen node kvitterer (ACK).
  template <std::size_t N>
  bool send(uint32_t id, const std::array<uint8_t, N>& data) {
    static_assert(N <= 8, "klassisk CAN har maks 8 databyte");
    can_frame f{};
    f.can_id = id;
    f.can_dlc = N;
    std::memcpy(f.data, data.data(), N);
    return ::write(fd, &f, sizeof f) == ssize_t(sizeof f);
  }

private:
  int fd;  // ponytail: lukkes av OS-et når programmet avslutter, ingen destruktor nødvendig

  static void die(const char* ifname) {
    std::fprintf(stderr, "Kunne ikke åpne %s (%s). Er CAN satt opp?\n  ip link set %s up type can bitrate 250000\n",
                 ifname, std::strerror(errno), ifname);
    std::exit(1);
  }
};
