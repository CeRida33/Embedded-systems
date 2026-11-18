#pragma once
#include <cmath>
#include <cstdint>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

// ---------- MPU-6050 registers ----------
constexpr const char* I2C_DEV = "/dev/i2c-1";
constexpr int MPU_ADDR = 0x68;              // 0x69 if solder bridge SJ4 is moved
constexpr uint8_t REG_CONFIG = 0x1A;        // DLPF_CFG: built-in low-pass filter
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;  // AFS_SEL: measurement range
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;  // 6 bytes: X_H X_L Y_H Y_L Z_H Z_L
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;    // chip starts in sleep mode
constexpr uint8_t REG_WHO_AM_I = 0x75;      // returns 0x68
constexpr float LSB_PER_G = 16384.0f;       // at ±2 g

// ---------- Tilt thresholds (g) ----------
// Tilt angle θ gives x = sin θ: 0.15 g ≈ 9°, 0.35 g ≈ 20°
constexpr float TILT_ON_G = 0.35f;   // counts as tilted
constexpr float TILT_OFF_G = 0.15f;  // back in neutral (dead zone)
constexpr float SIGN_X = +1.0f;
constexpr float SIGN_Y = +1.0f;

struct Accel {
  int16_t rawX = 0, rawY = 0, rawZ = 0;  // raw counts
  float x = 0, y = 0, z = 0;             // in g
};

class Imu {
public:
  bool ok = false;
  int whoAmI = -1;  // -1 = no response

  Imu() {
    fd = open(I2C_DEV, O_RDWR);
    if (fd < 0 || ioctl(fd, I2C_SLAVE, MPU_ADDR) < 0) return;
    whoAmI = readReg(REG_WHO_AM_I);
    ok = whoAmI == 0x68
      && writeReg(REG_PWR_MGMT_1, 0x01)     // wake up, clock from gyro X
      && writeReg(REG_CONFIG, 0x06)         // low-pass ~5 Hz
      && writeReg(REG_ACCEL_CONFIG, 0x00);  // ±2 g
  }

  bool read(Accel& a) {
    uint8_t reg = REG_ACCEL_XOUT_H, b[6];
    if (::write(fd, &reg, 1) != 1 || ::read(fd, b, 6) != 6) return false;
    a.rawX = int16_t(b[0] << 8 | b[1]);
    a.rawY = int16_t(b[2] << 8 | b[3]);
    a.rawZ = int16_t(b[4] << 8 | b[5]);
    // TODO: if left/right tilt shows up on y instead of x, swap rawX and rawY here
    a.x = SIGN_X * a.rawX / LSB_PER_G;
    a.y = SIGN_Y * a.rawY / LSB_PER_G;
    a.z = a.rawZ / LSB_PER_G;
    return true;
  }

private:
  int fd = -1;

  bool writeReg(uint8_t reg, uint8_t val) {
    const uint8_t b[2] = {reg, val};
    return ::write(fd, b, 2) == 2;
  }
  int readReg(uint8_t reg) {
    uint8_t v = 0;
    return ::write(fd, &reg, 1) == 1 && ::read(fd, &v, 1) == 1 ? v : -1;
  }
};

// ---------- Hysteresis for one axis ----------
// state: -1 / 0 / +1. update() returns ±1 only when leaving neutral (event).
struct Tilt {
  int state = 0;
  int update(float g) {
    if (state != 0) {  // tilted: no new event until back in the dead zone
      if (std::fabs(g) < TILT_OFF_G) state = 0;
      return 0;
    }
    if (g > TILT_ON_G) state = +1;
    if (g < -TILT_ON_G) state = -1;
    return state;
  }
};
