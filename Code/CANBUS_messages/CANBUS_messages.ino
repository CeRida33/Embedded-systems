#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FlexCAN_T4.h>

// ---------- OLED ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_DC     6
#define OLED_CS     10
#define OLED_RESET  5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
                         &SPI, OLED_DC, OLED_RESET, OLED_CS);

// ---------- CAN ----------
// <CAN module, RX queue, TX queue>. CAN0 is one of the channels on the SK Pang board.
FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;

uint32_t antallMottatt = 0;      // counts all received messages
CAN_message_t sisteMelding;      // copy of the last message, for display

// Redraws the whole screen based on the variables above
void tegnSkjerm() {
  display.clearDisplay();
  display.setTextSize(1);                   // 6x8 pixels per character -> 21 characters per line
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("MAS245 CAN-mottak");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);   // dividing line under the title

  display.setCursor(0, 14);
  display.print("Antall mottatt: ");
  display.println(antallMottatt);

  if (antallMottatt == 0) {
    display.println("Venter pa melding...");
  } else {
    display.print("ID:  0x");
    display.println(sisteMelding.id, HEX);
    display.print("Len: ");
    display.println(sisteMelding.len);

    display.print("Data:");
    for (uint8_t i = 0; i < sisteMelding.len; i++) {
      if (i == 4) display.print("\n     ");       // 4 bytes per line, so it fits
      display.print(' ');
      if (sisteMelding.buf[i] < 0x10) display.print('0');  // leading zero: 0x0A -> "0A"
      display.print(sisteMelding.buf[i], HEX);
    }
  }
  display.display();                         // send the buffer to the screen
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) { }

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("SSD1306 feilet");
    while (true) { }
  }

  can0.begin();
  can0.setBaudRate(500000);                  // must be the same on ALL nodes on the bus

  tegnSkjerm();                              // show "Venter..." at start-up
  Serial.println("Klar, lytter på CAN0 @ 500 kbit/s");
}

void loop() {
  CAN_message_t msg;

  // read() returns 1 if there was a message in the RX queue, otherwise 0 (polling)
  if (can0.read(msg)) {
    antallMottatt++;
    sisteMelding = msg;

    // Also print to the Serial Monitor, useful for debugging
    Serial.print("ID 0x"); Serial.print(msg.id, HEX);
    Serial.print("  len "); Serial.print(msg.len);
    Serial.print("  data");
    for (uint8_t i = 0; i < msg.len; i++) {
      Serial.print(' ');
      if (msg.buf[i] < 0x10) Serial.print('0');
      Serial.print(msg.buf[i], HEX);
    }
    Serial.println();

    tegnSkjerm();
  }
}