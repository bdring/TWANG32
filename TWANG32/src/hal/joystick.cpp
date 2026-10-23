#include "joystick.h"
#include <Wire.h>

static const uint8_t MPU_ADDR = 0x68;

static void writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission(true);
}

bool Joystick::begin(int sda, int scl) {
  Wire.begin(sda, scl);
  Wire.setClock(400000);
  Wire.setTimeOut(5);
  writeReg(0x6B, 0x00);  // PWR_MGMT_1: wake up
  delay(5);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);  // WHO_AM_I
  bool ok = Wire.endTransmission(false) == 0;
  if (ok && Wire.requestFrom(MPU_ADDR, (uint8_t)1) == 1) whoami_ = Wire.read();
  // 0x68 = MPU6050, 0x70 = MPU6500, 0x71 = MPU9250, 0x98 = some clones
  present_ = ok && whoami_ != 0 && whoami_ != 0xFF;
  return present_;
}

bool Joystick::readRaw(int16_t* a, int16_t* g) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)14) != 14) return false;
  for (int i = 0; i < 3; i++) a[i] = (int16_t)(Wire.read() << 8 | Wire.read());
  Wire.read();
  Wire.read();  // temperature
  for (int i = 0; i < 3; i++) g[i] = (int16_t)(Wire.read() << 8 | Wire.read());
  return true;
}

static int median5(const int* v) {
  int s[5];
  for (int i = 0; i < 5; i++) s[i] = v[i];
  for (int i = 1; i < 5; i++)
    for (int j = i; j > 0 && s[j - 1] > s[j]; j--) { int t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
  return s[2];
}

tw::Input Joystick::read(const JoystickConfig& cfg) {
  tw::Input in;
  int16_t a[3], g[3];
  if (!readRaw(a, g)) {
    if (++failures_ > 30) present_ = false;
    return in;
  }
  failures_ = 0;
  present_ = true;
  int axis = cfg.axis > 2 ? 1 : cfg.axis;
  // tilt: same scaling as the original (raw / 166 is roughly degrees), minus calibration
  int t = (a[axis] - cfg.offset[axis]) / 166;
  int dz = cfg.deadzone;
  if (abs(t) < dz) t = 0;
  else t += t > 0 ? -dz : dz;
  tiltHist_[histPos_] = t;
  wobbleHist_[histPos_] = abs((int)g[axis]);
  histPos_ = (histPos_ + 1) % 5;
  int tilt = median5(tiltHist_);
  int wobble = 0;
  for (int i = 0; i < 5; i++) wobble = max(wobble, wobbleHist_[i]);
  // original default (JOYSTICK_DIRECTION 1) moves forward on negative readings
  in.tilt = (float)(cfg.invert ? tilt : -tilt);
  in.attack = wobble >= cfg.attackThreshold;
  in.active = tilt != 0 || in.attack;
  lastTilt_ = in.tilt;
  lastWobble_ = wobble;
  return in;
}

void Joystick::calibrate(JoystickConfig& cfg) {
  long sum[3] = {0, 0, 0};
  int n = 0;
  for (int i = 0; i < 32; i++) {
    int16_t a[3], g[3];
    if (readRaw(a, g)) {
      for (int k = 0; k < 3; k++) sum[k] += a[k];
      n++;
    }
    delay(4);
  }
  if (!n) return;
  // only the two axes perpendicular to gravity are meaningful as "level"; store all three
  for (int k = 0; k < 3; k++) cfg.offset[k] = (int16_t)(sum[k] / n);
  // the axis we tilt on should read zero at rest; leave gravity's axis alone
  for (int k = 0; k < 3; k++) if (abs(cfg.offset[k]) > 12000) cfg.offset[k] = 0;
  for (int i = 0; i < 5; i++) tiltHist_[i] = 0;
}
