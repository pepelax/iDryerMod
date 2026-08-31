#include "services/NetworkService.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

NetworkService::NetworkService(const AppConfig& config) : config_(config) {}

bool NetworkService::begin(DeviceState& state) {
  WiFi.setHostname(config_.hostname);
  bool networkStarted = true;
  if (config_.wifiSsid[0] == '\0') {
    WiFi.mode(WIFI_AP_STA);
    networkStarted = WiFi.softAP("FilamentDryer-Setup");
    Serial.printf("[network] setup AP: %s, IP: %s\n",
                  networkStarted ? "FilamentDryer-Setup" : "FAILED",
                  WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
    Serial.printf("[network] connecting to SSID: %s\n", config_.wifiSsid);
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
  Serial.printf("[network] OTA hostname: %s\n", config_.hostname);
  return networkStarted;
}

void NetworkService::update(DeviceState& state, uint32_t now) {
  ArduinoOTA.handle();
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (!connectionStateKnown_ || connected != wasConnected_) {
    if (connected) {
      Serial.printf("[network] connected, IP: %s\n",
                    WiFi.localIP().toString().c_str());
    } else if (config_.wifiSsid[0] != '\0') {
      Serial.println("[network] disconnected");
    }
    connectionStateKnown_ = true;
    wasConnected_ = connected;
  }
  if (connected && !mdnsStarted_) {
    mdnsStarted_ = MDNS.begin(config_.hostname);
    if (mdnsStarted_) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[network] mDNS: http://%s.local\n", config_.hostname);
    }
  }
  if (!connected && config_.wifiSsid[0] != '\0' &&
      static_cast<uint32_t>(now - lastReconnectAt_) >= 10000UL) {
    WiFi.disconnect();
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
    lastReconnectAt_ = now;
  }
  state.wifiConnected = connected;
}
