#include "services/NetworkService.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

NetworkService::NetworkService(const AppConfig& config) : config_(config) {}

namespace {
void publishAddress(DeviceState& state, bool apActive, const char* hostname,
                    bool mdnsStarted) {
  if (WiFi.status() == WL_CONNECTED) {
    strlcpy(state.ipAddress, WiFi.localIP().toString().c_str(),
            sizeof(state.ipAddress));
    // Prefer the mDNS name on the display, but keep it short enough to fit
    // the field; fall back to the raw IP otherwise.
    if (mdnsStarted && hostname[0] != '\0' &&
        std::strlen(hostname) + 7 <= sizeof(state.webAddress)) {
      std::snprintf(state.webAddress, sizeof(state.webAddress), "%s.local",
                    hostname);
    } else {
      strlcpy(state.webAddress, state.ipAddress, sizeof(state.webAddress));
    }
  } else if (apActive) {
    strlcpy(state.ipAddress, WiFi.softAPIP().toString().c_str(),
            sizeof(state.ipAddress));
    strlcpy(state.webAddress, state.ipAddress, sizeof(state.webAddress));
  } else {
    state.ipAddress[0] = '\0';
    state.webAddress[0] = '\0';
  }
}
}  // namespace

bool NetworkService::begin(DeviceState& state) {
  WiFi.setHostname(config_.hostname);
  bool networkStarted = true;
  if (config_.wifiSsid[0] == '\0') {
    WiFi.mode(WIFI_AP_STA);
    networkStarted = WiFi.softAP("FilamentDryer-Setup");
    apActive_ = networkStarted;
    Serial.printf("[network] setup AP: %s, IP: %s\n",
                  networkStarted ? "FilamentDryer-Setup" : "FAILED",
                  WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
    staStartedAt_ = millis();
    Serial.printf("[network] connecting to SSID: %s\n", config_.wifiSsid);
  }
  publishAddress(state, apActive_, config_.hostname, mdnsStarted_);
  state.apActive = apActive_;
  ArduinoOTA.setHostname(config_.hostname);
  // OTA shares the web password; when no password is configured the panel
  // and OTA stay open for the local network.
  if (config_.webPassword[0] != '\0') {
    ArduinoOTA.setPassword(config_.webPassword);
  }
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
  if (connected) {
    lastConnectedAt_ = now;
    // Once the station is back online the fallback setup AP is no longer
    // needed; keep the air clean and drop it.
    if (apActive_) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      apActive_ = false;
      publishAddress(state, apActive_, config_.hostname, mdnsStarted_);
      Serial.println("[network] station online, setup AP stopped");
    }
  }
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
  // If the station cannot join for a while (bad password, missing network),
  // raise the setup AP so the dryer stays reachable for reconfiguration.
  if (!connected && !apActive_ && config_.wifiSsid[0] != '\0') {
    const uint32_t since = lastConnectedAt_ != 0 ? lastConnectedAt_ : staStartedAt_;
    if (since != 0 && static_cast<uint32_t>(now - since) >= 60000UL) {
      WiFi.mode(WIFI_AP_STA);
      apActive_ = WiFi.softAP("FilamentDryer-Setup");
      publishAddress(state, apActive_, config_.hostname, mdnsStarted_);
      Serial.printf("[network] station failed, setup AP started: %s\n",
                    apActive_ ? "OK" : "FAILED");
    }
  }
  if (connected && !mdnsStarted_) {
    mdnsStarted_ = MDNS.begin(config_.hostname);
    if (mdnsStarted_) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[network] mDNS: http://%s.local\n", config_.hostname);
    }
  }
  // Refresh the shown address on link changes and periodically afterwards
  // (DHCP may hand out a different lease long after boot).
  if (!connectionStateKnown_ || connected != wasConnected_ ||
      static_cast<uint32_t>(now - lastAddressRefreshAt_) >= 10000UL) {
    publishAddress(state, apActive_, config_.hostname, mdnsStarted_);
    state.apActive = apActive_;
    lastAddressRefreshAt_ = now;
  }
  if (!connected && config_.wifiSsid[0] != '\0' &&
      static_cast<uint32_t>(now - lastReconnectAt_) >= 10000UL) {
    WiFi.disconnect();
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
    lastReconnectAt_ = now;
  }
  state.wifiConnected = connected;
}
