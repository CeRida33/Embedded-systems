// imu.h – leser akselerometeret i MPU-6050 på PiCAN GPS+Gyro+Acc-kortet (I2C-buss 1, adresse 0x68)
//         og gjør vipping om til spillinput for spiller 2.
//
// Registrene er hentet fra InvenSense "MPU-6000/MPU-6050 Register Map and Descriptions".
// Adressen 0x68 og I2C-buss 1 er fra SK Pangs brukerveiledning for PiCAN GPS + Gyro + Accelerometer.
#pragma once
#include <cmath>
#include <cstdint>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

constexpr const char* I2C_DEV = "/dev/i2c-1";
constexpr int MPU_ADDR = 0x68;              // standard (0x69 hvis loddebroen SJ4 er flyttet)
constexpr uint8_t REG_CONFIG = 0x1A;        // DLPF_CFG: innebygd digitalt lavpassfilter
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;  // AFS_SEL: måleområde
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;  // 6 byte: X_H X_L Y_H Y_L Z_H Z_L (big-endian)
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;    // brikken starter i dvale (SLEEP = 1)
constexpr uint8_t REG_WHO_AM_I = 0x75;      // svarer 0x68 på en MPU-6050
constexpr float LSB_PER_G = 16384.0f;       // ved ±2 g (AFS_SEL = 0)

// ---------------------------------------------------------------------------------------------
// Hvordan tyngdekraften fordeler seg på aksene
//
// Et akselerometer i ro måler 1 g rett OPP (kraften fra underlaget som holder det oppe).
// Ligger kortet flatt, havner alt på z:  (x, y, z) = (0, 0, +1) g.
// Vippes kortet vinkelen θ til siden, fordeles den samme 1 g-en mellom x og z:
//
//     x = sin θ     z = cos θ        (x² + y² + z² = 1 g² så lenge kortet holdes i ro)
//
// x i g er altså et direkte mål på vippevinkelen:  0.15 g ≈ 9°,  0.35 g ≈ 20°,  0.50 g = 30°.
// Det samme gjelder y når kortet vippes mot eller bort fra spilleren. Rister man kortet, kommer
// det ekstra akselerasjon på toppen; den dempes av lavpassfilteret i brikken (REG_CONFIG).
//
// Hysterese: aksen regnes som vippet over TILT_ON_G, men er først nøytral igjen under TILT_OFF_G.
// Området under TILT_OFF_G er dødsonen. Gapet mellom tersklene gjør at støy rundt én terskel
// ikke gir en serie av hendelser.
// ---------------------------------------------------------------------------------------------
constexpr float TILT_ON_G = 0.35f;   // ≈ 20°: vippet
constexpr float TILT_OFF_G = 0.15f;  // ≈ 9°: tilbake i nøytral
// TODO kalibrering (can_pingpong --kalibrer): vipp mot HØYRE skal gi positiv x, og vipp MOT DEG
// skal gi positiv y. Gir det motsatt fortegn, sett tilsvarende konstant til -1.
constexpr float SIGN_X = +1.0f;
constexpr float SIGN_Y = +1.0f;

struct Accel {
  int16_t rawX = 0, rawY = 0, rawZ = 0;  // rå tellinger fra brikken
  float x = 0, y = 0, z = 0;             // i g, fortegn rettet med SIGN_X/SIGN_Y
};

class Imu {
public:
  bool ok = false;
  int whoAmI = -1;  // -1 = fikk ikke svar. Brukes i feilmeldingen.

  Imu() {
    fd = open(I2C_DEV, O_RDWR);
    if (fd < 0 || ioctl(fd, I2C_SLAVE, MPU_ADDR) < 0) return;
    whoAmI = readReg(REG_WHO_AM_I);
    ok = whoAmI == 0x68
      && writeReg(REG_PWR_MGMT_1, 0x01)     // våkne, klokke fra gyro X (anbefalt i databladet)
      && writeReg(REG_CONFIG, 0x06)         // lavpass ca. 5 Hz (≈19 ms forsinkelse): fjerner skjelving
      && writeReg(REG_ACCEL_CONFIG, 0x00);  // ±2 g
  }

  // Leser x, y, z. Tar ca. 1 ms på I2C ved 100 kHz, så det er greit å gjøre hvert 20. ms.
  bool read(Accel& a) {
    uint8_t reg = REG_ACCEL_XOUT_H, b[6];
    if (::write(fd, &reg, 1) != 1 || ::read(fd, b, 6) != 6) return false;
    a.rawX = int16_t(b[0] << 8 | b[1]);
    a.rawY = int16_t(b[2] << 8 | b[3]);
    a.rawZ = int16_t(b[4] << 8 | b[5]);
    // TODO kalibrering: dukker venstre/høyre-vipp opp på y i stedet for x, bytt rawX og rawY her.
    a.x = SIGN_X * a.rawX / LSB_PER_G;
    a.y = SIGN_Y * a.rawY / LSB_PER_G;
    a.z = a.rawZ / LSB_PER_G;
    return true;
  }

private:
  int fd = -1;  // ponytail: lukkes av OS-et når programmet avslutter, ingen destruktor nødvendig

  bool writeReg(uint8_t reg, uint8_t val) {
    const uint8_t b[2] = {reg, val};
    return ::write(fd, b, 2) == 2;
  }
  int readReg(uint8_t reg) {
    uint8_t v = 0;
    return ::write(fd, &reg, 1) == 1 && ::read(fd, &v, 1) == 1 ? v : -1;
  }
};

// Én akse med hysterese. state er -1, 0 eller +1.
// update() returnerer ±1 bare idet aksen går fra nøytral til vippet (en HENDELSE, f.eks. flytt én rute).
// state kan leses direkte som en TILSTAND (f.eks. raskere fall så lenge kortet holdes vippet).
struct Tilt {
  int state = 0;
  int update(float g) {
    if (state != 0) {  // vippet: ingen nye hendelser før aksen er tilbake i dødsonen
      if (std::fabs(g) < TILT_OFF_G) state = 0;
      return 0;
    }
    if (g > TILT_ON_G) state = +1;
    if (g < -TILT_ON_G) state = -1;
    return state;
  }
};
