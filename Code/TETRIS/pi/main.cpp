// main.cpp – MAS245 CAN-Tetris: spillserver og spiller 2 på Raspberry Pi 3.
//
// Dataflyt (oppgavekravet):  Teensy leser joystick (sensor) -> sender rå tilstand på CAN (0x100)
//   -> Pi prosesserer (flankedeteksjon + spillogikk) -> Pi sender brett og brikke tilbake (0x200-0x210)
//   -> Teensy viser det på OLED. Pi-en leser i tillegg sin egen IMU (spiller 2) og legger den ut på 0x110.
//
// Pi-en eier ALL spillogikk og klokka; Teensy-en er bare input og skjerm. Det gir én kilde til sannhet:
// kjørte begge nodene spillet, måtte de holdes synkronisert (to klokker som glir, tapte rammer).
// Nå kan Teensy-en starte sent, miste en ramme eller restarte uten at de to ser ulike spill.
//
// Hovedløkka blokkerer aldri: poll() venter på CAN i maks LOOP_TIMEOUT_MS, og alle periodiske
// oppgaver styres av steady_clock (monoton, hopper ikke når systemklokka stilles).
//
// Bruk:  can_pingpong              spill
//        can_pingpong --kalibrer   skriv IMU-verdier, for å justere tersklene i imu.h

#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include "can_bus.h"
#include "imu.h"
#include "protocol.h"
#include "renderer.h"
#include "tetris_game.h"

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr const char* CAN_IF = "can0";
constexpr int LOOP_TIMEOUT_MS = 5;            // lengste ventetid i poll()
constexpr milliseconds IMU_PERIOD{20};        // les IMU med 50 Hz
constexpr milliseconds IMU_SEND_PERIOD{100};  // send 0x110 med 10 Hz (bare logging)
constexpr milliseconds REFRESH_PERIOD{500};   // send alt på nytt selv uten endring
constexpr milliseconds TEENSY_TIMEOUT{1000};  // ingen 0x100 på så lenge = ingen kontakt

// Kalibreringsmodus: skriv rå og omregnede verdier 10 ganger i sekundet. Blokkering er greit her.
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
    usleep(100 * 1000);
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
  auto lastTeensy = lastFall - TEENSY_TIMEOUT;  // "ingen kontakt" til første 0x100 kommer

  auto send = [&](uint32_t id, const auto& data) {
    if (!can.send(id, data)) ++sendErrors;
  };

  std::printf("\x1b[2J");  // tøm terminalen én gang; render() tegner deretter over seg selv
  while (true) {
    // 1. Vent på CAN (maks LOOP_TIMEOUT_MS) og behandle alt som ligger i køen.
    can.wait(LOOP_TIMEOUT_MS);
    can_frame f;
    while (can.read(f)) {
      if (f.can_id != ID_JOYSTICK || f.can_dlc < 1) continue;
      // Spiller 1: Teensy sender rå knappetilstand. Flankedeteksjon her gir én hendelse per trykk
      // (ingen auto-repeat), og en ramme som kommer to ganger eller forsvinner gjør ingen skade.
      const uint8_t buttons = f.data[0], pressed = buttons & ~prevButtons;
      prevButtons = buttons;
      lastTeensy = Clock::now();
      if (pressed & BTN_LEFT) game.move(P1, -1);
      if (pressed & BTN_RIGHT) game.move(P1, +1);
      if (pressed & (BTN_UP | BTN_CLICK)) game.rotate(P1);
      game.setSoftDrop(P1, buttons & BTN_DOWN);  // tilstand: så lenge knappen holdes
    }

    const auto now = Clock::now();
    auto due = [&](Clock::time_point& last, Clock::duration period) {
      if (now - last < period) return false;
      last = now;
      return true;
    };

    // 2. Spiller 2: IMU. Vipp venstre/høyre og vipp bort er HENDELSER, vipp mot deg er en TILSTAND.
    //    Rotasjon = vipp bort: samme mekanisme som resten, og ingen gyro-terskel å stille inn. Et raskt
    //    rykk ville dessuten slått ut på akselerometeret og kunnet gi en flytt ved et uhell.
    if (imu.ok && due(lastImu, IMU_PERIOD) && imu.read(acc)) {
      if (const int dir = tiltX.update(acc.x)) game.move(P2, dir);
      if (tiltY.update(acc.y) == -1) game.rotate(P2);
      game.setSoftDrop(P2, tiltY.state == +1);
    }
    if (imu.ok && due(lastImuSend, IMU_SEND_PERIOD)) {
      const uint8_t flags = (tiltX.state < 0) | (tiltX.state > 0) << 1 | (tiltY.state < 0) << 2 | (tiltY.state > 0) << 3;
      send(ID_IMU, packImu(acc.x, acc.y, acc.z, flags));
    }

    // 3. Tyngdekraft: ett steg ned hver gang fall-intervallet har gått.
    if (due(lastFall, milliseconds(game.fallIntervalMs()))) game.step();

    // 4. Send det som er endret, og alt på nytt hver REFRESH_PERIOD (Teensy kan ha startet sent
    //    eller mistet en ramme). Brettet sendes før brikken, så Teensy har det nye brettet når
    //    den nye brikken kommer.
    const bool refresh = due(lastRefresh, REFRESH_PERIOD);
    const bool boardChanged = game.board != sentBoard;
    const Frame state = packState(game);
    const bool stateChanged = state != sentState;
    if (boardChanged) {
      ++boardSeq;
      sentBoard = game.board;
    }
    if (boardChanged || refresh) {
      const auto parts = packBoard(game.board, boardSeq);
      for (int k = 0; k < 4; ++k) send(ID_BOARD + k, parts[k]);
    }
    if (stateChanged || refresh) {
      send(ID_STATE, state);
      sentState = state;
    }
    if (boardChanged || stateChanged || refresh) {
      char footer[160];
      std::snprintf(footer, sizeof footer, "IMU %s  x %+.2f g  y %+.2f g | Teensy: %s | CAN-sendefeil: %d",
                    imu.ok ? "ok" : "MANGLER (kjør --kalibrer)", acc.x, acc.y,
                    now - lastTeensy < TEENSY_TIMEOUT ? "ok" : "ingen kontakt", sendErrors);
      render(game, footer);
    }
  }
}
