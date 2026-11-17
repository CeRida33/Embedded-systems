# CAN-Tetris for to spillere (MAS245, prosjekt del 3, oppgave 5)

To spillere bytter på å styre brikkene i ett felles Tetris-brett. Spiller 1 bruker joysticken på
Teensy 3.6, og spiller 2 vipper Raspberry Pi-en (akselerometeret på PiCAN-kortet). Når en brikke
lander, går turen til den andre. All kommunikasjon mellom nodene går over CAN.

## Arkitektur

```
   Teensy 3.6 (spiller 1)                Raspberry Pi 3 (spiller 2 + spillserver)
  +----------------------+              +---------------------------------------------+
  |                      |    0x100     |                                             |
  | joystick ------------+------------->|  CanBus --> flankedeteksjon --.             |
  |  (sampling 10 ms)    |              |                               v             |
  |                      |              |  MPU-6050 --> Imu ----------> TetrisGame    |
  |                      |              |   (I2C)    (hysterese)        |  (logikk,   |
  |                      | 0x200-0x203  |                               |   klokke)   |
  | OLED <-- tegning <---+<-------------+-- protocol.h <----------------+             |
  |                      | 0x210, 0x110 |                               |             |
  |                      |              |   SSH-terminal <-- Renderer <-+             |
  +----------+-----------+              +-----------------------+---------------------+
             |                                                  |
  ===========+===== CAN-bus 250 kbit/s, 120 ohm i hver ende ====+===========
```

| Fil | Innhold |
|---|---|
| `pi/tetris_game.h` | `TetrisGame`: ren spillogikk (brett, brikker, tur, poeng, fart). Ingen IO. |
| `pi/protocol.h` | CAN-ID-er og pakking av rammene. Ingen IO. |
| `pi/can_bus.h` | `CanBus`: SocketCAN-innpakning (ikke-blokkerende). |
| `pi/imu.h` | `Imu`: MPU-6050 via `/dev/i2c-1`, og `Tilt`: hysterese for én akse. |
| `pi/renderer.h` | `render()`: tegner spillet i terminalen med ANSI-escape-koder. |
| `pi/main.cpp` | Hovedløkka og timing, og kalibreringsmodus (`--kalibrer`). |
| `pi/test_tetris.cpp` | Test av spillogikk og CAN-pakking, kjøres på PC (ikke en del av Buildroot-bygget). |
| `pi/can_pingpong.pro` | qmake-prosjekt (C++17, uten Qt). |
| `teensy_tetris/teensy_tetris.ino` | Teensy-sketch: joystick inn, OLED ut. |

## CAN-protokoll

Alle rammer er standard (11-bit ID), 250 kbit/s. Multi-byte-tall er big-endian, så de kan leses rett
fra `candump`.

| ID | Fra → til | Len | Innhold | Sendes |
|---|---|---|---|---|
| `0x100` | Teensy → Pi | 1 | B0: rå joystick, 1 = inne. bit0 venstre, bit1 høyre, bit2 opp, bit3 ned, bit4 klikk | ved endring + hvert 100 ms |
| `0x110` | Pi → alle | 7 | B0–1 ax, B2–3 ay, B4–5 az (int16, milli-g). B6 vippeflagg: bit0 venstre, bit1 høyre, bit2 bort (roter), bit3 mot deg (raskere fall) | hvert 100 ms |
| `0x200`–`0x203` | Pi → Teensy | 8 | B0: sekvensnummer (øker når brettet endres). B1–7: rad 5k…5k+4 for del k = ID − 0x200. Bit i = (rad mod 5)·10 + kolonne, minst signifikante bit først | ved endring + hvert 500 ms |
| `0x210` | Pi → Teensy | 8 | B0–1: aktiv brikke som 4×4-maske (bit 15 = øverst til venstre, radvis; 0 = ingen). B2: x (int8). B3: y (int8). B4 flagg: bit0 tur (0 = spiller 1, 1 = spiller 2), bit1 game over, bit2 hvem som plasserte siste brikke. B5: fall-intervall / 10 ms. B6–7: poeng (uint16) | ved endring + hvert 500 ms |

**Hvorfor disse ID-ene.** På CAN vinner lavest ID arbitreringen. Teensy-ens input har lavest ID (`0x100`),
så den slipper alltid foran Pi-ens rammer. Den venter derfor maks på rammen som allerede er på bussen
(≤ 0,54 ms), også midt i en oppdatering. Grupperingen er `0x1xx` = rå sensordata inn og `0x2xx` = prosessert
spilltilstand ut. Det gjør `candump` lett å lese (`candump can0,100:700` viser bare sensordata,
`candump can0,200:700` bare spilltilstand). Pi-ens egne rammer konkurrerer ikke med hverandre, fordi
MCP2515-driveren sender én ramme om gangen i den rekkefølgen programmet skriver dem. Derfor sendes
brettet før brikken.

**Busslast.** En standardramme med n databyte er 47 + 8n bit, pluss opptil ⌊(34 + 8n − 1)/4⌋
stuffing-bit i verste fall.

| ID | Bit per ramme (verste fall) | Rammer/s | bit/s |
|---|---|---|---|
| `0x100` | 65 | ≈ 15 (10 heartbeat + trykk) | ≈ 1 000 |
| `0x110` | 125 | 10 | 1 250 |
| `0x200`–`0x203` | 135 | ≈ 12 (4 × (2 periodiske + ~1 endring)) | ≈ 1 600 |
| `0x210` | 135 | ≤ 27 (20 ved raskere fall + input + periodisk) | ≤ 3 650 |
| **Sum** | | | **≈ 7,5 kbit/s ≈ 3 % av 250 kbit/s** |

## Oppsett

### Raspberry Pi: I2C for IMU-en

IMU-en på PiCAN GPS + Gyro + Accelerometer er en **MPU-6050** på I2C-buss 1, adresse **0x68**
(fra SK Pangs brukerveiledning). CAN-oppsettet deres (`dtparam=spi=on`, `dtoverlay=mcp2515-can0,...`)
beholdes. I tillegg trengs dette:

1. **`dtparam=i2c_arm=on` i `config.txt`.** Raskest er å legge det inn på kortet:
   ```bash
   mount /dev/mmcblk0p1 /mnt && echo "dtparam=i2c_arm=on" >> /mnt/config.txt && umount /mnt && reboot
   ```
   Legg samme linje i `external/board/raspberrypi3/config.txt` (fila `BR2_PACKAGE_RPI_FIRMWARE_CONFIG_FILE`
   peker på), så den kommer med neste gang dere bygger og flasher image.
2. **I2C-driverne.** Konfigurasjonen bruker bare devtmpfs (ingen mdev/udev), så moduler lastes ikke av
   seg selv. I `bcm2709`-kjernen er I2C-driverne moduler:
   ```bash
   modprobe i2c-bcm2835
   modprobe i2c-dev
   ls /dev/i2c-1
   ```
   Si `modprobe` at modulen ikke finnes, men `/dev/i2c-1` finnes likevel, er driverne innebygd og alt er
   i orden. Mangler `/dev/i2c-1`: `make linux-menuconfig`, og slå på `CONFIG_I2C_BCM2835` og
   `CONFIG_I2C_CHARDEV`. Vil dere at modulene lastes ved oppstart, legg en kjørbar fil
   `mas245-rootfs-overlay/etc/init.d/S30i2c` i overlayet:
   ```sh
   #!/bin/sh
   # Laster I2C-driverne for MPU-6050 på PiCAN-kortet (/dev/i2c-1)
   [ "$1" = "start" ] || exit 0
   modprobe i2c-bcm2835
   modprobe i2c-dev
   ```
3. **Valgfritt:** `BR2_PACKAGE_I2C_TOOLS=y` (Target packages → Hardware handling → i2c-tools) gir
   `i2cdetect -y 1`, som skal vise `68`. Det trengs ikke: `can_pingpong --kalibrer` sjekker det samme
   (WHO_AM_I-registeret).

### Teensy

Arduino IDE 2 med Teensyduino, kort *Teensy 3.6*. Bibliotekene `Adafruit_SSD1306` og `Adafruit_GFX`
(installert fra før), og `FlexCAN_T4` følger med Teensyduino. Åpne `teensy_tetris/teensy_tetris.ino`.
Bruker dere PlatformIO-malen: kopier innholdet til `src/main.cpp` og legg til `#include <Arduino.h>` øverst.

**NB:** Bitraten er **250 kbit/s** for å matche Pi-en. Sketchene fra del 2 brukte 500 kbit/s.

### Kabling

- CAN_H til CAN_H, CAN_L til CAN_L og GND til GND. PiCAN-skrueterminalen J3: 1 n/c, 2 GND, 3 CAN_H, 4 CAN_L.
  Teensy: samme CAN0-kontakt og D-sub-adapter som i del 2.
- 120 Ω i hver ende av bussen. PiCAN har motstanden montert, men den er bare aktiv med jumper på JP2.
  Alternativt kan en 120 Ω-motstand settes mellom CAN_H og CAN_L i skrueterminalen. Teensy-kortet:
  se det dere fant i del 2, oppgave 1.4. Mål ≈ 60 Ω mellom CAN_H og CAN_L med strømmen av.

## Bygg og deploy (Pi)

1. Kopier alle filene i `pi/` inn i Buildroot-prosjektets `mas245/can_pingpong/`. `main.cpp` og
   `can_pingpong.pro` blir overskrevet; den gamle koden ligger i git-historikken til den mappa.
   Navnet `can_pingpong` er beholdt med vilje, så `can_pingpong.mk` virker uendret.
2. Bygg fra mappa som inneholder `buildroot-output/`. Første gang (ny `.pro`) kjøres configure på nytt:
   ```bash
   make -C "$PWD/buildroot-output" can_pingpong-reconfigure
   ```
   Etterpå holder det med:
   ```bash
   make -C "$PWD/buildroot-output" can_pingpong-rebuild
   ```
3. Kopier til Pi-en:
   ```bash
   scp -O buildroot-output/target/usr/bin/can_pingpong root@192.168.245.245:/usr/bin/
   ```
4. På Pi-en (over SSH, terminalvindu minst 100 × 25 tegn):
   ```bash
   ip link set can0 up type can bitrate 250000
   ```
   ```bash
   can_pingpong
   ```

Vil dere heller ha en egen pakke `can_tetris`: kopier `external/package/mas245/can_pingpong/` til
`can_tetris/`, bytt `CAN_PINGPONG`→`CAN_TETRIS` og `can_pingpong`→`can_tetris` i begge filene, gi `.pro`-fila
samme navn, og legg til en `source`-linje i `external/package/mas245/Config.in`.

## Kalibrering av IMU

Et akselerometer i ro måler 1 g rett opp. Flatt på bordet er (x, y, z) ≈ (0, 0, +1) g. Vippes kortet
vinkelen θ til siden, blir x = sin θ og z = cos θ, så x i g er et direkte mål på vinkelen:

| Vipp | x (g) |
|---|---|
| 9° | 0,15 (`TILT_OFF_G`, tilbake i nøytral) |
| 20° | 0,35 (`TILT_ON_G`, vippet) |
| 30° | 0,50 |

Det samme gjelder y når kortet vippes mot eller bort fra deg. Ulike terskler for "på" og "av" (hysterese)
gjør at støy rundt én terskel ikke gir mange hendelser. Under `TILT_OFF_G` er dødsonen: kortet må tilbake
dit før neste flytt registreres. Lavpassfilteret ligger i selve MPU-6050 (DLPF ≈ 5 Hz, register `0x1A`).

```bash
can_pingpong --kalibrer
```

skriver rå verdier, verdier i g og vippetilstand (−1/0/+1) for x og y ti ganger i sekundet. Den trenger
ikke CAN.

1. Flatt: x ≈ 0,00, y ≈ 0,00, z ≈ +1,00 g (rå z ≈ 16384).
2. Vipp mot **høyre**: x skal bli positiv. Hvis ikke, sett `SIGN_X = -1` i `imu.h`.
3. Vipp **mot deg**: y skal bli positiv. Hvis ikke, sett `SIGN_Y = -1`.
4. Dukker venstre/høyre opp på y i stedet for x, bytt `rawX` og `rawY` i `Imu::read()` (TODO-en står der).
5. Juster `TILT_ON_G` og `TILT_OFF_G` til x-vipp går til ±1 ved et tydelig vipp og tilbake til 0 nær flatt
   (`TILT_OFF_G` < `TILT_ON_G`). Bygg og kopier på nytt etter endringer.

Styring for spiller 2: vipp venstre/høyre = flytt én rute (én gang per vipp), vipp bort = roter,
vipp mot deg = raskere fall så lenge kortet holdes vippet.

## Testplan

Test hver del alene før alt kobles sammen.

### Steg 0: spillogikk på PC (ingen maskinvare)

```bash
cd pi && g++ -std=c++17 -Wall -Wextra test_tetris.cpp -o test_tetris && ./test_tetris
```

Forventet: `Alle tester OK`. Testen sjekker turtaking, vegger, landing, fjerning av linjer, game over og
nøyaktig de CAN-bytene som brukes i steg 3. Kjør den på Linux-maskina der dere bygger Buildroot. På
Windows med Smart App Control kan hjemmekompilerte `.exe`-filer bli blokkert.

### Steg 1: IMU alene (Pi)

Sett opp I2C (se Oppsett), og kjør `can_pingpong --kalibrer` som beskrevet under Kalibrering. Får du
`Fant ikke MPU-6050 ... WHO_AM_I = -1`, svarer ikke brikken: sjekk `config.txt`, `ls /dev/i2c-1` og modulene.

### Steg 2: CAN uten Teensy (Pi)

1. Er Pi-en alene på bussen, er det ingen som kvitterer (ACK) rammene. De sendes da på nytt i det
   uendelige, og ingenting kommer fram, heller ikke til `candump` på Pi-en. Slå derfor på loopback i
   MCP2515 (eller koble til PCAN-USB eller en annen node):
   ```bash
   ip link set can0 down && ip link set can0 up type can bitrate 250000 loopback on
   ```
2. SSH-vindu 1: `can_pingpong`. SSH-vindu 2: `candump can0`. Forventet:
   - `210 [8] ...` hver gang brikken flytter seg, og minst hvert 500 ms
   - `200`–`203` hvert 500 ms. Første byte (sekvensnummeret) øker når en brikke lander
   - `110 [7] ...` ti ganger i sekundet (IMU)
3. Simuler Teensy-en. Spiller 1 har første tur:
   ```bash
   cansend can0 100#01   # venstre inne: brikken flytter én rute til venstre
   cansend can0 100#00   # sluppet (må til før neste flytt: Pi-en reagerer på trykket, ikke på "inne")
   cansend can0 100#02   # høyre
   cansend can0 100#00
   cansend can0 100#04   # opp = roter (100#10 = klikk gjør det samme)
   cansend can0 100#00
   cansend can0 100#08   # ned holdt: raskere fall ...
   cansend can0 100#00   # ... til den slippes
   ```
   `100#01` to ganger på rad flytter bare én gang (ingen auto-repeat). Når brikken lander, går turen til
   spiller 2: nå har `cansend` ingen virkning, men vipping av Pi-en har det. I loopback kan `candump` vise
   rammer dobbelt. Det skader ikke her, fordi Pi-en bare reagerer på endringer i knappetilstanden.
4. Slå av loopback etterpå:
   ```bash
   ip link set can0 down && ip link set can0 up type can bitrate 250000
   ```

### Steg 3: Teensy alene

1. Last opp `teensy_tetris.ino`. Uten Pi viser OLED-en en ramme, en trekant nede til venstre (bunnraden
   full, én rute mindre for hver rad oppover) og `Venter pa Pi`. Står trekanten opp-ned eller speilvendt,
   er rad/kolonne-tegningen feil.
2. Koble Teensy-en til Pi-en (250 kbit/s, uten loopback, **uten** at `can_pingpong` kjører) og kjør
   `candump can0`: `100 [1] 00` ti ganger i sekundet. Venstre gir `01`, høyre `02`, opp `04`, ned `08`, klikk `10`.
3. Send et fast testbrett fra Pi-en (eller med PCAN-View fra PC-en, samme ID-er og data):
   ```bash
   cansend can0 200#0101084000021000
   cansend can0 201#0120000108400002
   cansend can0 202#0101084000021000
   cansend can0 203#0120000108400002
   cansend can0 210#4E000308005004D2
   ```
   Forventet: to diagonale linjer fra øverst til venstre og nedover mot høyre, et T-omriss med spissen opp
   midt på brettet, `Poeng 1234`, `Fall 800ms` og `DIN TUR!`. Etter 1 s står det `Venter pa Pi`, fordi
   ingen sender `0x210` lenger. Det er riktig.
4. Sekvensnummer: et ufullstendig sett skal ikke vises.
   ```bash
   cansend can0 202#0200000000000000
   cansend can0 200#0200000000000000
   cansend can0 201#0200000000000000   # fortsatt diagonaler: del 3 har gammelt nummer (01)
   cansend can0 203#0200000000000000   # nå blir brettet tomt
   ```
   Rekkefølgen 2, 0, 1, 3 viser samtidig at rekkefølgen ikke spiller noen rolle.

### Steg 4: hele systemet

1. Kabling og terminering som under Oppsett. Pi-en: `ip link set can0 up type can bitrate 250000`
   (uten loopback), og så `can_pingpong`.
2. OLED og terminal viser samme brett. Bunnlinja i terminalen viser `Teensy: ok`.
3. Spill: spiller 1 starter, og turen går over når brikken lander. Farten øker for hver linje. Etter
   game over starter roter (fra hvem som helst) en ny runde.
4. Feilsøking: `candump can0` i et ekstra SSH-vindu. `ip -details -statistics link show can0` viser
   busstatus (ERROR-ACTIVE/PASSIVE/BUS-OFF) og feiltellere. Øker `CAN-sendefeil` i terminalen, er det
   ingen som kvitterer: sjekk kabel, bitrate og terminering.

## Til rapporten: dataflyt

Kravet er at minst én node leser sensordata og sender dem til en annen node, som prosesserer dem og
sender prosesserte data tilbake.

1. **Sensor (Teensy).** Joysticken samples hvert 10. ms. Den rå tilstanden (fem bryterbit) sendes på CAN
   med ID `0x100`, ved endring og minst hvert 100. ms.
2. **Prosessering (Pi).** Pi-en tar imot rammen og gjør flankedeteksjon (trykk → én hendelse). Den sjekker
   at spiller 1 har turen, og kjører spillogikken: flytt, roter, fall, kollisjon, fulle linjer, poeng og
   fart. Pi-en leser også sin egen sensor, akselerometeret i MPU-6050 over I2C, og gjør vipping om til
   hendelser og tilstander med hysterese. Rådata og tolkning legges ut på `0x110`.
3. **Tilbake (Pi → Teensy).** Resultatet sendes tilbake som brett (`0x200`–`0x203`) og som aktiv brikke,
   poeng og tur (`0x210`). Teensy-en tegner det på OLED-skjermen.

Teensy-en sender altså bare rådata og får ferdig prosesserte data tilbake. Den har ingen spillogikk selv.

## Designvalg

- **Pi-en eier all logikk og klokka.** Én kilde til sannhet. Kjørte begge nodene spillet, måtte de holdes
  synkronisert (to klokker som glir fra hverandre, tapte rammer). Nå kan Teensy-en starte sent eller
  restarte uten at de to ser ulike spill.
- **Rå knappetilstand, ikke hendelser.** Teensy-en sender hva som er inne akkurat nå, og Pi-en finner
  trykkene. En ramme som kommer dobbelt eller forsvinner, gir ikke doble trekk, og heartbeat-en retter
  en tapt ramme innen 100 ms.
- **Debounce ved sampling.** 10 ms mellom hver lesing er lengre enn prelletiden, så én bevegelse gir én
  overgang. Joysticken er digital, så dødsone gjelder bare IMU-en.
- **Brikken sendes som 4×4-maske**, ikke som type + rotasjon. Teensy-en trenger da ingen kopi av
  brikketabellen eller rotasjonsreglene.
- **Sendes ved endring og hvert 500 ms.** Lav busslast, og en Teensy som har mistet noe, er à jour innen
  et halvt sekund.
- **Sekvensnummer på brettet.** Teensy-en bytter brett først når alle fire delene har samme nummer.
- **Spiller 2 roterer ved å vippe bort.** Det bruker samme hysterese som de andre bevegelsene, og det er
  ingen gyro-terskel å stille inn. Et raskt rykk ville dessuten slått ut på akselerometeret og kunnet gi
  en flytt ved et uhell.
- **Lavpassfilteret ligger i MPU-6050** (DLPF, ett register), ikke i programvaren.
- **Ikke-blokkerende hovedløkke.** `poll()` venter på CAN i maks 5 ms, og alle periodiske oppgaver styres
  av `std::chrono::steady_clock`, som er monoton.
- **Modulene er header-filer, og `main.cpp` er eneste kildefil.** Da trengs ingen endringer i
  Buildroot-pakken.
- **Avgrensninger:** ingen hard drop, ingen forhåndsvisning av neste brikke, tilfeldige brikker (ikke
  "7-bag"), ingen lock delay, og enkelt veggspark (±1 kolonne).
