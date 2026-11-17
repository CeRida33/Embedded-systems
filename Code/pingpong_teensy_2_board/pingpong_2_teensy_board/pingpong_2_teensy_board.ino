#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FlexCAN_T4.h>

<<<<<<< Updated upstream
constexpr int GRUPPE = 1;      // TODO: eget gruppenr + 20
constexpr int MOTSTANDER = 2;  // TODO: motstanders gruppenr + 20
=======
constexpr int GRUPPE = 2;      // Your groups number
constexpr int MOTSTANDER = 1;  // Opponents group nymber
>>>>>>> Stashed changes

// Joystick pins
constexpr int JOY_UP = 22, JOY_DOWN = 23, JOY_CLICK = 19;

constexpr int W = 128, H = 64;
constexpr int PH = 20, PW = 4, BALL = 3;  // height, thickness and ball size

Adafruit_SSD1306 display(W, H, &SPI, 6, 5, 10);  // DC, RESET, CS
FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;

int minY = (H - PH) / 2, motY = (H - PH) / 2;  // Top section of the plate
int ballX = -1, ballY = 0;                     
int vx = 1, vy = 1;
int minPoeng = 0, motPoeng = 0;
bool master = false;
uint32_t sistTick = 0;

void sendCan(uint32_t id, uint8_t len, uint8_t b0, uint8_t b1 = 0, uint8_t b2 = 0, uint8_t b3 = 0) {
  CAN_message_t m;
  m.id = id;
  m.len = len;
  m.buf[0] = b0; m.buf[1] = b1; m.buf[2] = b2; m.buf[3] = b3;
  can0.write(m);
}

bool treffer(int plateY) {
  return ballY + BALL > plateY && ballY < plateY + PH;
}

void nyBall() {
  ballX = W / 2;
  ballY = H / 2;
}

// Ball physics, only run from the master Teensy
void oppdaterBall() {
  ballX += vx;
  ballY += vy;
  if (ballY <= 0 || ballY >= H - BALL) vy = -vy;  // bounce on roof and floor

  if (vx > 0 && ballX >= W - PW - BALL) {         // Your groups side (right)
    if (treffer(minY)) vx = -vx;
    else { motPoeng++; nyBall(); }
  } else if (vx < 0 && ballX <= PW) {             // opponents side (left)
    if (treffer(motY)) vx = -vx;
    else { minPoeng++; nyBall(); }
  }
}

void tegn() {
  display.clearDisplay();
  display.fillRect(W - PW, minY, PW, PH, SSD1306_WHITE);
  display.fillRect(0, motY, PW, PH, SSD1306_WHITE);
  if (ballX >= 0) display.fillRect(ballX, ballY, BALL, BALL, SSD1306_WHITE);

  display.setCursor(W / 2 - 15, 0);
  display.print(motPoeng);
  display.print(" - ");
  display.print(minPoeng);
  display.display();
}

void setup() {
  pinMode(JOY_UP, INPUT_PULLUP);
  pinMode(JOY_DOWN, INPUT_PULLUP);
  pinMode(JOY_CLICK, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC);
  display.setTextColor(SSD1306_WHITE);

  can0.begin();
  can0.setBaudRate(500000);
}

void loop() {
  // Recive everything in the que
  CAN_message_t m;
  while (can0.read(m)) {
    if (m.id == MOTSTANDER + 20) {
      motY = m.buf[0];
    } else if (m.id == MOTSTANDER + 50 && !master) {
      ballX = W - BALL - m.buf[0];  // mirroring x to make your opponents coordniates
      ballY = m.buf[1];
      motPoeng = m.buf[2];          // Master points is the opponents points in our system
      minPoeng = m.buf[3];
    }
  }

  // Everything moves at a dictated rate 10 ms = 100 Hz
  if (millis() - sistTick < 10) return;
  sistTick = millis();

  // Joystick (active low)
  if (digitalRead(JOY_UP) == LOW) minY--;
  if (digitalRead(JOY_DOWN) == LOW) minY++;
  minY = constrain(minY, 0, H - PH);

  // First person to click the joystick becomes master. Unless a ball is already in play. 
  if (!master && ballX < 0 && digitalRead(JOY_CLICK) == LOW) {
    master = true;
    nyBall();
  }

  if (master) {
    oppdaterBall();
    sendCan(GRUPPE + 50, 4, ballX, ballY, minPoeng, motPoeng);
  }
  sendCan(GRUPPE + 20, 1, minY);

  tegn();
}