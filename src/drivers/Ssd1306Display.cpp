#include "drivers/Ssd1306Display.h"

#include <cstdio>

namespace {
const char* modeName(DryingMode mode) {
  switch (mode) {
    case DryingMode::TimedPreset: return "PRESET";
    case DryingMode::TimedManual: return "MANUAL";
    case DryingMode::Continuous: return "CONT";
    case DryingMode::Cooldown: return "COOL";
    case DryingMode::Calibration: return "CAL";
    case DryingMode::Fault: return "FAULT";
    default: return "IDLE";
  }
}
}  // namespace

Ssd1306Display::Ssd1306Display(uint8_t address)
    : display_(U8G2_R0, U8X8_PIN_NONE), address_(address) {}

bool Ssd1306Display::begin() {
  display_.setI2CAddress(static_cast<uint8_t>(address_ << 1));
  return display_.begin();
}

void Ssd1306Display::render(const DeviceState& state) {
  char line[32];
  display_.clearBuffer();
  display_.setFont(u8g2_font_6x10_tf);
  std::snprintf(line, sizeof(line), "%s %lus", modeName(state.mode),
                static_cast<unsigned long>(state.remainingSeconds));
  display_.drawStr(0, 10, line);
  std::snprintf(line, sizeof(line), "T %5.1fC RH %4.1f%%", state.air.temperatureC,
                state.air.relativeHumidity);
  display_.drawStr(0, 22, line);
  std::snprintf(line, sizeof(line), "H %5.1fC W %4.0f/%4.0fg", state.heater.temperatureC,
                state.spoolOne.grams, state.spoolTwo.grams);
  display_.drawStr(0, 34, line);
  std::snprintf(line, sizeof(line), "Heat %3.0f%% Fan %3u%%", state.actuators.heaterPower,
                state.actuators.fanPower);
  display_.drawStr(0, 46, line);
  if (state.fault != FaultCode::None) display_.drawStr(0, 60, "SAFETY FAULT");
  display_.sendBuffer();
}
