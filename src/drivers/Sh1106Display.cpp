#include "drivers/Sh1106Display.h"

#include <Arduino.h>
#include <Wire.h>

#include <cmath>
#include <cstdio>

#include "config/AppConfig.h"
#include "config/Defaults.h"

namespace {

// U8g2 _tf fonts are Latin-1 encoded: the degree sign is a single 0xB0 byte.
const char kDegC[] = "\xB0" "C";

constexpr uint8_t kMenuTop = 13;
constexpr uint8_t kMenuRowHeight = 13;
// Presets (6) + MANUAL + CONTINUOUS + BACK.
constexpr uint8_t kMainMenuCapacity = 12;
constexpr uint8_t kLineCapacity = 20;

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

// Short row tag for a preset: "ABS/ASA" -> "ABS", "PC (experimental)" -> "PC".
void shortPresetName(const char* name, char* output, size_t outputSize) {
  size_t index = 0;
  while (name[index] != '\0' && name[index] != '/' && name[index] != ' ' &&
         index + 1 < outputSize) {
    output[index] = name[index];
    ++index;
  }
  output[index] = '\0';
}

void drawRight(U8G2& display, const char* text, uint8_t right, int16_t baseline) {
  const int16_t width = display.getStrWidth(text);
  display.drawStr(right - width, baseline, text);
}

void drawFlame(U8G2& display, uint8_t x, uint8_t baseline, bool active,
               bool blinkOn) {
  // While heating the whole icon blinks so the state is obvious even next to
  // the small percentage readout. When idle the outline icon stays static.
  if (active && !blinkOn) return;
  display.drawTriangle(x + 4, baseline - 10, x, baseline - 2, x + 8,
                       baseline - 2);
  if (active) {
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
  display.drawStr(x, 20, label);
  display.setFont(u8g2_font_helvB14_tf);
  char text[16];
  std::snprintf(text, sizeof(text), "%s%s", value, suffix);
  display.drawStr(x, 36, text);
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
  render(state, UiState{});
}

void Sh1106Display::render(const DeviceState& state, const UiState& ui) {
  display_.clearBuffer();
  display_.setDrawColor(1);
  if (state.fault != FaultCode::None || state.phase == DryingPhase::Fault) {
    renderFault(state);
  } else if (state.calibration.active) {
    renderCalProgress(state);
  } else if (state.phase == DryingPhase::Idle) {
    switch (ui.screen) {
      case UiScreen::MainMenu:
        renderMainMenu(ui);
        break;
      case UiScreen::ManualSetup:
        renderManualSetup(ui);
        break;
      case UiScreen::ContinuousSetup:
        renderContinuousSetup(ui);
        break;
      case UiScreen::CalibrationMenu:
        renderCalibrationMenu(ui);
        break;
      case UiScreen::ScaleSetup:
        renderScaleSetup(state, ui);
        break;
      case UiScreen::DriftConfirm:
        renderDriftConfirm(ui);
        break;
      default:
        renderIdle(state);
        break;
    }
  } else {
    renderActive(state);
  }
  if (ui.toast[0] != '\0') {
    // Transient confirmation message styled as a mini screen: white outer
    // frame, black band, white inner frame, plain screen background and the
    // bold text drawn in regular white. helvB14 is the large bold face used
    // by the sensor values on the dashboard - well spaced and legible.
    display_.setFont(u8g2_font_helvB14_tf);
    const uint8_t textWidth =
        static_cast<uint8_t>(display_.getStrWidth(ui.toast));
    // Generous padding on every side so the frame never crowds the text.
    const uint8_t padX = 14;
    const uint8_t padY = 8;
    const uint8_t boxWidth =
        static_cast<uint8_t>(textWidth + padX * 2 + 6);
    const uint8_t boxX = static_cast<uint8_t>((128 - boxWidth) / 2);
    const uint8_t boxHeight = static_cast<uint8_t>(17 + padY * 2 + 6);
    const uint8_t boxY =
        static_cast<uint8_t>((64 - boxHeight) / 2);
    // Wipe the whole banner rect first: the banner must be fully opaque, no
    // pixels of a filled menu row underneath may bleed through anywhere,
    // including the band between the two frames.
    display_.setDrawColor(0);
    display_.drawBox(boxX, boxY, boxWidth, boxHeight);
    display_.setDrawColor(1);
    display_.drawFrame(boxX, boxY, boxWidth, boxHeight);
    display_.drawFrame(static_cast<uint8_t>(boxX + 2),
                       static_cast<uint8_t>(boxY + 2),
                       static_cast<uint8_t>(boxWidth - 4),
                       static_cast<uint8_t>(boxHeight - 4));
    display_.drawStr(static_cast<uint8_t>(boxX + 3 + padX),
                     static_cast<uint8_t>(boxY + 3 + padY + 13), ui.toast);
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

  display_.setFont(u8g2_font_7x13B_tf);
  display_.drawStr(0, 11, "READY");
  if (state.webAddress[0] != '\0') {
    // Friendly address of the web panel ("dryer.local", or the setup AP IP);
    // "AP " marks the configuration hotspot.
    char network[28];
    if (state.apActive) {
      std::snprintf(network, sizeof(network), "AP %s", state.webAddress);
    } else {
      std::snprintf(network, sizeof(network), "%s", state.webAddress);
    }
    display_.setFont(u8g2_font_6x10_tf);
    drawRight(display_, network, 128, 10);
    display_.setFont(u8g2_font_7x13B_tf);
  }
  drawSensorBlock(display_, 0, "AIR " "\xB0" "C", airTemperature, kDegC);
  drawSensorBlock(display_, 69, "RH", humidity, "%");
  display_.setFont(u8g2_font_6x10_tf);
  char line[32];
  // Two fixed columns (x = 0 and x = 69) keep the layout from wobbling as
  // the numbers change; the RH block above uses the same 69 px gutter.
  std::snprintf(line, sizeof(line), "NTC %s%s", ntcTemperature, kDegC);
  display_.drawStr(0, 49, line);
  display_.drawStr(69, 49, "HEAT OFF");
  if (state.spoolOne.valid || state.spoolTwo.valid) {
    // A small dead band hides load-cell noise when no spool is loaded.
    long one = static_cast<long>(state.spoolOne.grams);
    long two = static_cast<long>(state.spoolTwo.grams);
    if (one > -3 && one < 3) one = 0;
    if (two > -3 && two < 3) two = 0;
    std::snprintf(line, sizeof(line), "S1 %5ldg", one);
    display_.drawStr(0, 62, line);
    std::snprintf(line, sizeof(line), "S2 %5ldg", two);
    display_.drawStr(69, 62, line);
  }
}

void Sh1106Display::drawMenu(const char* title, const char* const* lines,
                             uint8_t count, const UiState& ui, uint8_t editRow,
                             const char* rightTitle) {
  display_.setFont(u8g2_font_7x13B_tf);
  display_.drawStr(0, 11, title);
  if (rightTitle != nullptr) {
    display_.setFont(u8g2_font_6x10_tf);
    drawRight(display_, rightTitle, 128, 10);
  }
  display_.setFont(u8g2_font_6x10_tf);
  const bool blinkOn = (millis() / 400UL) % 2UL == 0UL;
  // Stop the selection bar short of the scroll track so they never overlap.
  const uint8_t barWidth =
      count > defaults::kMenuVisibleRows ? 122 : 128;
  for (uint8_t row = 0;
       row < defaults::kMenuVisibleRows &&
       static_cast<uint8_t>(ui.scrollOffset + row) < count;
       ++row) {
    const uint8_t index = static_cast<uint8_t>(ui.scrollOffset + row);
    const uint8_t top = static_cast<uint8_t>(kMenuTop + row * kMenuRowHeight);
    const bool selected = index == ui.cursor;
    bool inverted = selected;
    // While a field is being edited its row blinks so the state is obvious.
    if (selected && index == editRow && !blinkOn) inverted = false;
    if (inverted) {
      display_.drawBox(0, top, barWidth, 12);
      display_.setDrawColor(0);
    }
    display_.drawStr(2, top + 9, lines[index]);
    display_.setDrawColor(1);
  }
  if (count > defaults::kMenuVisibleRows) {
    const uint8_t trackHeight =
        static_cast<uint8_t>(defaults::kMenuVisibleRows * kMenuRowHeight - 2);
    display_.drawVLine(126, kMenuTop, trackHeight);
    const uint8_t thumbHeight = static_cast<uint8_t>(
        (trackHeight * defaults::kMenuVisibleRows + count - 1) / count);
    // scrollOffset spans 0..(count - visibleRows); dividing by that range
    // (not count - 1) makes the thumb hit the bottom stop on the last row.
    const uint8_t scrollRange = static_cast<uint8_t>(
        count - defaults::kMenuVisibleRows);
    const uint8_t thumbTop = static_cast<uint8_t>(
        kMenuTop +
        ui.scrollOffset * (trackHeight - thumbHeight) / scrollRange);
    display_.drawBox(124, thumbTop, 5, thumbHeight);
  }
}

void Sh1106Display::renderMainMenu(const UiState& ui) {
  size_t presetCount = 0;
  const DryingPreset* presets = defaultPresets(presetCount);
  char buffers[kMainMenuCapacity][kLineCapacity];
  const char* lines[kMainMenuCapacity];
  uint8_t total = 0;
  for (size_t i = 0; i < presetCount && total < kMainMenuCapacity - 4; ++i) {
    char shortName[8];
    shortPresetName(presets[i].name, shortName, sizeof(shortName));
    std::snprintf(buffers[total], kLineCapacity, " %-5s %2.0fC %luH", shortName,
                  presets[i].airTemperatureC,
                  static_cast<unsigned long>(presets[i].durationSeconds / 3600UL));
    lines[total] = buffers[total];
    ++total;
  }
  std::snprintf(buffers[total], kLineCapacity, " MANUAL");
  lines[total] = buffers[total];
  ++total;
  std::snprintf(buffers[total], kLineCapacity, " CONTINUOUS");
  lines[total] = buffers[total];
  ++total;
  std::snprintf(buffers[total], kLineCapacity, " CALIB");
  lines[total] = buffers[total];
  ++total;
  std::snprintf(buffers[total], kLineCapacity, " BACK");
  lines[total] = buffers[total];
  ++total;
  drawMenu("SELECT MODE", lines, total, ui, 255);
}

void Sh1106Display::renderManualSetup(const UiState& ui) {
  char buffers[4][kLineCapacity];
  const char* lines[4];
  const uint32_t hours = ui.manualDurationSeconds / 3600UL;
  const uint32_t minutes = (ui.manualDurationSeconds % 3600UL) / 60UL;
  std::snprintf(buffers[0], kLineCapacity, " Temp    %2.0fC",
                ui.manualTemperatureC);
  std::snprintf(buffers[1], kLineCapacity, " Time    %lu:%02lu",
                static_cast<unsigned long>(hours),
                static_cast<unsigned long>(minutes));
  std::snprintf(buffers[2], kLineCapacity, " Start");
  std::snprintf(buffers[3], kLineCapacity, " Back");
  for (uint8_t i = 0; i < 4; ++i) lines[i] = buffers[i];
  const uint8_t editRow = ui.editField == 1 ? 0 : (ui.editField == 2 ? 1 : 255);
  drawMenu("MANUAL", lines, 4, ui, editRow);
}

void Sh1106Display::renderContinuousSetup(const UiState& ui) {
  char buffers[3][kLineCapacity];
  const char* lines[3];
  std::snprintf(buffers[0], kLineCapacity, " Temp    %2.0fC",
                ui.continuousTemperatureC);
  std::snprintf(buffers[1], kLineCapacity, " Start");
  std::snprintf(buffers[2], kLineCapacity, " Back");
  for (uint8_t i = 0; i < 3; ++i) lines[i] = buffers[i];
  const uint8_t editRow = ui.editField == 1 ? 0 : 255;
  drawMenu("CONTINUOUS", lines, 3, ui, editRow);
}

void Sh1106Display::renderCalibrationMenu(const UiState& ui) {
  char buffers[5][kLineCapacity];
  const char* lines[5];
  std::snprintf(buffers[0], kLineCapacity, " Tare");
  std::snprintf(buffers[1], kLineCapacity, " Scale S1");
  std::snprintf(buffers[2], kLineCapacity, " Scale S2");
  std::snprintf(buffers[3], kLineCapacity, " Drift cal");
  std::snprintf(buffers[4], kLineCapacity, " Back");
  for (uint8_t i = 0; i < 5; ++i) lines[i] = buffers[i];
  drawMenu("CALIBRATION", lines, 5, ui, 255);
}

void Sh1106Display::renderScaleSetup(const DeviceState& state,
                                     const UiState& ui) {
  // Raw value lives in the title row: a separate menu line would push the
  // footer hint onto the Back button.
  const WeightReading& reading =
      ui.targetSpool == 1 ? state.spoolTwo : state.spoolOne;
  char buffers[3][kLineCapacity];
  const char* lines[3];
  std::snprintf(buffers[0], kLineCapacity, " Weight %ug",
                static_cast<unsigned>(ui.knownGrams));
  std::snprintf(buffers[1], kLineCapacity, " Save");
  std::snprintf(buffers[2], kLineCapacity, " Back");
  for (uint8_t i = 0; i < 3; ++i) lines[i] = buffers[i];
  char title[8];
  std::snprintf(title, sizeof(title), "S%u",
                static_cast<unsigned>(ui.targetSpool + 1));
  char right[16];
  if (reading.valid) {
    std::snprintf(right, sizeof(right), "RAW %ld",
                  static_cast<long>(reading.raw));
  } else {
    std::snprintf(right, sizeof(right), "NO DATA");
  }
  const uint8_t editRow = ui.editField == 1 ? 0 : 255;
  drawMenu(title, lines, 3, ui, editRow, right);
  display_.setFont(u8g2_font_5x7_tf);
  display_.drawStr(0, 63, reading.valid ? "PUT KNOWN MASS ON SPOOL"
                                        : "SPOOL SENSOR MISSING");
}

void Sh1106Display::renderDriftConfirm(const UiState& ui) {
  char buffers[2][kLineCapacity];
  const char* lines[2];
  std::snprintf(buffers[0], kLineCapacity, " Start");
  std::snprintf(buffers[1], kLineCapacity, " Back");
  lines[0] = buffers[0];
  lines[1] = buffers[1];
  drawMenu("DRIFT CAL", lines, 2, ui, 255);
  display_.setFont(u8g2_font_5x7_tf);
  display_.drawStr(0, 46, "NEEDS KNOWN MASS ON SCALE");
  display_.drawStr(0, 55, "HEATS + COOLS: TAKES HOURS");
  display_.drawStr(0, 64, "HOLD CANCELS");
}

void Sh1106Display::renderCalProgress(const DeviceState& state) {
  const bool heating = state.calibration.phase == 1;
  char airTemperature[12];
  formatTemperature(airTemperature, sizeof(airTemperature), state.air);

  display_.setFont(u8g2_font_7x13B_tf);
  display_.drawStr(0, 11, heating ? "CALIB HEAT" : "CALIB COOL");
  char target[16];
  std::snprintf(target, sizeof(target), "-> %2.0fC",
                state.calibration.targetTempC);
  drawRight(display_, target, 128, 10);

  display_.setFont(u8g2_font_helvB14_tf);
  char temperature[16];
  std::snprintf(temperature, sizeof(temperature), "%s%s", airTemperature,
                kDegC);
  display_.drawStr(0, 34, temperature);

  display_.setFont(u8g2_font_6x10_tf);
  char line[32];
  std::snprintf(line, sizeof(line), "Pts %u   Start %2.0fC",
                static_cast<unsigned>(state.calibration.points),
                state.calibration.startTempC);
  display_.drawStr(0, 47, line);
  display_.setFont(u8g2_font_5x7_tf);
  display_.drawStr(0, 63, "HOLD=CANCEL  KEEP MASS STILL");
}

void Sh1106Display::renderActive(const DeviceState& state) {
  char clock[16];
  char airTemperature[12];
  char humidity[12];
  char ntcTemperature[12];
  if (state.mode == DryingMode::Continuous) {
    const uint32_t elapsed =
        state.runStartedAt != 0 ? (millis() - state.runStartedAt) / 1000UL : 0;
    formatClock(clock, sizeof(clock), elapsed);
  } else {
    formatClock(clock, sizeof(clock), state.remainingSeconds);
  }
  formatTemperature(airTemperature, sizeof(airTemperature), state.air);
  formatHumidity(humidity, sizeof(humidity), state.air);
  formatTemperature(ntcTemperature, sizeof(ntcTemperature), state.heater);

  display_.setFont(u8g2_font_6x10_tf);
  char header[24];
  std::snprintf(header, sizeof(header), "%s %s", phaseName(state.phase),
                state.runLabel);
  display_.drawStr(0, 10, header);
  drawRight(display_, clock, 128, 10);
  drawSensorBlock(display_, 0, "AIR " "\xB0" "C", airTemperature, kDegC);
  drawSensorBlock(display_, 69, "RH", humidity, "%");

  display_.setFont(u8g2_font_6x10_tf);
  char line[32];
  std::snprintf(line, sizeof(line), "NTC %s%s", ntcTemperature, kDegC);
  display_.drawStr(0, 49, line);
  std::snprintf(line, sizeof(line), "SET %3.0f%s",
                state.setpoints.airTemperatureC, kDegC);
  display_.drawStr(69, 49, line);

  const uint8_t animationFrame =
      static_cast<uint8_t>((millis() / 300UL) % 3UL);
  // Keep the animated symbols in their own compact columns so they do not
  // collide with the numeric telemetry on a 128-pixel-wide panel.
  display_.setFont(u8g2_font_6x10_tf);
  const bool heaterActive = state.actuators.heaterPower > 0.5f;
  drawFlame(display_, 0, 61, heaterActive, (millis() / 500UL) % 2UL == 0UL);
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
  display_.setFont(u8g2_font_7x13B_tf);
  display_.drawStr(0, 12, "SAFETY FAULT");
  display_.setFont(u8g2_font_helvB14_tf);
  display_.drawStr(0, 34, faultName(state.fault));
  display_.setFont(u8g2_font_6x10_tf);
  display_.drawStr(0, 50, "HEAT OFF   FAN SAFE");
  display_.drawStr(0, 62, "HOLD: RESET / COOL");
}
