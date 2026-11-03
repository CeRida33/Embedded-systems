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
FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;

uint32_t antallMottatt = 0;      // antall mottatte meldinger
uint32_t antallSendt   = 0;      // antall ekko sendt tilbake
CAN_message_t sisteMelding;      // kopi av siste melding, til visning

// Tegner hele skjermbildet på nytt
void tegnSkjerm() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("MAS245 CAN-ekko");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.print("Mottatt: "); display.println(antallMottatt);
  display.print("Sendt:   "); display.println(antallSendt);

  if (antallMottatt == 0) {
    display.println("Venter pa melding...");
  } else {
    display.print("ID:  0x"); display.println(sisteMelding.id, HEX);
    display.print("Len: ");   display.println(sisteMelding.len);
    display.print("Data:");
    for (uint8_t i = 0; i < sisteMelding.len; i++) {
      if (i == 4) display.print("\n     ");      // 4 byte per linje
      display.print(' ');
      if (sisteMelding.buf[i] < 0x10) display.print('0');
      display.print(sisteMelding.buf[i], HEX);
    }
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) { }

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("SSD1306 feilet");
    while (true) { }
  }

  can0.begin();
  can0.setBaudRate(500000);          // må matche PCAN-View

  tegnSkjerm();
  Serial.println("CAN-ekko klar på CAN0 @ 500 kbit/s");
}

void loop() {
  CAN_message_t msg;

  // Polling: read() returnerer 1 hvis en melding ligger i RX-køen
  if (can0.read(msg)) {
    antallMottatt++;
    sisteMelding = msg;

    // Send samme melding tilbake til avsender
    if (can0.write(msg)) antallSendt++;

    Serial.print("Mottok og returnerte ID 0x"); Serial.print(msg.id, HEX);
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