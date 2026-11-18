#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FlexCAN_T4.h>

// ---------- CAN protocol (must match pi/protocol.h) ----------
constexpr uint32_t ID_JOYSTICK = 0x100;   // Teensy -> Pi: [buttons]
constexpr uint32_t ID_BOARD = 0x200;      // Pi -> Teensy: 0x200..0x203 [seq, 7 bytes board]
constexpr uint32_t ID_STATE = 0x210;      // Pi -> Teensy: [mask_H mask_L x y flags fall/10ms score_H score_L]
constexpr uint32_t CAN_BITRATE = 250000;  // same as the Pi

// ---------- Joystick (active low) ----------
constexpr int JOY_LEFT = 18, JOY_RIGHT = 17, JOY_UP = 22, JOY_DOWN = 23, JOY_CLICK = 19;

// ---------- OLED screen ----------
Adafruit_SSD1306 display(128, 64, &SPI, 6, 5, 10);  // DC = 6, RESET = 5, CS = 10

// ---------- CAN ----------
FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;

// ---------- Timing (ms) ----------
constexpr uint32_t SAMPLE_MS = 10;        // longer than switch bounce = debounce
constexpr uint32_t HEARTBEAT_MS = 100;    // send joystick state at least this often
constexpr uint32_t DRAW_MS = 30;          // ~33 frames per second
constexpr uint32_t PI_TIMEOUT_MS = 1000;  // no 0x210 for this long = "Venter pa Pi"

// ---------- Board ----------
constexpr int COLS = 10, ROWS = 20, CELL = 3;  // 3x3 pixels per cell

bool board[ROWS][COLS];
uint8_t parts[4][7];                // received board parts
int partSeq[4] = {-1, -1, -1, -1};  // sequence number per part, -1 = not received
uint8_t state[8];                   // last 0x210
bool gotState = false;
uint32_t lastStateMs = 0, lastSampleMs = 0, lastSendMs = 0, lastDrawMs = 0;
uint8_t lastSent = 0;

// Bit order as BTN_* in pi/protocol.h
uint8_t readJoystick() {
  return (digitalRead(JOY_LEFT) == LOW) << 0
       | (digitalRead(JOY_RIGHT) == LOW) << 1
       | (digitalRead(JOY_UP) == LOW) << 2
       | (digitalRead(JOY_DOWN) == LOW) << 3
       | (digitalRead(JOY_CLICK) == LOW) << 4;
}

// Use the board only when all four parts have the same sequence number
void onBoardPart(int k, const uint8_t* buf) {
  partSeq[k] = buf[0];
  memcpy(parts[k], buf + 1, 7);
  for (int i = 1; i < 4; i++)
    if (partSeq[i] != partSeq[0]) return;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      const int i = (r % 5) * 10 + c;  // bit number within the part, LSB first
      board[r][c] = (parts[r / 5][i / 8] >> (i % 8)) & 1;
    }
}

void draw() {
  display.clearDisplay();

  // ---------- Board ----------
  display.drawRect(0, 1, COLS * CELL + 2, ROWS * CELL + 2, SSD1306_WHITE);
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (board[r][c]) display.fillRect(1 + c * CELL, 2 + r * CELL, CELL, CELL, SSD1306_WHITE);

  // ---------- Active piece (4x4 mask, drawn as outline) ----------
  const uint16_t mask = state[0] << 8 | state[1];
  const int8_t px = state[2], py = state[3];
  for (int i = 0; i < 16; i++)
    if (mask & (0x8000 >> i))
      display.drawRect(1 + (px + i % 4) * CELL, 2 + (py + i / 4) * CELL, CELL, CELL, SSD1306_WHITE);

  // ---------- Text ----------
  const uint8_t flags = state[4];  // bit0 turn (0 = Teensy), bit1 game over, bit2 who placed last piece
  const bool piAlive = gotState && millis() - lastStateMs < PI_TIMEOUT_MS;
  display.setCursor(36, 0);
  display.print("CAN-TETRIS");
  if (gotState) {
    display.setCursor(36, 12);
    display.print("Poeng ");
    display.print(state[6] << 8 | state[7]);
    display.setCursor(36, 22);
    display.print("Fall  ");
    display.print(state[5] * 10);
    display.print("ms");
  }
  display.setCursor(36, 34);
  display.print(!piAlive ? "Venter pa Pi" : (flags & 1) ? "Pi sin tur" : "DIN TUR!");
  if (piAlive && (flags & 2)) {
    display.setCursor(36, 46);
    display.print("GAME OVER");
    display.setCursor(36, 56);
    display.print((flags & 4) ? "Siste: Pi" : "Siste: Teensy");
  }
  display.display();
}

void setup() {
  pinMode(JOY_LEFT, INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);
  pinMode(JOY_UP, INPUT_PULLUP);
  pinMode(JOY_DOWN, INPUT_PULLUP);
  pinMode(JOY_CLICK, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC);
  display.setTextColor(SSD1306_WHITE);

  can0.begin();
  can0.setBaudRate(CAN_BITRATE);

  // Test board until the Pi sends one: triangle in the bottom left corner
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      board[r][c] = r >= 10 && c <= r - 10;
}

void loop() {
  // ---------- 1. Read all CAN frames in the queue ----------
  CAN_message_t m;
  while (can0.read(m)) {
    if (m.id >= ID_BOARD && m.id < ID_BOARD + 4 && m.len == 8) {
      onBoardPart(m.id - ID_BOARD, m.buf);
    } else if (m.id == ID_STATE && m.len == 8) {
      memcpy(state, m.buf, 8);
      gotState = true;
      lastStateMs = millis();
    }
  }

  // ---------- 2. Joystick: send on change and as heartbeat ----------
  if (millis() - lastSampleMs >= SAMPLE_MS) {
    lastSampleMs = millis();
    const uint8_t buttons = readJoystick();
    if (buttons != lastSent || millis() - lastSendMs >= HEARTBEAT_MS) {
      CAN_message_t out;
      out.id = ID_JOYSTICK;
      out.len = 1;
      out.buf[0] = buttons;
      can0.write(out);
      lastSent = buttons;
      lastSendMs = millis();
    }
  }

  // ---------- 3. Draw screen ----------
  if (millis() - lastDrawMs >= DRAW_MS) {
    lastDrawMs = millis();
    draw();
  }
}
