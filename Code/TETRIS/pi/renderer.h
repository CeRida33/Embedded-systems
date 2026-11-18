#pragma once
#include <array>
#include <iostream>
#include <sstream>
#include <string>
#include "tetris_game.h"

inline void render(const TetrisGame& g, const std::string& footer) {
  // ---------- Info text, one string per board row ----------
  std::array<std::string, ROWS> info;
  info[0] = "MAS245 CAN-Tetris";
  info[2] = "Score: " + std::to_string(g.score);
  info[3] = "Lines: " + std::to_string(g.lines);
  info[4] = "Fall:  " + std::to_string(g.baseFallMs()) + " ms";
  info[6] = std::string("Turn:  ") + (g.turn == P1 ? "player 1 (Teensy joystick)" : "player 2 (Pi IMU)");
  if (g.over) {
    info[8] = "GAME OVER! Player " + std::to_string(g.lastPlacedBy + 1) + " placed the last piece";
    info[9] = "Rotate (joystick up/click or tilt away) for a new round";
  }

  // ---------- Board ----------
  std::ostringstream out;
  out << "\x1b[H";  // cursor to top left: draw over the previous frame, no flicker
  for (int r = 0; r < ROWS; ++r) {
    out << "<!";
    for (int c = 0; c < COLS; ++c) {
      const int pr = r - g.y, pc = c - g.x;  // cell position inside the active piece's 4x4 mask
      const bool piece = !g.over && pr >= 0 && pr < 4 && pc >= 0 && pc < 4 && maskHas(g.pieceMask(), pr, pc);
      out << (piece ? "[]" : g.board[r][c] ? "##" : " .");
    }
    out << "!>  " << info[r] << "\x1b[K\n";  // \x1b[K clears the rest of the line
  }
  out << "<!" << std::string(2 * COLS, '=') << "!>\x1b[K\n" << footer << "\x1b[K\n";
  std::cout << out.str() << std::flush;
}
