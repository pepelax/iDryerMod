#include "drivers/Sh1106Display.h"

#include <Arduino.h>
#include <Wire.h>

#include <cmath>
#include <cstdio>

namespace {

const char* phaseName(DryingPhase phase) {
  switch (phase) {
    case DryingPhase::Precheck: return "CHECK";
    case DryingPhase::Warmup: return "WARMUP";
    case DryingPhase::Drying: return "DRYING";
    case DryingPhase::Paused: return "PAUSED";
    case DryingPhase::Finish: return "DONE";
    case DryingPhase::Cooldown: return "COOL";
    case DryingPhase::Fault: return "FAULT";
    default: return "READY";
  }
}

const char* faultName(FaultCode fault) {
  switch (fault) {
    case FaultCode::NtcInvalid: return "NTC INVALID";
    case FaultCode::HeaterOverTemperature: return "HEATER HOT";
    case FaultCode::AirSensorInvalid: return "AIR SENSOR";
    case FaultCode::WarmupTimeout: return "WARMUP TIMEOUT";
    case FaultCode::ConfigurationInvalid: return "CONFIGURATION";
    case FaultCode::WatchdogReset: return "WATCHDOG";
    default: return "UNKNOWN";
  }
}

void formatClock(char* output, size_t outputSize, uint32_t seconds) {
  const uint32_t hours = seconds / 3600UL;
  const uint8_t minutes = static_cast<uint8_t>((seconds / 60UL) % 60UL);
  const uint8_t remaining = static_cast<uint8_t>(seconds % 60UL);
  if (hours > 0) {
    std::snprintf(output, outputSize, "%lu:%02u:%02u",
                  static_cast<unsigned long>(hours), minutes, remaining);
  } else {
    std::snprintf(output, outputSize, "%02u:%02u", minutes, remaining);
  }
}

void formatTemperature(char* output, size_t outputSize,
                       const HeaterReading& reading) {
  if (!reading.valid || !std::isfinite(reading.temperatureC)) {
    std::snprintf(output, outputSize, "--.-");
    return;
  }
  std::snprintf(output, outputSize, "%4.1f", reading.temperatureC);
}

void formatTemperature(char* output, size_t outputSize,
                       const AirReading& reading) {
  if (!reading.valid || !std::isfinite(reading.temperatureC)) {
    std::snprintf(output, outputSize, "--.-");
    return;
  }
  std::snprintf(output, outputSize, "%4.1f", reading.temperatureC);
}

void formatHumidity(char* output, size_t outputSize, const AirReading& reading) {
  if (!reading.valid || !std::isfinite(reading.relativeHumidity)) {
    std::snprintf(output, outputSize, "--");
    return;
  }
  std::snprintf(output, outputSize, "%2.0f", reading.relativeHumidity);
}

void drawRight(U8G2& display, const char* text, uint8_t right, int16_t baseline) {
  const int16_t width = display.getStrWidth(text);
  display.drawStr(right - width, baseline, text);
}

void drawFlame(U8G2& display, uint8_t x, uint8_t baseline, bool active,
               bool pulse) {
  display.drawTriangle(x + 4, baseline - 10, x, baseline - 2, x + 8,
                       baseline - 2);
  if (active && pulse) {
    display.drawDisc(x + 4, baseline - 4, 2);
  } else {
    display.drawCircle(x + 4, baseline - 4, 2);
  }
}

void drawFan(U8G2& display, uint8_t x, uint8_t baseline, bool active,
             uint8_t frame) {
  const int16_t centerX = x + 4;
  const int16_t centerY = baseline - 5;
  display.drawCircle(centerX, centerY, 4);
  if (!active) {
    display.drawDisc(centerX, centerY, 1);
    return;
  }
  const uint8_t spoke = frame % 3;
  if (spoke == 0) {
    display.drawLine(centerX, centerY, centerX + 3, centerY - 4);
    display.drawLine(centerX, centerY, centerX - 3, centerY + 3);
  } else if (spoke == 1) {
    display.drawLine(centerX, centerY, centerX - 4, centerY);
    display.drawLine(centerX, centerY, centerX + 3, centerY + 3);
  } else {
    display.drawLine(centerX, centerY, centerX + 4, centerY);
    display.drawLine(centerX, centerY, centerX - 3, centerY - 3);
  }
}

void drawSensorBlock(U8G2& display, uint8_t x, const char* label,
                     const char* value, const char* suffix) {
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(x, 18, label);
  display.setFont(u8g2_font_helvB14_tf);
  char text[16];
  std::snprintf(text, sizeof(text), "%s%s", value, suffix);
  display.drawStr(x, 34, text);
}

}  // namespace

Sh1106Display::Sh1106Display(uint8_t address)
    : display_(U8G2_R0, U8X8_PIN_NONE), address_(address) {}

bool Sh1106Display::begin() {
  Wire.beginTransmission(address_);
  if (Wire.endTransmission() != 0) return false;
  display_.setI2CAddress(static_cast<uint8_t>(address_ << 1));
  return display_.begin();
}

void Sh1106Display::render(const DeviceState& state) {
  render(state, DisplayView::Dashboard, 0);
}

void Sh1106Display::render(const DeviceState& state, DisplayView view,
                           uint8_t selectedMode) {
  display_.clearBuffer();
  display_.setDrawColor(1);
  if (state.fault != FaultCode::None || state.phase == DryingPhase::Fault) {
    renderFault(state);
  } else if (state.phase == DryingPhase::Idle &&
             view == DisplayView::ModeMenu) {
    renderModeMenu(selectedMode);
  } else if (state.phase == DryingPhase::Idle) {
    renderIdle(state);
  } else {
    renderActive(state);
  }
  display_.sendBuffer();
}

void Sh1106Display::renderIdle(const DeviceState& state) {
  char airTemperature[12];
  char humidity[12];
  char ntcTemperature[12];
  formatTemperature(airTemperature, sizeof(airTemperature), state.air);
  formatHumidity(humidity, sizeof(humidity), state.air);
  formatTemperature(ntcTemperature, sizeof(ntcTemperature), state.heater);

  display_.setFont(u8g2_font_6x13B_tf);
  display_.drawStr(0, 11, "READY");
  drawRight(display_, "SH1106", 128, 10);
  drawSensorBlock(display_, 0, "AIR C", airTemperature, "C");
  drawSensorBlock(display_, 69, "RH", humidity, "%");
  display_.setFont(u8g2_font_6x10_tf);
  char line[32];
  std::snprintf(line, sizeof(line), "NTC %sC   HEAT OFF", ntcTemperature);
  display_.drawStr(0, 47, line);
  display_.setFont(u8g2_font_5x7_tf);
  display_.drawStr(0, 62, "PRESS=MENU  ROTATE=MODE");
}

void Sh1106Display::renderModeMenu(uint8_t selectedMode) {
  display_.setFont(u8g2_font_6x13B_tf);
  display_.drawStr(0, 11, "SELECT MODE");
  display_.setFont(u8g2_font_6x10_tf);
  const uint8_t selected = selectedMode % 3;
  for (uint8_t index = 0; index < 3; ++index) {
    const uint8_t top = static_cast<uint8_t>(15 + index * 13);
    if (index == selected) {
      display_.drawBox(0, top, 128, 12);
      display_.setDrawColor(0);
    }
    char line[32];
    if (index == 0) {
      std::snprintf(line, sizeof(line), ">%s", " PRESET 45C 1H");
    } else if (index == 1) {
      std::snprintf(line, sizeof(line), ">%s", " MANUAL 45C 1H");
    } else {
      std::snprintf(line, sizeof(line), ">%s", " CONTINUOUS");
    }
    if (index != selected) line[0] = ' ';
    display_.drawStr(2, top + 9, line);
    display_.setDrawColor(1);
  }
  display_.setFont(u8g2_font_5x7_tf);
  display_.drawStr(0, 63, "ROTATE MODE  CLICK START");
}

void Sh1106Display::renderActive(const DeviceState& state) {
  char clock[16];
  char airTemperature[12];
  char humidity[12];
  char ntcTemperature[12];
  formatClock(clock, sizeof(clock), state.remainingSeconds);
  formatTemperature(airTemperature, sizeof(airTemperature), state.air);
  formatHumidity(humidity, sizeof(humidity), state.air);
  formatTemperature(ntcTemperature, sizeof(ntcTemperature), state.heater);

  display_.setFont(u8g2_font_6x10_tf);
  display_.drawStr(0, 10, phaseName(state.phase));
  drawRight(display_, clock, 128, 10);
  drawSensorBlock(display_, 0, "AIR C", airTemperature, "C");
  drawSensorBlock(display_, 69, "RH", humidity, "%");

  display_.setFont(u8g2_font_6x10_tf);
  char line[32];
  std::snprintf(line, sizeof(line), "NTC %sC  SET %3.0fC", ntcTemperature,
                state.setpoints.airTemperatureC);
  display_.drawStr(0, 47, line);

  const uint8_t animationFrame =
      static_cast<uint8_t>((millis() / 300UL) % 3UL);
  // Keep the animated symbols in their own compact columns so they do not
  // collide with the numeric telemetry on a 128-pixel-wide panel.
  display_.setFont(u8g2_font_6x10_tf);
  drawFlame(display_, 0, 61, state.actuators.heaterPower > 0.5f,
            animationFrame != 0);
  std::snprintf(line, sizeof(line), "H%3.0f%%", state.actuators.heaterPower);
  display_.drawStr(10, 62, line);
  drawFan(display_, 51, 61, state.actuators.fanPower > 0, animationFrame);
  std::snprintf(line, sizeof(line), "F%3u%%",
                static_cast<unsigned>(state.actuators.fanPower));
  display_.drawStr(61, 62, line);
  std::snprintf(line, sizeof(line), "V%3uD",
                static_cast<unsigned>(state.actuators.ventAngle));
  display_.drawStr(96, 62, line);
}

void Sh1106Display::renderFault(const DeviceState& state) {
  display_.setFont(u8g2_font_6x13B_tf);
  display_.drawStr(0, 12, "SAFETY FAULT");
  display_.setFont(u8g2_font_helvB14_tf);
  display_.drawStr(0, 34, faultName(state.fault));
  display_.setFont(u8g2_font_6x10_tf);
  display_.drawStr(0, 50, "HEAT OFF   FAN SAFE");
  display_.drawStr(0, 62, "HOLD: RESET / COOL");
}
