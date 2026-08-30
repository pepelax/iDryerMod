#include "services/NetworkService.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

NetworkService::NetworkService(const AppConfig& config) : config_(config) {}

bool NetworkService::begin(DeviceState& state) {
  WiFi.setHostname(config_.hostname);
  if (config_.wifiSsid[0] == '\0') {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("FilamentDryer-Setup");
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
  }
  ArduinoOTA.setHostname(config_.hostname);
  ArduinoOTA.setPassword(config_.otaPassword);
  ArduinoOTA.onStart([&state]() { state.otaInProgress = true; });
  ArduinoOTA.onEnd([&state]() { state.otaInProgress = false; });
  ArduinoOTA.onError([&state](ota_error_t error) {
    (void)error;
    state.otaInProgress = false;
  });
  ArduinoOTA.begin();
  return true;
}

void NetworkService::update(DeviceState& state, uint32_t now) {
  ArduinoOTA.handle();
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !mdnsStarted_) {
    mdnsStarted_ = MDNS.begin(config_.hostname);
    if (mdnsStarted_) MDNS.addService("http", "tcp", 80);
  }
  if (!connected && config_.wifiSsid[0] != '\0' &&
      static_cast<uint32_t>(now - lastReconnectAt_) >= 10000UL) {
    WiFi.disconnect();
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
    lastReconnectAt_ = now;
  }
  state.wifiConnected = connected;
}
