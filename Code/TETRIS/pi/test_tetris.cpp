// test_tetris.cpp – sjekker spillogikken og CAN-pakkingen uten maskinvare.
// Kjør på PC eller Pi:  g++ -std=c++17 -Wall -Wextra test_tetris.cpp -o test_tetris && ./test_tetris
// (Ikke med i can_pingpong.pro, så Buildroot bygger den ikke.)
#include <algorithm>
#include <cassert>
#include <cstdio>
#include "protocol.h"

int main() {
  // 1. Bare spilleren som har turen styrer brikken.
  TetrisGame g(1);
  assert(g.turn == P1 && !g.over);
  const int x0 = g.x;
  g.move(P2, -1);
  assert(g.x == x0);
  g.move(P1, -1);
  assert(g.x == x0 - 1);
  const int r0 = g.rot;
  g.rotate(P2);
  assert(g.rot == r0);
  g.rotate(P1);
  assert(g.rot == (r0 + 1) % 4);

  // 2. Brikken stopper ved veggen.
  auto leftmost = [](const TetrisGame& t) {
    int m = COLS;
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        if (maskHas(t.pieceMask(), r, c)) m = std::min(m, t.x + c);
    return m;
  };
  for (int i = 0; i < COLS; ++i) g.move(P1, -1);
  assert(leftmost(g) == 0);

  // 3. Når brikken lander, går turen over. Raskere fall gjelder bare den som har turen.
  while (g.turn == P1) g.step();
  assert(g.turn == P2 && g.lastPlacedBy == P1);
  g.setSoftDrop(P1, true);
  assert(g.fallIntervalMs() == START_FALL_MS);
  g.setSoftDrop(P2, true);
  assert(g.fallIntervalMs() == SOFT_DROP_MS);

  // 4. Full rad fjernes og gir poeng og raskere fall.
  TetrisGame h(1);
  for (int c = 4; c < COLS; ++c) h.board[ROWS - 1][c] = true;  // bunnrad med hull i kolonne 0-3
  h.type = 0, h.rot = 0, h.x = 0, h.y = 0;                    // liggende I rett over hullet
  while (h.turn == P1) h.step();
  assert(h.lines == 1 && h.score == POINTS[1]);
  assert(std::count(h.board[ROWS - 1].begin(), h.board[ROWS - 1].end(), true) == 0);
  assert(h.baseFallMs() == START_FALL_MS - SPEEDUP_PER_LINE_MS);

  // 5. Game over når neste brikke ikke får plass. Roter starter ny runde.
  TetrisGame k(1);
  for (int c = 1; c < COLS; ++c) k.board[2][c] = true;  // rad 2 nesten full: O-en blir liggende øverst
  k.type = 1, k.rot = 0, k.x = 3, k.y = 0;              // O i rad 0-1, midt i startområdet
  k.step();
  assert(k.over && k.lastPlacedBy == P1);
  k.rotate(P2);
  assert(!k.over && k.score == 0);

  // 6. CAN-pakking. Dette er nøyaktig bytene i cansend-eksemplene i README (testplan steg 3).
  Board d{};
  for (int r = 0; r < ROWS; ++r) d[r][r % 10] = true;  // to diagonaler
  const std::array<Frame, 4> parts = packBoard(d, 1);
  const Frame p0 = {0x01, 0x01, 0x08, 0x40, 0x00, 0x02, 0x10, 0x00};
  const Frame p1 = {0x01, 0x20, 0x00, 0x01, 0x08, 0x40, 0x00, 0x02};
  assert(parts[0] == p0 && parts[1] == p1 && parts[2] == p0 && parts[3] == p1);

  TetrisGame s(1);
  s.type = 2, s.rot = 2, s.x = 3, s.y = 8, s.score = 1234;  // T med spissen opp, spiller 1 sin tur
  assert(packState(s) == (Frame{0x4E, 0x00, 0x03, 0x08, 0x00, 0x50, 0x04, 0xD2}));
  s.over = true;
  assert(packState(s)[0] == 0 && packState(s)[1] == 0 && packState(s)[4] == 0x02);  // ingen brikke ved game over

  assert(packImu(1.0f, -0.5f, 0.0f, 0x05) == (std::array<uint8_t, 7>{0x03, 0xE8, 0xFE, 0x0C, 0x00, 0x00, 0x05}));

  std::puts("Alle tester OK");
}
