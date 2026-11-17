// MAS245 CAN-Tetris – Teensy 3.6 på SK Pang Dual CAN-kortet (spiller 1).
//
// Teensy-en er bare INPUT og SKJERM:
//   - leser joysticken (sensor) og sender rå knappetilstand til Pi-en på 0x100
//   - tegner brettet (0x200-0x203) og aktiv brikke/poeng (0x210) som Pi-en sender tilbake
// All spillogikk ligger på Pi-en. Én kilde til sannhet: ingenting å synkronisere, og Teensy-en
// kan restartes midt i et spill uten at noe går i stykker.
//
// Biblioteker: FlexCAN_T4 (følger med Teensyduino), Adafruit_SSD1306 og Adafruit_GFX.

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FlexCAN_T4.h>

// ---------- CAN-protokoll: må matche README.md og pi/protocol.h ----------
constexpr uint32_t ID_JOYSTICK = 0x100;   // Teensy -> Pi: [knappebiter]
constexpr uint32_t ID_BOARD = 0x200;      // Pi -> Teensy: 0x200..0x203 [sekvensnr, 7 byte brett]
constexpr uint32_t ID_STATE = 0x210;      // Pi -> Teensy: [maske_H maske_L x y flagg fall/10ms poeng_H poeng_L]
constexpr uint32_t CAN_BITRATE = 250000;  // må være lik Pi-en (ip link ... bitrate 250000)

// ---------- Maskinvare ----------
// Joystick-pinner fra SK Pangs eksempelkode og MAS245-malen. Aktiv lav (INPUT_PULLUP).
constexpr int JOY_LEFT = 18, JOY_RIGHT = 17, JOY_UP = 22, JOY_DOWN = 23, JOY_CLICK = 19;
Adafruit_SSD1306 display(128, 64, &SPI, 6, 5, 10);  // OLED: DC = 6, RESET = 5, CS = 10
FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;     // samme CAN0-kontakt som i del 2

// ---------- Timing (millis) ----------
constexpr uint32_t SAMPLE_MS = 10;        // joystick-sampling. Lengre enn prelletiden (~5 ms) = debounce
constexpr uint32_t HEARTBEAT_MS = 100;    // send joystick minst så ofte, så Pi-en ser at vi lever
constexpr uint32_t DRAW_MS = 30;          // ca. 33 skjermbilder i sekundet
constexpr uint32_t PI_TIMEOUT_MS = 1000;  // ingen 0x210 på så lenge = "Venter pa Pi"

// ---------- Brett ----------
constexpr int COLS = 10, ROWS = 20, CELL = 3;  // 3x3 piksler per rute = 30x60 piksler

bool board[ROWS][COLS];             // brettet som vises
uint8_t parts[4][7];                // mottatte brett-deler, venter på komplett sett
int partSeq[4] = {-1, -1, -1, -1};  // sekvensnr per del, -1 = ikke mottatt ennå
uint8_t state[8];                   // siste 0x210
bool gotState = false;
uint32_t lastStateMs = 0, lastSampleMs = 0, lastSendMs = 0, lastDrawMs = 0;
uint8_t lastSent = 0;

// Rå joystick-tilstand som bitmaske (1 = inne). Bitrekkefølge som BTN_* i pi/protocol.h.
uint8_t readJoystick() {
  return (digitalRead(JOY_LEFT) == LOW) << 0
       | (digitalRead(JOY_RIGHT) == LOW) << 1
       | (digitalRead(JOY_UP) == LOW) << 2
       | (digitalRead(JOY_DOWN) == LOW) << 3
       | (digitalRead(JOY_CLICK) == LOW) << 4;
}

// Én brett-del (rad 5k..5k+4). Brettet byttes først når alle fire delene har samme sekvensnummer:
// da spiller det ingen rolle hvilken rekkefølge de kommer i, og mangler én, beholder vi forrige
// komplette brett til Pi-en sender alt på nytt (minst hvert 500. ms).
void onBoardPart(int k, const uint8_t* buf) {
  partSeq[k] = buf[0];
  memcpy(parts[k], buf + 1, 7);
  for (int i = 1; i < 4; i++)
    if (partSeq[i] != partSeq[0]) return;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      const int i = (r % 5) * 10 + c;  // bitnummer innenfor delen, LSB først
      board[r][c] = (parts[r / 5][i / 8] >> (i % 8)) & 1;
    }
}

void draw() {
  display.clearDisplay();
  display.drawRect(0, 1, COLS * CELL + 2, ROWS * CELL + 2, SSD1306_WHITE);  // ramme rundt brettet
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (board[r][c]) display.fillRect(1 + c * CELL, 2 + r * CELL, CELL, CELL, SSD1306_WHITE);

  // Aktiv brikke: ferdig 4x4-maske fra Pi-en (bit 15 = øverst til venstre, radvis).
  // Tegnes som omriss så den skiller seg fra brikkene som har landet. Maske 0 = ingen brikke.
  const uint16_t mask = state[0] << 8 | state[1];
  const int8_t px = state[2], py = state[3];
  for (int i = 0; i < 16; i++)
    if (mask & (0x8000 >> i))
      display.drawRect(1 + (px + i % 4) * CELL, 2 + (py + i / 4) * CELL, CELL, CELL, SSD1306_WHITE);

  const uint8_t flags = state[4];  // bit0 tur (0 = Teensy), bit1 game over, bit2 hvem plasserte siste
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

  // Testbrett til Pi-en sender noe (testplan steg 3): trekant nede til venstre, bunnraden full.
  // Står den opp-ned eller speilvendt, er rad/kolonne-tegningen feil.
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      board[r][c] = r >= 10 && c <= r - 10;
}

void loop() {
  // 1. Tøm CAN-køen. Mister vi likevel en ramme (f.eks. mens skjermen tegnes), retter
  //    sekvensnummeret og Pi-ens periodiske gjentakelse det opp.
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

  // 2. Joystick: sample hvert SAMPLE_MS, send ved endring og som heartbeat.
  //    Pi-en gjør flankedeteksjonen (trykk -> én hendelse), Teensy-en sender bare rå tilstand.
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

  // 3. Tegn skjermen.
  if (millis() - lastDrawMs >= DRAW_MS) {
    lastDrawMs = millis();
    draw();
  }
}
