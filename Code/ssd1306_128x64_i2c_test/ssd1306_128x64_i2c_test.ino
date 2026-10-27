#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Skjermstørrelse i piksler
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// Pinner fra SK Pang-kretsskjemaet (U4 OLED -> U5 Teensy 3.6)
// DATA -> pinne 11 (MOSI) og CLK -> pinne 13 (SCK) brukes automatisk av &SPI
#define OLED_DC     6
#define OLED_CS     10
#define OLED_RESET  5

// &SPI = bruk hardware-SPI (ikke I2C)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
                         &SPI, OLED_DC, OLED_RESET, OLED_CS);

void setup() {
  Serial.begin(9600);
  while (!Serial && millis() < 4000) { }  // vent på Serial Monitor, maks 4 s

  // SSD1306_SWITCHCAPVCC = skjermen lager sin egen høyspenning internt
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("SSD1306 feilet ved oppstart");
    while (true) { }                      // stopp her
  }
  Serial.println("SSD1306 OK");

  display.clearDisplay();                 // tøm bufferet i Teensy-ens minne
  display.setTextSize(2);                 // 2x størrelse
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);                // øverst til venstre
  display.println("MAS245");
  display.setTextSize(1);
  display.println("Hello OLED!");
  display.drawRect(0, 40, 128, 24, SSD1306_WHITE);  // rektangel nederst
  display.display();                      // send bufferet til skjermen
}

void loop() {
}