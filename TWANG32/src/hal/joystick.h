// TWANG Next - the doorstop-spring joystick (MPU6050 accelerometer + gyro)
#pragma once
#include <Arduino.h>
#include "../core/tw_game.h"

struct JoystickConfig {
  uint8_t axis = 1;              // 0 = X, 1 = Y, 2 = Z (original JOYSTICK_ORIENTATION)
  bool invert = false;           // flips direction (original JOYSTICK_DIRECTION)
  uint8_t deadzone = 8;          // in tilt units (~1 per degree)
  uint16_t attackThreshold = 30000;  // gyro magnitude that counts as a twang
  int16_t offset[3] = {0, 0, 0};     // calibration: raw accel reading at rest per axis
};

class Joystick {
 public:
  bool begin(int sda, int scl);
  tw::Input read(const JoystickConfig& cfg);  // call once per game tick
  void calibrate(JoystickConfig& cfg);        // capture the resting position as "level"
  bool present() const { return present_; }
  uint8_t whoAmI() const { return whoami_; }
  float lastTilt() const { return lastTilt_; }
  int lastWobble() const { return lastWobble_; }

 private:
  bool readRaw(int16_t* a, int16_t* g);
  bool present_ = false;
  uint8_t whoami_ = 0;
  int tiltHist_[5] = {0};
  int wobbleHist_[5] = {0};
  int histPos_ = 0;
  float lastTilt_ = 0;
  int lastWobble_ = 0;
  uint32_t failures_ = 0;
};
