#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FlexCAN_T4.h>
#include <math.h>

#define GRUPPE_NR   1          // Group number

// ---------- OLED ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_DC     6
#define OLED_CS     10
#define OLED_RESET  5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
                         &SPI, OLED_DC, OLED_RESET, OLED_CS);

// ---------- CAN ----------
FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;

uint32_t antallMottatt = 0;
uint32_t antallSendt   = 0;
uint32_t sistID        = 0;
bool     maTegnes      = true;    // set when something has changed
uint32_t sistTegnet    = 0;       // time of the previous screen update

// ---------- Drawing functions ----------

// Writes text horizontally centred on line y
void skrivSentrert(const char* tekst, int16_t y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(tekst, 0, y, &x1, &y1, &w, &h);   // measure the text width
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(tekst);
}

// Draws an arc (a section of a circle) that curves downwards.
// halvBredde = half the width of the arc, hoyde = how far the middle hangs down
void tegnBue(int16_t xMidt, int16_t yBunn, int16_t halvBredde, int16_t hoyde) {
  // Radius from half chord c and sagitta s:  r = (c² + s²) / (2s)
  float r  = (float)(halvBredde * halvBredde + hoyde * hoyde) / (2.0f * hoyde);
  float yc = yBunn - r;                     // the circle centre lies above the arc
  for (int16_t dx = -halvBredde; dx <= halvBredde; dx++) {
    float y = yc + sqrtf(r * r - (float)dx * dx);   // lower half of the circle
    display.drawPixel(xMidt + dx, (int16_t)lroundf(y), SSD1306_WHITE);
  }
}

// Dashed horizontal line: 3 pixels on, 3 off
void stipletLinje(int16_t y) {
  for (int16_t x = 5; x < 123; x += 6) {
    display.drawFastHLine(x, y, 3, SSD1306_WHITE);
  }
}

void tegnSkjerm() {
  display.clearDisplay();
  display.setTextSize(1);                   // 6x8 pixels per character
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);                      // correct character table (for ² etc.)

  // Frame with rounded corners around the whole screen
  display.drawRoundRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 6, SSD1306_WHITE);

  // Title + arc
  char tittel[24];
  snprintf(tittel, sizeof(tittel), "MAS245 - Gruppe %d", GRUPPE_NR);
  skrivSentrert(tittel, 2);
  tegnBue(64, 14, 25, 4);                   // 50 px wide, 4 px deep

  // CAN statistics
  display.setCursor(5, 16);
  display.print("CAN-statistikk");
  stipletLinje(25);

  display.setCursor(5, 27);
  display.print("Antall mottatt: ");
  display.print(antallMottatt);

  display.setCursor(5, 35);
  display.print("Antall sendt:   ");
  display.print(antallSendt);

  display.setCursor(5, 43);
  display.print("Mottok sist ID:");
  if (antallMottatt > 0) {
    display.print("0x");
    display.print(sistID, HEX);
  } else {
    display.print(" -");
  }
  stipletLinje(52);

  // IMU line: unused placeholder for task 4a
  display.setCursor(5, 54);
  display.print("IMU z: -- m/s\xFD");      // \xFD = ² in CP437

  display.display();                        // send the buffer to the screen
}

// ---------- Start-up ----------
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) { }

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("SSD1306 feilet");
    while (true) { }
  }

  can0.begin();
  can0.setBaudRate(500000);

  tegnSkjerm();
  Serial.println("Klar, CAN0 @ 500 kbit/s");
}

// ---------- Main loop ----------
void loop() {
  CAN_message_t msg;

  // Read ALL messages waiting in the queue
  while (can0.read(msg)) {
    antallMottatt++;
    sistID = msg.id;

    if (can0.write(msg)) antallSendt++;     // echo back to the sender

    maTegnes = true;
  }

  // Update the screen at most 10 times per second
  if (maTegnes && millis() - sistTegnet >= 100) {
    tegnSkjerm();
    sistTegnet = millis();
    maTegnes = false;
  }
}