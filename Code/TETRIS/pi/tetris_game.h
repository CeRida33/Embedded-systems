// tetris_game.h – ren spillogikk for to-spiller Tetris. Ingen IO og ingen klokke, så den kan
// testes alene (test_tetris.cpp). main.cpp eier tiden og kaller step() når et fall-intervall har gått.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <random>

constexpr int COLS = 10, ROWS = 20;

// Fart: fall-intervallet krymper for hver fjernet linje, ned til en nedre grense.
constexpr int START_FALL_MS = 800;
constexpr int SPEEDUP_PER_LINE_MS = 25;
constexpr int MIN_FALL_MS = 100;
constexpr int SOFT_DROP_MS = 50;  // fall-intervall mens "raskere fall" holdes

constexpr int POINTS[5] = {0, 100, 300, 500, 800};  // poeng for 0..4 linjer fjernet på én gang

constexpr int P1 = 0, P2 = 1;  // spiller 1 = Teensy (joystick), spiller 2 = Pi (IMU)

// De 7 brikkene, 4 rotasjoner hver (med klokka), som 4x4-masker.
// Bit 15 er øverste venstre rute, og masken leses radvis: 0x0F00 = rad 1 full = liggende I.
constexpr uint16_t SHAPES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},  // I
    {0xCC00, 0xCC00, 0xCC00, 0xCC00},  // O
    {0x0E40, 0x4C40, 0x4E00, 0x4640},  // T
    {0x06C0, 0x8C40, 0x6C00, 0x4620},  // S
    {0x0C60, 0x4C80, 0xC600, 0x2640},  // Z
    {0x44C0, 0x8E00, 0x6440, 0x0E20},  // J
    {0x4460, 0x0E80, 0xC440, 0x2E00},  // L
};

inline bool maskHas(uint16_t mask, int r, int c) { return mask & (0x8000 >> (r * 4 + c)); }

using Board = std::array<std::array<bool, COLS>, ROWS>;  // [rad][kolonne], rad 0 øverst

struct TetrisGame {
  Board board{};          // landede brikker (uten den aktive)
  int type = 0, rot = 0;  // aktiv brikke = SHAPES[type][rot]
  int x = 0, y = 0;       // øverste venstre hjørne av 4x4-masken på brettet
  int turn = P1;          // hvem som styrer den aktive brikken
  int lastPlacedBy = P1;  // hvem som plasserte forrige brikke (vises ved game over)
  int score = 0, lines = 0;
  bool over = false;
  std::array<bool, 2> softDrop{};  // "raskere fall" holdes inne, per spiller

  explicit TetrisGame(unsigned seed) : rng(seed) { reset(); }

  void reset() {
    board = {};
    score = lines = 0;
    turn = lastPlacedBy = P1;
    over = false;
    spawn();
  }

  // ---- Input. Bare spilleren som har turen påvirker brikken. ----
  void move(int player, int dx) {
    if (player == turn && !over && fits(x + dx, y, rot)) x += dx;
  }
  void rotate(int player) {
    if (over) {  // etter game over betyr roter "ny runde", for begge spillere
      reset();
      return;
    }
    if (player != turn) return;
    const int r = (rot + 1) % 4;
    for (int dx : {0, -1, 1})  // enkelt veggspark: flytt én rute hvis rotasjonen kolliderer
      if (fits(x + dx, y, r)) {
        x += dx;
        rot = r;
        return;
      }
  }
  void setSoftDrop(int player, bool held) { softDrop[player] = held; }

  // ---- Tid ----
  int baseFallMs() const { return std::max(MIN_FALL_MS, START_FALL_MS - lines * SPEEDUP_PER_LINE_MS); }
  int fallIntervalMs() const { return softDrop[turn] ? std::min(SOFT_DROP_MS, baseFallMs()) : baseFallMs(); }
  void step() {  // ett tyngdekraft-steg
    if (over) return;
    if (fits(x, y + 1, rot)) ++y;
    else lock();
  }

  uint16_t pieceMask() const { return SHAPES[type][rot]; }

  bool fits(int nx, int ny, int nrot) const {
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c) {
        if (!maskHas(SHAPES[type][nrot], r, c)) continue;
        const int br = ny + r, bc = nx + c;
        if (br < 0 || br >= ROWS || bc < 0 || bc >= COLS || board[br][bc]) return false;
      }
    return true;
  }

private:
  std::mt19937 rng;

  void spawn() {
    type = std::uniform_int_distribution<int>(0, 6)(rng);
    rot = 0;
    x = (COLS - 4) / 2;
    y = 0;
    if (!fits(x, y, rot)) over = true;  // ny brikke får ikke plass = game over
  }

  void lock() {
    // 1. Skriv brikken inn i brettet.
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        if (maskHas(pieceMask(), r, c)) board[y + r][x + c] = true;

    // 2. Fjern fulle rader: kopier alle ikke-fulle rader nedover, og tøm det som blir igjen øverst.
    int dst = ROWS - 1;
    for (int src = ROWS - 1; src >= 0; --src)
      if (std::count(board[src].begin(), board[src].end(), true) < COLS) board[dst--] = board[src];
    const int cleared = dst + 1;
    for (; dst >= 0; --dst) board[dst] = {};
    lines += cleared;
    score += POINTS[cleared];

    // 3. Turen går over til den andre spilleren.
    lastPlacedBy = turn;
    turn = 1 - turn;
    spawn();
  }
};
