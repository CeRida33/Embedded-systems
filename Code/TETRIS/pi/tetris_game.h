#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <random>

constexpr int COLS = 10, ROWS = 20;

// ---------- Speed ----------
constexpr int START_FALL_MS = 800;
constexpr int SPEEDUP_PER_LINE_MS = 25;
constexpr int MIN_FALL_MS = 100;
constexpr int SOFT_DROP_MS = 50;  // fall interval while soft drop is held

constexpr int POINTS[5] = {0, 100, 300, 500, 800};  // points for 0..4 lines cleared at once

constexpr int P1 = 0, P2 = 1;  // player 1 = Teensy (joystick), player 2 = Pi (IMU)

// ---------- Pieces ----------
// 7 pieces x 4 rotations (clockwise) as 4x4 masks. Bit 15 = top left, read row by row.
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

using Board = std::array<std::array<bool, COLS>, ROWS>;  // [row][col], row 0 at top

struct TetrisGame {
  Board board{};          // landed pieces, without the active piece
  int type = 0, rot = 0;  // active piece = SHAPES[type][rot]
  int x = 0, y = 0;       // top left corner of the 4x4 mask
  int turn = P1;
  int lastPlacedBy = P1;
  int score = 0, lines = 0;
  bool over = false;
  std::array<bool, 2> softDrop{};  // per player

  explicit TetrisGame(unsigned seed) : rng(seed) { reset(); }

  void reset() {
    board = {};
    score = lines = 0;
    turn = lastPlacedBy = P1;
    over = false;
    spawn();
  }

  // ---------- Input (only the player whose turn it is) ----------
  void move(int player, int dx) {
    if (player == turn && !over && fits(x + dx, y, rot)) x += dx;
  }
  void rotate(int player) {
    if (over) {  // rotate after game over = new round
      reset();
      return;
    }
    if (player != turn) return;
    const int r = (rot + 1) % 4;
    for (int dx : {0, -1, 1})  // simple wall kick: shift one column if the rotation collides
      if (fits(x + dx, y, r)) {
        x += dx;
        rot = r;
        return;
      }
  }
  void setSoftDrop(int player, bool held) { softDrop[player] = held; }

  // ---------- Time ----------
  int baseFallMs() const { return std::max(MIN_FALL_MS, START_FALL_MS - lines * SPEEDUP_PER_LINE_MS); }
  int fallIntervalMs() const { return softDrop[turn] ? std::min(SOFT_DROP_MS, baseFallMs()) : baseFallMs(); }
  void step() {
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
    if (!fits(x, y, rot)) over = true;  // new piece does not fit = game over
  }

  void lock() {
    // Write the piece into the board
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        if (maskHas(pieceMask(), r, c)) board[y + r][x + c] = true;

    // Clear full rows: copy non-full rows down, empty what is left at the top
    int dst = ROWS - 1;
    for (int src = ROWS - 1; src >= 0; --src)
      if (std::count(board[src].begin(), board[src].end(), true) < COLS) board[dst--] = board[src];
    const int cleared = dst + 1;
    for (; dst >= 0; --dst) board[dst] = {};
    lines += cleared;
    score += POINTS[cleared];

    // Turn goes to the other player
    lastPlacedBy = turn;
    turn = 1 - turn;
    spawn();
  }
};
