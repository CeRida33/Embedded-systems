#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FlexCAN_T4.h>
#include <math.h>

#define GRUPPE_NR   1          // <-- BYTT til deres gruppenummer

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
bool     maTegnes      = true;    // settes når noe har endret seg
uint32_t sistTegnet    = 0;       // tidspunkt for forrige skjermoppdatering

// ---------- Tegnefunksjoner ----------

// Skriver tekst midtstilt horisontalt på linje y
void skrivSentrert(const char* tekst, int16_t y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(tekst, 0, y, &x1, &y1, &w, &h);   // mål tekstbredden
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(tekst);
}

// Tegner en bue (et utsnitt av en sirkel) som "smiler" nedover.
// halvBredde = halve bredden på buen, hoyde = hvor mye midten henger ned
void tegnBue(int16_t xMidt, int16_t yBunn, int16_t halvBredde, int16_t hoyde) {
  // Radius fra halv korde c og pilhøyde s:  r = (c² + s²) / (2s)
  float r  = (float)(halvBredde * halvBredde + hoyde * hoyde) / (2.0f * hoyde);
  float yc = yBunn - r;                     // sirkelsentrum ligger over buen
  for (int16_t dx = -halvBredde; dx <= halvBredde; dx++) {
    float y = yc + sqrtf(r * r - (float)dx * dx);   // nedre halvdel av sirkelen
    display.drawPixel(xMidt + dx, (int16_t)lroundf(y), SSD1306_WHITE);
  }
}

// Stiplet horisontal linje: 3 piksler på, 3 av
void stipletLinje(int16_t y) {
  for (int16_t x = 5; x < 123; x += 6) {
    display.drawFastHLine(x, y, 3, SSD1306_WHITE);
  }
}

void tegnSkjerm() {
  display.clearDisplay();
  display.setTextSize(1);                   // 6x8 piksler per tegn
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);                      // riktig tegntabell (for ² o.l.)

  // Ramme med avrundede hjørner rundt hele skjermen
  display.drawRoundRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 6, SSD1306_WHITE);

  // Tittel + bue
  char tittel[24];
  snprintf(tittel, sizeof(tittel), "MAS245 - Gruppe %d", GRUPPE_NR);
  skrivSentrert(tittel, 2);
  tegnBue(64, 14, 25, 4);                   // 50 px bred, 4 px dyp

  // CAN-statistikk
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

  // IMU-linje: plassholder til Oppgave 4a
  display.setCursor(5, 54);
  display.print("IMU z: -- m/s\xFD");      // \xFD = ² i CP437

  display.display();                        // send bufferet til skjermen
}

// ---------- Oppstart ----------
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

// ---------- Hovedløkke ----------
void loop() {
  CAN_message_t msg;

  // Les ALLE meldinger som ligger i køen
  while (can0.read(msg)) {
    antallMottatt++;
    sistID = msg.id;

    if (can0.write(msg)) antallSendt++;     // ekko tilbake til avsender

    maTegnes = true;
  }

  // Oppdater skjermen maks 10 ganger per sekund
  if (maTegnes && millis() - sistTegnet >= 100) {
    tegnSkjerm();
    sistTegnet = millis();
    maTegnes = false;
  }
}