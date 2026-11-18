# CAN-Tetris for to spillere

To spillere bytter på å styre brikkene i et felles Tetris-brett (10 × 20). Spiller 1 bruker joysticken
på en Teensy 3.6, og spiller 2 vipper en Raspberry Pi 3 med akselerometer. Når en brikke lander,
går turen over til den andre spilleren. Fallfarten øker for hver fjernet linje, og spillet er over
når en ny brikke ikke får plass.

## Arkitektur

Nodene kommuniserer over CAN på 250 kbit/s.

- **Teensy 3.6** (SK Pang Dual CAN-kort) leser joysticken hvert 10. ms og sender rå knappetilstand
  til Pi-en. Den tegner brettet og den aktive brikken som Pi-en sender tilbake, på OLED-skjermen.
  Teensy-en har ingen spillogikk.
- **Raspberry Pi 3** (PiCAN GPS + Gyro + Accelerometer) eier all spillogikk og klokka.
  - Den gjør flankedeteksjon på knappetilstanden, så hvert trykk gir én hendelse.
  - Den leser akselerometeret i MPU-6050 over I2C, og gjør vipping om til input med hysterese.
  - Den sender spilltilstanden tilbake over CAN og viser spillet i terminalen over SSH.

Sensordata går altså fra Teensy-en over CAN til Pi-en, blir prosessert der, og går tilbake til
Teensy-en som prosesserte data. Med logikken samlet på én node finnes det bare én versjon av
spilltilstanden, så ingenting må synkroniseres.

Pi-koden er delt i moduler:

- `TetrisGame` er ren spillogikk uten IO.
- `protocol.h` pakker CAN-rammene.
- `CanBus` er en SocketCAN-innpakning.
- `Imu` leser MPU-6050.
- `render()` tegner spillet i terminalen.
- `main.cpp` har hovedløkka, som ikke blokkerer: den venter med `poll()`, og alle tidsstyrte oppgaver
  bruker `std::chrono::steady_clock`.

## CAN-protokoll

| ID | Fra → til | Len | Innhold | Sendes |
|---|---|---|---|---|
| `0x100` | Teensy → Pi | 1 | Knapper (1 = inne): bit0 venstre, bit1 høyre, bit2 opp, bit3 ned, bit4 klikk | ved endring + hvert 100 ms |
| `0x110` | Pi → alle | 7 | ax, ay, az (int16, milli-g, big-endian). Byte 6 er vippeflagg: bit0 venstre, bit1 høyre, bit2 bort, bit3 mot | hvert 100 ms |
| `0x200`–`0x203` | Pi → Teensy | 8 | Byte 0: sekvensnummer. Byte 1–7: fem rader à 10 bit (del k = rad 5k…5k+4) | ved endring + hvert 500 ms |
| `0x210` | Pi → Teensy | 8 | Aktiv brikke som 4×4-maske (2 byte), x, y, flagg (tur, game over, siste spiller), fall-intervall / 10 ms, poeng (uint16) | ved endring + hvert 500 ms |

- **Prioritet.** Lavest ID vinner arbitreringen. Joystick-rammen har derfor lavest ID og venter aldri
  på Pi-ens rammer.
- **Pålitelighet.** Teensy-en tar et nytt brett i bruk først når alle fire delene har samme
  sekvensnummer. Rammer som kommer i feil rekkefølge eller blir borte, gir dermed ikke et halvt
  oppdatert brett.
- **Periodisk gjentakelse.** Alt sendes på nytt hvert 500. ms, så en Teensy som har mistet noe, er
  oppdatert igjen innen et halvt sekund.
- **Busslast.** Den er i verste fall omtrent 7,5 kbit/s, altså rundt 3 % av 250 kbit/s.

## Styring

| | Spiller 1 (joystick) | Spiller 2 (vipping) |
|---|---|---|
| Flytt én rute | venstre / høyre | vipp til venstre / høyre (én gang per vipp) |
| Roter | opp eller trykk | vipp bort fra deg |
| Raskere fall | hold ned | hold kortet vippet mot deg |

Etter game over starter en rotasjon en ny runde.

Akselerometeret måler tyngdekraften, så en vippevinkel θ gir x = sin θ. Ved 0,35 g (≈ 20°) regnes
kortet som vippet. Under 0,15 g (≈ 9°) er det nøytralt igjen. De ulike tersklene gir hysterese.
Lavpassfiltrering skjer i MPU-6050 (DLPF ≈ 5 Hz).

## Bygg og kjøring

**Pi.** Legg filene i `pi/` i Buildroot-prosjektets `mas245/can_pingpong/`, og bygg med
`make can_pingpong-rebuild`. I2C må være slått på (`dtparam=i2c_arm=on` i `config.txt`, og
`modprobe i2c-bcm2835 i2c-dev`). Start så:

```bash
ip link set can0 up type can bitrate 250000
```
```bash
can_pingpong
```

`can_pingpong --kalibrer` skriver akselerometerverdiene fortløpende, slik at tersklene og fortegnene
i `imu.h` kan justeres.

**Teensy.** Last opp `teensy_tetris/teensy_tetris.ino` med Teensyduino. Sketchen trenger
`FlexCAN_T4` og `Adafruit_SSD1306`.

## Testing

- **Spillogikk og CAN-pakking** testes uten maskinvare:
  ```bash
  g++ -std=c++17 test_tetris.cpp -o test_tetris && ./test_tetris
  ```
- **Pi-en** kan testes uten Teensy med `loopback on` på `can0`. `cansend can0 100#01` etterfulgt av
  `100#00` simulerer et trykk på venstre.
- **Teensy-en** viser et testmønster til den mottar et brett. Et kjent brett kan sendes med
  `cansend`:
  ```bash
  cansend can0 200#0101084000021000
  cansend can0 201#0120000108400002
  cansend can0 202#0101084000021000
  cansend can0 203#0120000108400002
  cansend can0 210#4E000308005004D2
  ```
  Det skal gi to diagonale linjer og en T-brikke.
