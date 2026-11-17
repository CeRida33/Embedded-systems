// renderer.h – tegner spillet i terminalen (over SSH) med ANSI-escape-koder.
// Ingen ncurses: vi trenger bare "markør øverst til venstre" (\x1b[H) og "slett resten av linja" (\x1b[K).
#pragma once
#include <array>
#include <iostream>
#include <sstream>
#include <string>
#include "tetris_game.h"

inline void render(const TetrisGame& g, const std::string& footer) {
  std::array<std::string, ROWS> info;  // tekst til høyre for brettet, én per rad
  info[0] = "MAS245 CAN-Tetris";
  info[2] = "Poeng:  " + std::to_string(g.score);
  info[3] = "Linjer: " + std::to_string(g.lines);
  info[4] = "Fall:   " + std::to_string(g.baseFallMs()) + " ms";
  info[6] = std::string("Tur:    ") + (g.turn == P1 ? "spiller 1 (Teensy-joystick)" : "spiller 2 (Pi-IMU)");
  if (g.over) {
    info[8] = "GAME OVER! Spiller " + std::to_string(g.lastPlacedBy + 1) + " plasserte siste brikke";
    info[9] = "Roter (joystick opp/klikk eller vipp bort) for ny runde";
  }

  std::ostringstream out;
  out << "\x1b[H";  // tegn over forrige bilde i stedet for å tømme skjermen: ingen flimring
  for (int r = 0; r < ROWS; ++r) {
    out << "<!";
    for (int c = 0; c < COLS; ++c) {
      const int pr = r - g.y, pc = c - g.x;  // ruta sett fra den aktive brikkens 4x4-maske
      const bool piece = !g.over && pr >= 0 && pr < 4 && pc >= 0 && pc < 4 && maskHas(g.pieceMask(), pr, pc);
      out << (piece ? "[]" : g.board[r][c] ? "##" : " .");
    }
    out << "!>  " << info[r] << "\x1b[K\n";
  }
  out << "<!" << std::string(2 * COLS, '=') << "!>\x1b[K\n" << footer << "\x1b[K\n";
  std::cout << out.str() << std::flush;
}
