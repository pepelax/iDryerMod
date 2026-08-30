#pragma once

#include <Arduino.h>

namespace board {

constexpr uint8_t kI2cSda = 21;
constexpr uint8_t kI2cScl = 22;
constexpr uint32_t kI2cFrequency = 400000;
constexpr uint8_t kAht30Address = 0x38;
constexpr uint8_t kDisplayAddress = 0x3C;

constexpr uint8_t kEncoderA = 32;
constexpr uint8_t kEncoderB = 33;
constexpr uint8_t kButton = 25;

constexpr uint8_t kHeater = 26;
constexpr uint8_t kFan = 27;
constexpr uint8_t kServo = 14;
constexpr uint8_t kNtc = 34;

constexpr uint8_t kHx711OneDout = 16;
constexpr uint8_t kHx711OneSck = 17;
constexpr uint8_t kHx711TwoDout = 18;
constexpr uint8_t kHx711TwoSck = 19;

constexpr uint8_t kHeaterPwmChannel = 0;
constexpr uint8_t kFanPwmChannel = 1;
constexpr uint16_t kFanPwmFrequency = 20000;
constexpr uint8_t kFanPwmResolution = 10;
constexpr uint16_t kServoFrequency = 50;
constexpr uint16_t kServoMinUs = 500;
constexpr uint16_t kServoMaxUs = 2500;

}  // namespace board
