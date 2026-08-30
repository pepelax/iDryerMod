#include "drivers/RotaryEncoder.h"

#include <Arduino.h>

RotaryEncoderInput::RotaryEncoderInput(uint8_t pinA, uint8_t pinB, uint8_t buttonPin)
    : pinA_(pinA), pinB_(pinB), buttonPin_(buttonPin) {}

bool RotaryEncoderInput::begin() {
  pinMode(pinA_, INPUT_PULLUP);
  pinMode(pinB_, INPUT_PULLUP);
  encoder_.attachHalfQuad(pinA_, pinB_);
  encoder_.setCount(0);
  pinMode(buttonPin_, INPUT_PULLUP);
  return true;
}

void RotaryEncoderInput::update(uint32_t now) {
  const int64_t count = encoder_.getCount();
  delta_ = static_cast<int32_t>(count - previousCount_);
  previousCount_ = count;

  const bool pressed = digitalRead(buttonPin_) == LOW;
  if (pressed && !previousPressed_) {
    pressedAt_ = now;
    longReported_ = false;
  }
  if (pressed && !longReported_ &&
      static_cast<uint32_t>(now - pressedAt_) >= 1200UL) {
    longPressPending_ = true;
    longReported_ = true;
  }
  if (!pressed && previousPressed_ && !longReported_) shortPressPending_ = true;
  previousPressed_ = pressed;
}

int32_t RotaryEncoderInput::encoderDelta() {
  const int32_t value = delta_;
  delta_ = 0;
  return value;
}

bool RotaryEncoderInput::shortPress() {
  const bool value = shortPressPending_;
  shortPressPending_ = false;
  return value;
}

bool RotaryEncoderInput::longPress() {
  const bool value = longPressPending_;
  longPressPending_ = false;
  return value;
}
