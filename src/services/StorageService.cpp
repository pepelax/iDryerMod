#include "services/StorageService.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "config/AppConfig.h"

bool StorageService::begin() {
  const bool preferencesOk = preferences_.begin("dryer", false);
  const bool littleFsOk = LittleFS.begin(true);
  if (littleFsOk) {
    File telemetry = LittleFS.open("/telemetry.jsonl", FILE_READ);
    Serial.printf("[storage] LittleFS total=%u used=%u telemetry=%s size=%u\n",
                  static_cast<unsigned>(LittleFS.totalBytes()),
                  static_cast<unsigned>(LittleFS.usedBytes()),
                  telemetry ? "present" : "absent",
                  telemetry ? static_cast<unsigned>(telemetry.size()) : 0U);
    if (telemetry) telemetry.close();
  }
  return preferencesOk && littleFsOk;
}

bool StorageService::loadConfig(AppConfig& config) {
  setDefaultConfig(config);
  if (!preferences_.isKey("config")) return false;
  const size_t expected = sizeof(AppConfig);
  if (preferences_.getBytesLength("config") != expected) return false;
  return preferences_.getBytes("config", &config, expected) == expected;
}

bool StorageService::saveConfig(const AppConfig& config) {
  return preferences_.putBytes("config", &config, sizeof(config)) == sizeof(config);
}

bool StorageService::rotateIfNeeded(const char* path) {
  if (!LittleFS.exists(path)) return true;
  File file = LittleFS.open(path, FILE_READ);
  const size_t size = file ? file.size() : 0;
  file.close();
  if (size < 300000) return true;
  const String backup = String(path) + ".old";
  LittleFS.remove(backup);
  LittleFS.rename(path, backup);
  return true;
}

bool StorageService::appendTelemetry(const DeviceState& state) {
  rotateIfNeeded("/telemetry.jsonl");
  File file = LittleFS.open("/telemetry.jsonl", FILE_APPEND);
  if (!file) return false;
  StaticJsonDocument<512> doc;
  doc["ts"] = state.air.timestamp;
  doc["t"] = state.air.temperatureC;
  doc["rh"] = state.air.relativeHumidity;
  doc["ntc"] = state.heater.temperatureC;
  doc["w1"] = state.spoolOne.grams;
  doc["w2"] = state.spoolTwo.grams;
  doc["heat"] = state.actuators.heaterPower;
  doc["fan"] = state.actuators.fanPower;
  serializeJson(doc, file);
  file.println();
  file.close();
  return true;
}

bool StorageService::appendEvent(const EventRecord& event) {
  rotateIfNeeded("/events.jsonl");
  File file = LittleFS.open("/events.jsonl", FILE_APPEND);
  if (!file) return false;
  StaticJsonDocument<384> doc;
  doc["ts"] = event.timestamp;
  doc["type"] = event.type;
  doc["message"] = event.message;
  serializeJson(doc, file);
  file.println();
  file.close();
  return true;
}
