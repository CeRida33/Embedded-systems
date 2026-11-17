// protocol.h – CAN-protokollen mellom Pi og Teensy (samme tabell som i README.md).
// Bare rene funksjoner uten IO, så pakkingen kan testes i test_tetris.cpp.
// Teensy-sketchen har en egen kopi av ID-ene og bitoppsettet; endrer du noe her, endre der også.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "tetris_game.h"

// Lavest ID vinner arbitreringen på bussen. Teensy-ens input har lavest ID, så den aldri må
// vente på Pi-ens tilstandsrammer. 0x1xx = sensordata (rå input), 0x2xx = prosessert spilltilstand.
constexpr uint32_t ID_JOYSTICK = 0x100;  // Teensy -> Pi
constexpr uint32_t ID_IMU = 0x110;       // Pi -> alle (logging/debug i candump)
constexpr uint32_t ID_BOARD = 0x200;     // Pi -> Teensy, fire rammer 0x200..0x203
constexpr uint32_t ID_STATE = 0x210;     // Pi -> Teensy

// 0x100, byte 0: rå joystick-tilstand fra Teensy (1 = inne).
constexpr uint8_t BTN_LEFT = 1 << 0, BTN_RIGHT = 1 << 1, BTN_UP = 1 << 2, BTN_DOWN = 1 << 3, BTN_CLICK = 1 << 4;

using Frame = std::array<uint8_t, 8>;

// 0x200+k: byte 0 = sekvensnummer (øker hver gang brettet endres), byte 1-7 = radene 5k..5k+4.
// 5 rader x 10 kolonner = 50 bit. Bit i = (rad % 5) * 10 + kolonne, minst signifikante bit først.
// Teensy-en tar brettet i bruk først når alle fire delene har samme sekvensnummer.
inline std::array<Frame, 4> packBoard(const Board& b, uint8_t seq) {
  std::array<Frame, 4> f{};
  for (auto& part : f) part[0] = seq;
  for (int r = 0; r < ROWS; ++r)
    for (int c = 0; c < COLS; ++c)
      if (b[r][c]) {
        const int i = (r % 5) * 10 + c;
        f[r / 5][1 + i / 8] |= 1 << (i % 8);
      }
  return f;
}

// 0x210: aktiv brikke som ferdig 4x4-maske, så Teensy-en ikke trenger å kunne brikkeformene.
// Ved game over er masken 0 (ingen aktiv brikke å tegne).
inline Frame packState(const TetrisGame& g) {
  const uint16_t mask = g.over ? 0 : g.pieceMask();
  const uint16_t score = std::min(g.score, 0xFFFF);
  const uint8_t flags = g.turn | g.over << 1 | g.lastPlacedBy << 2;
  return {uint8_t(mask >> 8), uint8_t(mask), uint8_t(g.x), uint8_t(g.y),
          flags, uint8_t(g.baseFallMs() / 10), uint8_t(score >> 8), uint8_t(score)};
}

// 0x110: akselerasjon i milli-g (int16, big-endian) + vippeflagg. Teensy-en bruker den ikke.
inline std::array<uint8_t, 7> packImu(float x, float y, float z, uint8_t flags) {
  auto mg = [](float g) { return uint16_t(int16_t(std::lround(g * 1000))); };
  const uint16_t ax = mg(x), ay = mg(y), az = mg(z);
  return {uint8_t(ax >> 8), uint8_t(ax), uint8_t(ay >> 8), uint8_t(ay), uint8_t(az >> 8), uint8_t(az), flags};
}
