#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include "can_bus.h"
#include "imu.h"
#include "protocol.h"
#include "renderer.h"
#include "tetris_game.h"

using Clock = std::chrono::steady_clock;  // monotonic
using std::chrono::milliseconds;

// ---------- Settings ----------
constexpr const char* CAN_IF = "can0";
constexpr int LOOP_TIMEOUT_MS = 5;            // max wait in poll()
constexpr milliseconds IMU_PERIOD{20};        // read IMU at 50 Hz
constexpr milliseconds IMU_SEND_PERIOD{100};  // send 0x110 at 10 Hz
constexpr milliseconds REFRESH_PERIOD{500};   // resend everything even without changes
constexpr milliseconds TEENSY_TIMEOUT{1000};  // no 0x100 for this long = no contact

// ---------- Calibration mode (--kalibrer) ----------
int calibrate(Imu& imu) {
  if (!imu.ok) {
    std::fprintf(stderr,
                 "Fant ikke MPU-6050 på %s, adresse 0x%02X (WHO_AM_I = %d, forventet 104 = 0x68, -1 = ingen svar).\n"
                 "Sjekk dtparam=i2c_arm=on i config.txt og at /dev/i2c-1 finnes (modprobe i2c-dev). Se README.\n",
                 I2C_DEV, MPU_ADDR, imu.whoAmI);
    return 1;
  }
  std::printf("MPU-6050 OK. Vippet over %.2f g, nøytral under %.2f g. Ctrl+C avslutter.\n", TILT_ON_G, TILT_OFF_G);
  Tilt tiltX, tiltY;
  Accel a;
  while (imu.read(a)) {
    tiltX.update(a.x);
    tiltY.update(a.y);
    std::printf("\rrå %6d %6d %6d | x %+5.2f g  y %+5.2f g  z %+5.2f g | x-vipp %+d  y-vipp %+d   ",
                a.rawX, a.rawY, a.rawZ, a.x, a.y, a.z, tiltX.state, tiltY.state);
    std::fflush(stdout);
    usleep(100 * 1000);  // 10 Hz
  }
  std::fprintf(stderr, "\nLesefeil fra IMU\n");
  return 1;
}

int main(int argc, char** argv) {
  Imu imu;
  if (argc > 1 && std::string(argv[1]) == "--kalibrer") return calibrate(imu);

  CanBus can(CAN_IF);
  TetrisGame game(std::random_device{}());
  Tilt tiltX, tiltY;
  Accel acc;
  uint8_t prevButtons = 0, boardSeq = 0;
  int sendErrors = 0;
  Board sentBoard{};
  Frame sentState{};
  auto lastFall = Clock::now(), lastImu = lastFall, lastImuSend = lastFall, lastRefresh = lastFall;
  auto lastTeensy = lastFall - TEENSY_TIMEOUT;

  auto send = [&](uint32_t id, const auto& data) {
    if (!can.send(id, data)) ++sendErrors;
  };

  std::printf("\x1b[2J");  // clear terminal once
  while (true) {
    // ---------- 1. CAN input (player 1) ----------
    can.wait(LOOP_TIMEOUT_MS);
    can_frame f;
    while (can.read(f)) {
      if (f.can_id != ID_JOYSTICK || f.can_dlc < 1) continue;
      const uint8_t buttons = f.data[0], pressed = buttons & ~prevButtons;  // edge detection
      prevButtons = buttons;
      lastTeensy = Clock::now();
      if (pressed & BTN_LEFT) game.move(P1, -1);
      if (pressed & BTN_RIGHT) game.move(P1, +1);
      if (pressed & (BTN_UP | BTN_CLICK)) game.rotate(P1);
      game.setSoftDrop(P1, buttons & BTN_DOWN);  // held, not edge
    }

    const auto now = Clock::now();
    auto due = [&](Clock::time_point& last, Clock::duration period) {
      if (now - last < period) return false;
      last = now;
      return true;
    };

    // ---------- 2. IMU input (player 2) ----------
    // Tilt left/right = move, tilt away = rotate, tilt towards you = soft drop
    if (imu.ok && due(lastImu, IMU_PERIOD) && imu.read(acc)) {
      if (const int dir = tiltX.update(acc.x)) game.move(P2, dir);
      if (tiltY.update(acc.y) == -1) game.rotate(P2);
      game.setSoftDrop(P2, tiltY.state == +1);
    }
    if (imu.ok && due(lastImuSend, IMU_SEND_PERIOD)) {
      const uint8_t flags = (tiltX.state < 0) | (tiltX.state > 0) << 1 | (tiltY.state < 0) << 2 | (tiltY.state > 0) << 3;
      send(ID_IMU, packImu(acc.x, acc.y, acc.z, flags));
    }

    // ---------- 3. Gravity ----------
    if (due(lastFall, milliseconds(game.fallIntervalMs()))) game.step();

    // ---------- 4. Send changes, and everything every REFRESH_PERIOD ----------
    const bool refresh = due(lastRefresh, REFRESH_PERIOD);
    const bool boardChanged = game.board != sentBoard;
    const Frame state = packState(game);
    const bool stateChanged = state != sentState;
    if (boardChanged) {
      ++boardSeq;
      sentBoard = game.board;
    }
    if (boardChanged || refresh) {  // board before state, so the Teensy has the new board first
      const auto parts = packBoard(game.board, boardSeq);
      for (int k = 0; k < 4; ++k) send(ID_BOARD + k, parts[k]);
    }
    if (stateChanged || refresh) {
      send(ID_STATE, state);
      sentState = state;
    }

    // ---------- 5. Terminal ----------
    if (boardChanged || stateChanged || refresh) {
      char footer[160];
      std::snprintf(footer, sizeof footer, "IMU %s  x %+.2f g  y %+.2f g | Teensy: %s | CAN-sendefeil: %d",
                    imu.ok ? "ok" : "MANGLER (kjør --kalibrer)", acc.x, acc.y,
                    now - lastTeensy < TEENSY_TIMEOUT ? "ok" : "ingen kontakt", sendErrors);
      render(game, footer);
    }
  }
}
