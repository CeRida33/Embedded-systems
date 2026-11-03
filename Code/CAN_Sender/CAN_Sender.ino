#include <FlexCAN_T4.h>

FlexCAN_T4<CAN0, RX_SIZE_256, TX_SIZE_16> can0;

uint32_t sistSendt = 0;   // tidspunkt (ms) for forrige sending
uint8_t  teller = 0;      // verdien som sendes, øker for hver melding

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) { }

  can0.begin();
  can0.setBaudRate(500000);      // samme bitrate som PCAN-View
  Serial.println("CAN-sender klar på CAN0 @ 500 kbit/s");
}

void loop() {
  // Send hvert 1000 ms uten delay(), så loop() aldri blokkerer
  if (millis() - sistSendt >= 1000) {
    sistSendt = millis();

    CAN_message_t ut;
    ut.id     = 0x42;            // 11-bit standard-ID
    ut.len    = 1;               // 1 databyte
    ut.buf[0] = teller++;        // 0, 1, 2, ... 255, 0, ...

    can0.write(ut);

    Serial.print("Sendte ID 0x42, data ");
    Serial.println(ut.buf[0]);
  }
}