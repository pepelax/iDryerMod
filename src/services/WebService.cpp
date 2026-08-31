#include "services/WebService.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "config/Defaults.h"

namespace {
const char* modeName(DryingMode mode) {
  switch (mode) {
    case DryingMode::TimedPreset: return "timed_preset";
    case DryingMode::TimedManual: return "timed_manual";
    case DryingMode::Continuous: return "continuous";
    case DryingMode::Cooldown: return "cooldown";
    case DryingMode::Calibration: return "calibration";
    case DryingMode::Fault: return "fault";
    default: return "idle";
  }
}

DryingMode parseMode(const String& value) {
  if (value == "timed_manual") return DryingMode::TimedManual;
  if (value == "continuous") return DryingMode::Continuous;
  return DryingMode::TimedPreset;
}

const char* phaseName(DryingPhase phase) {
  switch (phase) {
    case DryingPhase::Precheck: return "precheck";
    case DryingPhase::Warmup: return "warmup";
    case DryingPhase::Drying: return "drying";
    case DryingPhase::Hold: return "hold";
    case DryingPhase::Paused: return "paused";
    case DryingPhase::Finish: return "finish";
    case DryingPhase::Cooldown: return "cooldown";
    case DryingPhase::Fault: return "fault";
    default: return "idle";
  }
}

const char* faultName(FaultCode fault) {
  switch (fault) {
    case FaultCode::NtcInvalid: return "ntc_invalid";
    case FaultCode::HeaterOverTemperature: return "heater_overtemperature";
    case FaultCode::AirSensorInvalid: return "air_sensor_invalid";
    case FaultCode::WeightSensorOneInvalid: return "weight1_invalid";
    case FaultCode::WeightSensorTwoInvalid: return "weight2_invalid";
    case FaultCode::WarmupTimeout: return "warmup_timeout";
    case FaultCode::ConfigurationInvalid: return "configuration_invalid";
    case FaultCode::WatchdogReset: return "watchdog_reset";
    default: return "none";
  }
}
}  // namespace

WebService::WebService(DeviceState& state, AppConfig& config,
                       DryingStateMachine& stateMachine, IStorage& storage,
                       CalibrationService& calibration)
    : state_(state),
      config_(config),
      stateMachine_(stateMachine),
      storage_(storage),
      calibration_(calibration) {}

bool WebService::authorize() {
  // Local-network appliance: an empty password means open access.
  if (config_.webPassword[0] == '\0') return true;
  // Accept the session cookie first so fetch() calls keep working even if
  // the browser does not replay cached Basic-auth credentials.
  if (server_.hasHeader("Cookie") &&
      server_.header("Cookie").indexOf("dsession=" + sessionToken_) >= 0) {
    return true;
  }
  const char* login = config_.webLogin[0] != '\0' ? config_.webLogin : "admin";
  if (server_.authenticate(login, config_.webPassword)) {
    server_.sendHeader("Set-Cookie", "dsession=" + sessionToken_);
    return true;
  }
  server_.requestAuthentication();
  return false;
}

bool WebService::begin() {
  char token[17];
  std::snprintf(token, sizeof(token), "%08x%08x", esp_random(), esp_random());
  sessionToken_ = String(token);
  const char* collectedHeaders[] = {"Cookie"};
  server_.collectHeaders(collectedHeaders, 1);
  server_.on("/", HTTP_GET, [this]() {
    if (!authorize()) return;
    if (!LittleFS.exists("/index.html")) {
      server_.send(404, "text/plain", "UI not installed");
      return;
    }
    File file = LittleFS.open("/index.html", FILE_READ);
    server_.streamFile(file, "text/html");
    file.close();
  });
  server_.on("/api/state", HTTP_GET, [this]() { if (authorize()) sendState(); });
  server_.on("/api/config", HTTP_GET, [this]() { if (authorize()) sendConfig(); });
  server_.on("/api/config", HTTP_PUT, [this]() { if (authorize()) saveConfig(); });
  server_.on("/api/run", HTTP_POST, [this]() { if (authorize()) runCommand(); });
  server_.on("/api/stop", HTTP_POST, [this]() { if (authorize()) stopCommand(); });
  server_.on("/api/pause", HTTP_POST, [this]() { if (authorize()) pauseCommand(); });
  server_.on("/api/calibration", HTTP_POST,
             [this]() { if (authorize()) calibrationCommand(); });
  server_.on("/api/calibration", HTTP_GET,
             [this]() { if (authorize()) sendCalibration(); });
  server_.on("/api/history", HTTP_GET, [this]() { if (authorize()) sendHistory(); });
  server_.on("/api/events", HTTP_GET, [this]() { if (authorize()) sendEvents(); });
  server_.on("/api/scan", HTTP_GET, [this]() { if (authorize()) scanCommand(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { if (authorize()) rebootCommand(); });
  server_.on(
      "/api/ota", HTTP_POST,
      [this]() {
        if (!authorize()) return;
        const bool ok = !Update.hasError();
        server_.send(ok ? 200 : 500, "application/json",
                     ok ? "{\"ok\":true,\"rebooting\":true}"
                        : "{\"ok\":false}");
        if (ok) {
          appendEvent("ota", "firmware uploaded");
          delay(100);
          ESP.restart();
        }
      },
      [this]() {
        if (!authorize()) return;
        HTTPUpload& upload = server_.upload();
        if (upload.status == UPLOAD_FILE_START) {
          state_.otaInProgress = true;
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (!Update.end(true)) Update.printError(Serial);
          state_.otaInProgress = false;
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          Update.abort();
          state_.otaInProgress = false;
        }
      });
  // Static files must be registered last: the WebServer picks the first
  // handler whose canHandle() matches, and serveStatic claims every GET
  // request under "/" — including /api/* — even when no such file exists.
  server_.serveStatic("/", LittleFS, "/", "no-cache");
  server_.begin();
  return true;
}

void WebService::update() { server_.handleClient(); }

void WebService::sendState() {
  StaticJsonDocument<1536> doc;
  doc["version"] = APP_VERSION;
  doc["mode"] = modeName(state_.mode);
  doc["phase"] = phaseName(state_.phase);
  doc["fault"] = faultName(state_.fault);
  doc["runLabel"] = state_.runLabel;
  doc["remainingSeconds"] = state_.remainingSeconds;
  doc["wifiConnected"] = state_.wifiConnected;
  doc["apActive"] = state_.apActive;
  doc["ip"] = state_.ipAddress;
  doc["hostname"] = config_.hostname;
  doc["setpoints"]["airTemperatureC"] = state_.setpoints.airTemperatureC;
  doc["setpoints"]["relativeHumidity"] = state_.setpoints.relativeHumidity;
  doc["setpoints"]["durationSeconds"] = state_.setpoints.durationSeconds;
  uint32_t elapsedSeconds = 0;
  if (state_.runStartedAt != 0) {
    elapsedSeconds = static_cast<uint32_t>((millis() - state_.runStartedAt) / 1000UL);
  }
  doc["elapsedSeconds"] = elapsedSeconds;
  doc["air"]["valid"] = state_.air.valid;
  doc["air"]["temperatureC"] = state_.air.temperatureC;
  doc["air"]["relativeHumidity"] = state_.air.relativeHumidity;
  doc["air"]["absoluteHumidityGm3"] = state_.air.absoluteHumidityGm3;
  doc["heater"]["valid"] = state_.heater.valid;
  doc["heater"]["raw"] = state_.heater.raw;
  doc["heater"]["temperatureC"] = state_.heater.temperatureC;
  doc["weights"]["oneValid"] = state_.spoolOne.valid;
  doc["weights"]["twoValid"] = state_.spoolTwo.valid;
  doc["weights"]["one"] = state_.spoolOne.grams;
  doc["weights"]["two"] = state_.spoolTwo.grams;
  doc["weights"]["total"] = state_.spoolOne.grams + state_.spoolTwo.grams;
  doc["outputs"]["heater"] = state_.actuators.heaterPower;
  doc["outputs"]["heaterSetpointC"] = state_.heaterSetpointC;
  doc["outputs"]["fan"] = state_.actuators.fanPower;
  doc["outputs"]["ventAngle"] = state_.actuators.ventAngle;
  // Dryness telemetry: moisture-release slope from the sealed-window AH
  // regression, the pulse-ventilation phase (0 sealed, 1 settle, 2 purge)
  // and the consecutive quiet-window streak that ends in Hold.
  doc["dryness"]["ahSlopeGm3PerHour"] = state_.ahSlopeGm3PerHour;
  doc["dryness"]["ahSlopeSamples"] = state_.ahSlopeSamples;
  doc["dryness"]["purgePhase"] = state_.purgePhase;
  doc["dryness"]["stableWindows"] = state_.drynessChecks;
  doc["dryness"]["slopeThreshold"] = state_.setpoints.drynessSlopeGm3PerHour;
  doc["setpoints"]["holdTemperatureC"] = state_.setpoints.holdTemperatureC;
  doc["setpoints"]["minDurationSeconds"] = state_.setpoints.minDurationSeconds;
  doc["setpoints"]["maxDurationSeconds"] = state_.setpoints.maxDurationSeconds;
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebService::sendConfig() {
  StaticJsonDocument<2048> doc;
  doc["hostname"] = config_.hostname;
  doc["wifiSsid"] = config_.wifiSsid;
  // The password itself is never exposed; only whether one is configured.
  doc["webLogin"] = config_.webLogin;
  doc["hasWebPassword"] = config_.webPassword[0] != '\0';
  doc["heaterMaxTemperatureC"] = config_.heaterMaxTemperatureC;
  doc["airMaxTemperatureC"] = config_.airMaxTemperatureC;
  doc["fanMinimumDuty"] = config_.fanMinimumDuty;
  doc["servoMinUs"] = config_.servoMinUs;
  doc["servoMaxUs"] = config_.servoMaxUs;
  JsonArray presets = doc.createNestedArray("presets");
  size_t count = 0;
  const DryingPreset* defaultsList = defaultPresets(count);
  for (size_t i = 0; i < count; ++i) {
    JsonObject item = presets.createNestedObject();
    item["id"] = defaultsList[i].id;
    item["name"] = defaultsList[i].name;
    item["temperatureC"] = defaultsList[i].airTemperatureC;
    item["holdTemperatureC"] = defaultsList[i].holdTemperatureC;
    item["relativeHumidity"] = defaultsList[i].relativeHumidity;
    item["drynessSlopeGm3PerHour"] = defaultsList[i].drynessSlopeGm3PerHour;
    item["minDurationSeconds"] = defaultsList[i].minDurationSeconds;
    item["maxDurationSeconds"] = defaultsList[i].maxDurationSeconds;
  }
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebService::runCommand() {
  StaticJsonDocument<512> doc;
  if (server_.hasArg("plain")) deserializeJson(doc, server_.arg("plain"));
  String modeValue = "timed_manual";
  if (doc.containsKey("mode")) modeValue = doc["mode"].as<const char*>();
  const DryingMode mode = parseMode(modeValue);

  float temperature = 45.0f;
  float humidity = 20.0f;
  float holdTemperature = 0.0f;
  float drynessSlope = 0.0f;
  uint32_t duration = 3600UL;
  uint32_t minDuration = 0UL;
  uint32_t maxDuration = 0UL;
  float heaterLimit = config_.heaterMaxTemperatureC;
  char label[24] = "";
  bool fromPreset = false;
  VentilationPlan ventilation;
  if (mode == DryingMode::TimedPreset && doc.containsKey("preset")) {
    const String presetId = doc["preset"].as<const char*>();
    size_t count = 0;
    const DryingPreset* presets = defaultPresets(count);
    for (size_t i = 0; i < count; ++i) {
      if (presetId == presets[i].id) {
        temperature = presets[i].airTemperatureC;
        humidity = presets[i].relativeHumidity;
        heaterLimit = presets[i].heaterMaxTemperatureC;
        ventilation = toVentilationPlan(presets[i].ventilation);
        minDuration = presets[i].minDurationSeconds;
        maxDuration = presets[i].maxDurationSeconds;
        holdTemperature = presets[i].holdTemperatureC;
        drynessSlope = presets[i].drynessSlopeGm3PerHour;
        strlcpy(label, presets[i].name, sizeof(label));
        fromPreset = true;
        break;
      }
    }
  }
  if (!fromPreset) {
    if (doc.containsKey("temperatureC")) temperature = doc["temperatureC"].as<float>();
    if (doc.containsKey("relativeHumidity")) humidity = doc["relativeHumidity"].as<float>();
    if (doc.containsKey("durationSeconds")) duration = doc["durationSeconds"].as<uint32_t>();
  }
  Setpoints setpoints;
  setpoints.airTemperatureC =
      std::max(20.0f, std::min(config_.airMaxTemperatureC, temperature));
  setpoints.holdTemperatureC =
      std::max(0.0f, std::min(config_.airMaxTemperatureC, holdTemperature));
  setpoints.relativeHumidity = humidity;
  setpoints.drynessSlopeGm3PerHour = std::max(0.0f, drynessSlope);
  setpoints.heaterLimitC = heaterLimit;
  setpoints.durationSeconds = mode == DryingMode::Continuous ? 0UL : duration;
  setpoints.minDurationSeconds = minDuration;
  setpoints.maxDurationSeconds =
      mode == DryingMode::Continuous ? 0UL : maxDuration;
  setpoints.ventilation = ventilation;
  const bool accepted = stateMachine_.start(state_, mode, setpoints, millis());
  if (accepted) {
    if (label[0] != '\0') {
      strlcpy(state_.runLabel, label, sizeof(state_.runLabel));
    }
    appendEvent("run", modeName(mode));
  }
  server_.send(accepted ? 200 : 400, "application/json",
               accepted ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WebService::stopCommand() {
  stateMachine_.stop(state_, millis());
  appendEvent("stop", "stopped by web");
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WebService::pauseCommand() {
  // Toggle: the same endpoint resumes a paused run so the web UI needs no
  // separate "continue" command.
  const bool wasPaused = state_.phase == DryingPhase::Paused;
  if (wasPaused) {
    stateMachine_.resume(state_, millis());
  } else {
    stateMachine_.pause(state_, millis());
  }
  appendEvent(wasPaused ? "resume" : "pause",
              wasPaused ? "resumed by web" : "paused by web");
  String body = String("{\"ok\":true,\"paused\":") +
                (wasPaused ? "false" : "true") + "}";
  server_.send(200, "application/json", body);
}

void WebService::sendCalibration() {
  StaticJsonDocument<2048> doc;
  doc["active"] = state_.calibration.active;
  doc["phase"] = state_.calibration.phase == 1
                     ? "heat"
                     : (state_.calibration.phase == 2 ? "cool" : "idle");
  doc["points"] = state_.calibration.points;
  doc["targetTempC"] = state_.calibration.targetTempC;
  doc["startTempC"] = state_.calibration.startTempC;
  doc["airTempC"] = state_.air.temperatureC;
  const float scales[2] = {config_.weightScaleOne, config_.weightScaleTwo};
  const int32_t tares[2] = {config_.weightTareOne, config_.weightTareTwo};
  const WeightReading* readings[2] = {&state_.spoolOne, &state_.spoolTwo};
  JsonArray spools = doc.createNestedArray("spools");
  for (uint8_t spool = 0; spool < 2; ++spool) {
    JsonObject item = spools.createNestedObject();
    item["present"] = readings[spool]->valid;
    item["raw"] = readings[spool]->raw;
    item["grams"] = readings[spool]->grams;
    item["scale"] = scales[spool];
    item["tareRaw"] = tares[spool];
    const WeightCalTable& table = calibration_.table(spool);
    item["calValid"] = table.valid;
    item["calBands"] = table.filledBands;
    JsonArray coeff = item.createNestedArray("coeff");
    for (uint8_t band = 0; band < kWeightCalBands; ++band) {
      coeff.add(table.coeff[band]);
    }
  }
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebService::calibrationCommand() {
  StaticJsonDocument<256> doc;
  if (!server_.hasArg("plain") ||
      deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  const String action = doc["action"] | "";
  bool accepted = true;
  String detail = action;
  if (action == "tare") {
    accepted = calibration_.tare(state_);
    if (accepted) appendEvent("calibration", "tare by web");
  } else if (action == "scale") {
    const uint8_t spool = doc["spool"] | 0;
    const float grams = doc["knownGrams"] | 0.0f;
    accepted = calibration_.applyKnownWeight(state_, spool, grams);
    if (accepted) appendEvent("calibration", "scale set by web");
  } else if (action == "drift_start") {
    accepted = calibration_.startDrift(state_, millis());
    if (accepted) appendEvent("calibration", "drift run started by web");
    detail = accepted ? "drift started" : "cannot start (sensors or air)";
  } else if (action == "drift_cancel") {
    calibration_.cancelDrift(state_);
    appendEvent("calibration", "drift run cancelled by web");
  } else {
    accepted = false;
    detail = "unknown action";
  }
  server_.send(accepted ? 200 : 400, "application/json",
               accepted ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WebService::saveConfig() {
  StaticJsonDocument<768> doc;
  if (!server_.hasArg("plain") ||
      deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  if (doc.containsKey("heaterMaxTemperatureC")) {
    config_.heaterMaxTemperatureC =
        std::max(50.0f, std::min(115.0f, doc["heaterMaxTemperatureC"].as<float>()));
  }
  if (doc.containsKey("airMaxTemperatureC")) {
    config_.airMaxTemperatureC =
        std::max(40.0f, std::min(80.0f, doc["airMaxTemperatureC"].as<float>()));
  }
  if (doc.containsKey("fanMinimumDuty")) {
    config_.fanMinimumDuty =
        std::max(0.0f, std::min(100.0f, doc["fanMinimumDuty"].as<float>()));
  }
  bool weightsTouched = false;
  if (doc.containsKey("weightScaleOne")) {
    const float scale = doc["weightScaleOne"].as<float>();
    if (scale > 0.01f && scale < 1000000.0f) {
      config_.weightScaleOne = scale;
      weightsTouched = true;
    }
  }
  if (doc.containsKey("weightScaleTwo")) {
    const float scale = doc["weightScaleTwo"].as<float>();
    if (scale > 0.01f && scale < 1000000.0f) {
      config_.weightScaleTwo = scale;
      weightsTouched = true;
    }
  }
  if (doc.containsKey("weightTareOne")) {
    config_.weightTareOne = doc["weightTareOne"].as<int32_t>();
    weightsTouched = true;
  }
  if (doc.containsKey("weightTareTwo")) {
    config_.weightTareTwo = doc["weightTareTwo"].as<int32_t>();
    weightsTouched = true;
  }
  if (doc.containsKey("wifiSsid")) {
    strlcpy(config_.wifiSsid, doc["wifiSsid"] | "", sizeof(config_.wifiSsid));
  }
  if (doc.containsKey("wifiPassword")) {
    strlcpy(config_.wifiPassword, doc["wifiPassword"] | "", sizeof(config_.wifiPassword));
  }
  if (doc.containsKey("hostname")) {
    strlcpy(config_.hostname, doc["hostname"] | "dryer",
            sizeof(config_.hostname));
  }
  if (doc.containsKey("webLogin")) {
    strlcpy(config_.webLogin, doc["webLogin"] | "", sizeof(config_.webLogin));
  }
  // An empty password disables authentication for the local network.
  if (doc.containsKey("webPassword")) {
    strlcpy(config_.webPassword, doc["webPassword"] | "",
            sizeof(config_.webPassword));
  }
  storage_.saveConfig(config_);
  if (weightsTouched) calibration_.syncSensors();
  appendEvent("config", "configuration changed");
  server_.send(200, "application/json", "{\"ok\":true,\"rebootRequired\":true}");
}

void WebService::sendHistory() {
  File file = LittleFS.open("/telemetry.jsonl", FILE_READ);
  if (!file) {
    server_.send(200, "application/x-ndjson", "");
    return;
  }
  server_.streamFile(file, "application/x-ndjson");
  file.close();
}

void WebService::sendEvents() {
  File file = LittleFS.open("/events.jsonl", FILE_READ);
  if (!file) {
    server_.send(200, "application/x-ndjson", "");
    return;
  }
  server_.streamFile(file, "application/x-ndjson");
  file.close();
}

void WebService::scanCommand() {
  // Async Wi-Fi scan: the first call starts it, later calls collect results.
  if (!scanRunning_) {
    WiFi.scanNetworks(true);
    scanRunning_ = true;
    server_.send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  const int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    server_.send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  scanRunning_ = false;
  if (result < 0) {
    server_.send(500, "application/json", "{\"error\":\"scan failed\"}");
    return;
  }
  StaticJsonDocument<2048> doc;
  JsonArray networks = doc.createNestedArray("networks");
  // Cap the list to bound the JSON size on the stack.
  const int16_t count = result > 15 ? 15 : result;
  for (int16_t i = 0; i < count; ++i) {
    JsonObject item = networks.createNestedObject();
    item["ssid"] = WiFi.SSID(i);
    item["rssi"] = WiFi.RSSI(i);
    item["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebService::rebootCommand() {
  appendEvent("reboot", "reboot requested from web");
  server_.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  delay(200);
  ESP.restart();
}

void WebService::appendEvent(const char* type, const char* message) {
  EventRecord event{};
  event.timestamp = millis();
  std::strncpy(event.type, type, sizeof(event.type) - 1);
  std::strncpy(event.message, message, sizeof(event.message) - 1);
  storage_.appendEvent(event);
}
