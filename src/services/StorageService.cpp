#include "services/StorageService.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include <cstring>

#include "config/AppConfig.h"

namespace {
// Binary layout used by firmware 0.1.0 (Preferences blob "config"). Kept only
// to migrate settings saved before the key-value format was introduced.
struct LegacyConfig {
  char wifiSsid[33];
  char wifiPassword[65];
  char hostname[32];
  char otaPassword[33];
  float ntcR25Ohms;
  float ntcBeta;
  float ntcDividerOhms;
  float heaterMaxTemperatureC;
  float airMaxTemperatureC;
  float fanMinimumDuty;
  float fanStartupDuty;
  uint16_t servoMinUs;
  uint16_t servoMaxUs;
  uint8_t safeVentAngle;
  PidConfig temperaturePid;
  PidConfig humidityPid;
  float weightScaleOne;
  float weightScaleTwo;
  int32_t weightTareOne;
  int32_t weightTareTwo;
};
}  // namespace

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

void StorageService::migrateLegacyBlob(AppConfig& config) {
  const size_t length = preferences_.getBytesLength("config");
  if (length == sizeof(LegacyConfig)) {
    LegacyConfig legacy{};
    if (preferences_.getBytes("config", &legacy, sizeof(legacy)) ==
        sizeof(legacy)) {
      strlcpy(config.wifiSsid, legacy.wifiSsid, sizeof(config.wifiSsid));
      strlcpy(config.wifiPassword, legacy.wifiPassword,
              sizeof(config.wifiPassword));
      strlcpy(config.hostname, legacy.hostname, sizeof(config.hostname));
      if (std::strcmp(config.hostname, "filament-dryer") == 0) {
        strlcpy(config.hostname, "dryer", sizeof(config.hostname));
      }
      // A custom OTA password becomes the web password; the shipped default
      // "change-me" is dropped so the panel starts open.
      if (legacy.otaPassword[0] != '\0' &&
          std::strcmp(legacy.otaPassword, "change-me") != 0) {
        strlcpy(config.webLogin, "admin", sizeof(config.webLogin));
        strlcpy(config.webPassword, legacy.otaPassword,
                sizeof(config.webPassword));
      }
      config.ntcR25Ohms = legacy.ntcR25Ohms;
      config.ntcBeta = legacy.ntcBeta;
      config.ntcDividerOhms = legacy.ntcDividerOhms;
      config.heaterMaxTemperatureC = legacy.heaterMaxTemperatureC;
      config.airMaxTemperatureC = legacy.airMaxTemperatureC;
      config.fanMinimumDuty = legacy.fanMinimumDuty;
      config.fanStartupDuty = legacy.fanStartupDuty;
      config.servoMinUs = legacy.servoMinUs;
      config.servoMaxUs = legacy.servoMaxUs;
      config.safeVentAngle = legacy.safeVentAngle;
      config.temperaturePid = legacy.temperaturePid;
      config.humidityPid = legacy.humidityPid;
      config.weightScaleOne = legacy.weightScaleOne;
      config.weightScaleTwo = legacy.weightScaleTwo;
      config.weightTareOne = legacy.weightTareOne;
      config.weightTareTwo = legacy.weightTareTwo;
      Serial.println("[storage] legacy config migrated to key-value format");
    }
  }
  preferences_.remove("config");
  saveConfig(config);
}

bool StorageService::loadConfig(AppConfig& config) {
  setDefaultConfig(config);
  if (preferences_.isKey("config")) migrateLegacyBlob(config);
  // Each field is stored under its own NVS key, so future struct changes no
  // longer invalidate the whole configuration.
  strlcpy(config.wifiSsid,
          preferences_.getString("wifiSsid", config.wifiSsid).c_str(),
          sizeof(config.wifiSsid));
  strlcpy(config.wifiPassword,
          preferences_.getString("wifiPassword", config.wifiPassword).c_str(),
          sizeof(config.wifiPassword));
  strlcpy(config.hostname,
          preferences_.getString("hostname", config.hostname).c_str(),
          sizeof(config.hostname));
  strlcpy(config.webLogin,
          preferences_.getString("webLogin", config.webLogin).c_str(),
          sizeof(config.webLogin));
  strlcpy(config.webPassword,
          preferences_.getString("webPassword", config.webPassword).c_str(),
          sizeof(config.webPassword));
  config.ntcR25Ohms = preferences_.getFloat("ntcR25", config.ntcR25Ohms);
  config.ntcBeta = preferences_.getFloat("ntcBeta", config.ntcBeta);
  config.ntcDividerOhms =
      preferences_.getFloat("ntcDivider", config.ntcDividerOhms);
  config.heaterMaxTemperatureC =
      preferences_.getFloat("heaterMax", config.heaterMaxTemperatureC);
  config.airMaxTemperatureC =
      preferences_.getFloat("airMax", config.airMaxTemperatureC);
  config.fanMinimumDuty =
      preferences_.getFloat("fanMinDuty", config.fanMinimumDuty);
  config.fanStartupDuty =
      preferences_.getFloat("fanStartup", config.fanStartupDuty);
  config.servoMinUs = preferences_.getUShort("servoMinUs", config.servoMinUs);
  config.servoMaxUs = preferences_.getUShort("servoMaxUs", config.servoMaxUs);
  config.safeVentAngle =
      preferences_.getUChar("safeVent", config.safeVentAngle);
  config.temperaturePid.kp = preferences_.getFloat("tpKp", config.temperaturePid.kp);
  config.temperaturePid.ki = preferences_.getFloat("tpKi", config.temperaturePid.ki);
  config.temperaturePid.kd = preferences_.getFloat("tpKd", config.temperaturePid.kd);
  config.temperaturePid.outputMin =
      preferences_.getFloat("tpMin", config.temperaturePid.outputMin);
  config.temperaturePid.outputMax =
      preferences_.getFloat("tpMax", config.temperaturePid.outputMax);
  config.humidityPid.kp = preferences_.getFloat("hpKp", config.humidityPid.kp);
  config.humidityPid.ki = preferences_.getFloat("hpKi", config.humidityPid.ki);
  config.humidityPid.kd = preferences_.getFloat("hpKd", config.humidityPid.kd);
  config.humidityPid.outputMin =
      preferences_.getFloat("hpMin", config.humidityPid.outputMin);
  config.humidityPid.outputMax =
      preferences_.getFloat("hpMax", config.humidityPid.outputMax);
  config.weightScaleOne =
      preferences_.getFloat("scaleOne", config.weightScaleOne);
  config.weightScaleTwo =
      preferences_.getFloat("scaleTwo", config.weightScaleTwo);
  config.weightTareOne =
      preferences_.getInt("tareOne", config.weightTareOne);
  config.weightTareTwo =
      preferences_.getInt("tareTwo", config.weightTareTwo);
  return true;
}

bool StorageService::saveConfig(const AppConfig& config) {
  preferences_.putString("wifiSsid", config.wifiSsid);
  preferences_.putString("wifiPassword", config.wifiPassword);
  preferences_.putString("hostname", config.hostname);
  preferences_.putString("webLogin", config.webLogin);
  preferences_.putString("webPassword", config.webPassword);
  preferences_.putFloat("ntcR25", config.ntcR25Ohms);
  preferences_.putFloat("ntcBeta", config.ntcBeta);
  preferences_.putFloat("ntcDivider", config.ntcDividerOhms);
  preferences_.putFloat("heaterMax", config.heaterMaxTemperatureC);
  preferences_.putFloat("airMax", config.airMaxTemperatureC);
  preferences_.putFloat("fanMinDuty", config.fanMinimumDuty);
  preferences_.putFloat("fanStartup", config.fanStartupDuty);
  preferences_.putUShort("servoMinUs", config.servoMinUs);
  preferences_.putUShort("servoMaxUs", config.servoMaxUs);
  preferences_.putUChar("safeVent", config.safeVentAngle);
  preferences_.putFloat("tpKp", config.temperaturePid.kp);
  preferences_.putFloat("tpKi", config.temperaturePid.ki);
  preferences_.putFloat("tpKd", config.temperaturePid.kd);
  preferences_.putFloat("tpMin", config.temperaturePid.outputMin);
  preferences_.putFloat("tpMax", config.temperaturePid.outputMax);
  preferences_.putFloat("hpKp", config.humidityPid.kp);
  preferences_.putFloat("hpKi", config.humidityPid.ki);
  preferences_.putFloat("hpKd", config.humidityPid.kd);
  preferences_.putFloat("hpMin", config.humidityPid.outputMin);
  preferences_.putFloat("hpMax", config.humidityPid.outputMax);
  preferences_.putFloat("scaleOne", config.weightScaleOne);
  preferences_.putFloat("scaleTwo", config.weightScaleTwo);
  preferences_.putInt("tareOne", config.weightTareOne);
  preferences_.putInt("tareTwo", config.weightTareTwo);
  return true;
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

bool StorageService::loadWeightCalibration(uint8_t spool, WeightCalTable& table) {
  const char* key = spool == 0 ? "calW1" : "calW2";
  if (!preferences_.isKey(key)) return false;
  if (preferences_.getBytesLength(key) != sizeof(WeightCalTable)) return false;
  return preferences_.getBytes(key, &table, sizeof(WeightCalTable)) ==
         sizeof(WeightCalTable);
}

bool StorageService::saveWeightCalibration(uint8_t spool,
                                           const WeightCalTable& table) {
  const char* key = spool == 0 ? "calW1" : "calW2";
  return preferences_.putBytes(key, &table, sizeof(WeightCalTable)) ==
         sizeof(WeightCalTable);
}
