#include "services/WebService.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>

#include <algorithm>
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
}  // namespace

WebService::WebService(DeviceState& state, AppConfig& config,
                       DryingStateMachine& stateMachine, IStorage& storage)
    : state_(state), config_(config), stateMachine_(stateMachine), storage_(storage) {}

bool WebService::authorize() {
  if (server_.authenticate("admin", config_.otaPassword)) return true;
  server_.requestAuthentication();
  return false;
}

bool WebService::begin() {
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
  server_.serveStatic("/", LittleFS, "/");
  server_.on("/api/state", HTTP_GET, [this]() { if (authorize()) sendState(); });
  server_.on("/api/config", HTTP_GET, [this]() { if (authorize()) sendConfig(); });
  server_.on("/api/config", HTTP_PUT, [this]() { if (authorize()) saveConfig(); });
  server_.on("/api/run", HTTP_POST, [this]() { if (authorize()) runCommand(); });
  server_.on("/api/stop", HTTP_POST, [this]() { if (authorize()) stopCommand(); });
  server_.on("/api/pause", HTTP_POST, [this]() { if (authorize()) pauseCommand(); });
  server_.on("/api/calibration", HTTP_POST,
             [this]() { if (authorize()) calibrationCommand(); });
  server_.on("/api/history", HTTP_GET, [this]() { if (authorize()) sendHistory(); });
  server_.on("/api/events", HTTP_GET, [this]() { if (authorize()) sendEvents(); });
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
  server_.begin();
  return true;
}

void WebService::update() { server_.handleClient(); }

void WebService::sendState() {
  StaticJsonDocument<1536> doc;
  doc["mode"] = modeName(state_.mode);
  doc["phase"] = static_cast<uint8_t>(state_.phase);
  doc["fault"] = static_cast<uint8_t>(state_.fault);
  doc["remainingSeconds"] = state_.remainingSeconds;
  doc["wifiConnected"] = state_.wifiConnected;
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
  doc["outputs"]["fan"] = state_.actuators.fanPower;
  doc["outputs"]["ventAngle"] = state_.actuators.ventAngle;
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebService::sendConfig() {
  StaticJsonDocument<2048> doc;
  doc["hostname"] = config_.hostname;
  doc["wifiSsid"] = config_.wifiSsid;
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
    item["relativeHumidity"] = defaultsList[i].relativeHumidity;
    item["durationSeconds"] = defaultsList[i].durationSeconds;
  }
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebService::runCommand() {
  StaticJsonDocument<512> doc;
  if (server_.hasArg("plain")) deserializeJson(doc, server_.arg("plain"));
  String modeValue = server_.hasArg("mode") ? server_.arg("mode") : "timed_manual";
  float temperature = server_.hasArg("temperatureC") ? server_.arg("temperatureC").toFloat() : 45.0f;
  float humidity = server_.hasArg("relativeHumidity") ? server_.arg("relativeHumidity").toFloat() : 20.0f;
  uint32_t duration = server_.hasArg("durationSeconds")
                           ? static_cast<uint32_t>(server_.arg("durationSeconds").toInt())
                           : 3600UL;
  if (doc.containsKey("mode")) modeValue = doc["mode"].as<const char*>();
  if (doc.containsKey("temperatureC")) temperature = doc["temperatureC"].as<float>();
  if (doc.containsKey("relativeHumidity")) humidity = doc["relativeHumidity"].as<float>();
  if (doc.containsKey("durationSeconds")) duration = doc["durationSeconds"].as<uint32_t>();
  Setpoints setpoints;
  setpoints.airTemperatureC = temperature;
  setpoints.relativeHumidity = humidity;
  setpoints.durationSeconds = duration;
  setpoints.heaterLimitC = config_.heaterMaxTemperatureC;
  setpoints.airTemperatureC =
      std::max(20.0f, std::min(config_.airMaxTemperatureC, setpoints.airTemperatureC));
  const DryingMode mode = parseMode(modeValue);
  const bool accepted = stateMachine_.start(state_, mode, setpoints, millis());
  if (accepted) appendEvent("run", modeName(mode));
  server_.send(accepted ? 200 : 400, "application/json",
               accepted ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WebService::stopCommand() {
  stateMachine_.stop(state_, millis());
  appendEvent("stop", "stopped by web");
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WebService::pauseCommand() {
  stateMachine_.pause(state_, millis());
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WebService::calibrationCommand() {
  server_.send(501, "application/json",
               "{\"error\":\"calibration must be completed from local service mode\"}");
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
  if (doc.containsKey("wifiSsid")) {
    strlcpy(config_.wifiSsid, doc["wifiSsid"] | "", sizeof(config_.wifiSsid));
  }
  if (doc.containsKey("wifiPassword")) {
    strlcpy(config_.wifiPassword, doc["wifiPassword"] | "", sizeof(config_.wifiPassword));
  }
  if (doc.containsKey("hostname")) {
    strlcpy(config_.hostname, doc["hostname"] | "filament-dryer",
            sizeof(config_.hostname));
  }
  storage_.saveConfig(config_);
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

void WebService::appendEvent(const char* type, const char* message) {
  EventRecord event{};
  event.timestamp = millis();
  std::strncpy(event.type, type, sizeof(event.type) - 1);
  std::strncpy(event.message, message, sizeof(event.message) - 1);
  storage_.appendEvent(event);
}
