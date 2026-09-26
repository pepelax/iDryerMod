#include "services/NetworkService.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_task_wdt.h>

#include "config/Defaults.h"

#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_sntp.h>
#else
extern "C" {
#include <lwip/apps/sntp.h>
}
#endif

#include <cstdio>
#include <cstring>
#include <ctime>

#include <esp_system.h>

namespace {
// ArduinoOTA receives the whole image inside handle(), blocking the main
// loop for the duration of the transfer. The loop task must leave the
// watchdog for that window, otherwise the 5 s timeout resets the board
// mid-upload.
void leaveWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_delete(nullptr);
#else
  esp_task_wdt_delete(NULL);
#endif
}

void rejoinWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_add(nullptr);
#else
  esp_task_wdt_add(NULL);
#endif
}
}  // namespace

NetworkService::NetworkService(const AppConfig& config) : config_(config) {}

void NetworkService::applyTimezone() {
  // POSIX TZ offsets are inverted: "UTC-3" means local = UTC+3.
  const int16_t off = config_.tzOffsetMinutes;
  const uint16_t abs = static_cast<uint16_t>(off < 0 ? -off : off);
  char tz[12];
  if (abs % 60 == 0) {
    std::snprintf(tz, sizeof(tz), "UTC%c%u", off >= 0 ? '-' : '+', abs / 60);
  } else {
    std::snprintf(tz, sizeof(tz), "UTC%c%u:%02u", off >= 0 ? '-' : '+',
                  abs / 60, abs % 60);
  }
  setenv("TZ", tz, 1);
  tzset();
  appliedTzOff_ = off;
  Serial.printf("[network] timezone: UTC%s%d\n", off >= 0 ? "+" : "",
                static_cast<int>(off) / 60);
}

void NetworkService::syncNtpState(bool connected) {
  const bool wanted =
      connected && config_.ntpEnabled && config_.ntpServer[0] != '\0';
  if (timeServiceStarted_ && !wanted) {
    // Disabled or offline: stop polling so the clock no longer follows NTP.
#if ESP_IDF_VERSION_MAJOR >= 5
    if (esp_sntp_enabled()) esp_sntp_stop();
#else
    if (sntp_enabled()) sntp_stop();
#endif
    timeServiceStarted_ = false;
    Serial.println("[network] NTP sync stopped");
    return;
  }
  // SNTP retries on its own; (re)start only once per boot or after the
  // configured server changes.
  if (!wanted || (timeServiceStarted_ &&
                  std::strcmp(appliedNtpServer_, config_.ntpServer) == 0)) {
    return;
  }
  strlcpy(appliedNtpServer_, config_.ntpServer, sizeof(appliedNtpServer_));
  configTime(0, 0, appliedNtpServer_, "time.google.com", "time.nist.gov");
  timeServiceStarted_ = true;
  Serial.printf("[network] NTP sync started via %s\n", appliedNtpServer_);
}

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

// Unambiguous alphabet for the on-screen password: no 0/O/1/I/L.
constexpr char kApPasswordAlphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";
constexpr uint8_t kApPasswordLength = 8;
}  // namespace

bool NetworkService::begin(DeviceState& state) {
  WiFi.setHostname(config_.hostname);
  applyTimezone();
  // The setup hotspot is never raised automatically: a device without saved
  // credentials (or with an unreachable router) simply stays offline and
  // works standalone. The hotspot is opened manually from the menu or by
  // holding the encoder button through power-on.
  if (config_.wifiSsid[0] != '\0') {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
    Serial.printf("[network] connecting to SSID: %s\n", config_.wifiSsid);
  } else {
    WiFi.mode(WIFI_OFF);
    Serial.println("[network] no saved SSID, radio off");
  }
  publishAddress(state, apActive_, config_.hostname, mdnsStarted_);
  state.apActive = apActive_;
  ArduinoOTA.setHostname(config_.hostname);
  // OTA shares the web password; when no password is configured the panel
  // and OTA stay open for the local network.
  if (config_.webPassword[0] != '\0') {
    ArduinoOTA.setPassword(config_.webPassword);
  }
  ArduinoOTA.onStart([&state]() {
    state.otaInProgress = true;
    leaveWatchdog();
  });
  ArduinoOTA.onEnd([&state]() {
    state.otaInProgress = false;
    rejoinWatchdog();
  });
  ArduinoOTA.onError([&state](ota_error_t error) {
    (void)error;
    state.otaInProgress = false;
    rejoinWatchdog();
  });
  ArduinoOTA.begin();
  Serial.printf("[network] OTA hostname: %s\n", config_.hostname);
  return true;
}

void NetworkService::update(DeviceState& state, uint32_t now) {
  ArduinoOTA.handle();
  const bool connected = WiFi.status() == WL_CONNECTED;
  // Offset changes saved from the web panel or the device menu take effect
  // without a reboot.
  if (appliedTzOff_ != config_.tzOffsetMinutes) {
    applyTimezone();
  }
  syncNtpState(connected);
  // A freshly booted ESP32 has no wall clock: epoch 0 (1970) means the SNTP
  // exchange has not succeeded yet.
  const time_t wallClock = time(nullptr);
  const bool timeSynced = wallClock > 1600000000L;
  if (timeSynced && !state.timeSynced) {
    Serial.printf("[network] NTP time synchronized: %s", ctime(&wallClock));
  }
  state.timeSynced = timeSynced;
  if (timeSynced) {
    state.epochSeconds = static_cast<uint32_t>(wallClock);
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
  // The setup hotspot lives only for its manually requested window; when it
  // expires the device goes back to station-only (or radio-off) operation.
  if (apActive_) {
    if (static_cast<int32_t>(now - apWindowEndsAt_) >= 0) {
      Serial.println("[network] setup AP window expired");
      stopSetupAp(state);
    } else {
      state.apRemainingSeconds = static_cast<uint16_t>(
          (apWindowEndsAt_ - now) / 1000UL);
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

void NetworkService::startSetupAp(DeviceState& state, uint32_t now) {
  // Per-boot random password: regenerated only on reboot so a re-request
  // while the hotspot is already up keeps the credentials the user sees.
  if (apPassword_[0] == '\0') {
    for (uint8_t i = 0; i < kApPasswordLength; ++i) {
      apPassword_[i] = kApPasswordAlphabet[esp_random() %
                                           (sizeof(kApPasswordAlphabet) - 1)];
    }
    apPassword_[kApPasswordLength] = '\0';
  }
  // Short unique SSID: first characters of the hostname plus four hex digits
  // of the unit MAC, so several dryers can be told apart in the Wi-Fi list.
  char host[6];
  strlcpy(host, config_.hostname, sizeof(host));
  if (host[0] == '\0') strlcpy(host, "dryer", sizeof(host));
  char ssid[24];
  const uint16_t unitId =
      static_cast<uint16_t>((ESP.getEfuseMac() >> 32) & 0xFFFFULL);
  std::snprintf(ssid, sizeof(ssid), "%s-setup-%04X", host, unitId);
  // AP+STA keeps the station link alive while the hotspot is open.
  WiFi.mode(WIFI_AP_STA);
  apActive_ = WiFi.softAP(ssid, apPassword_);
  apWindowEndsAt_ = now + defaults::kSetupApWindowMs;
  if (apActive_) {
    strlcpy(state.apSsid, ssid, sizeof(state.apSsid));
    strlcpy(state.apPassword, apPassword_, sizeof(state.apPassword));
    strlcpy(state.apAddress, WiFi.softAPIP().toString().c_str(),
            sizeof(state.apAddress));
    state.apRemainingSeconds = defaults::kSetupApWindowMs / 1000UL;
    Serial.printf("[network] setup AP up: SSID %s, password %s, IP %s\n",
                  ssid, apPassword_, state.apAddress);
  } else {
    state.apSsid[0] = '\0';
    state.apPassword[0] = '\0';
    state.apAddress[0] = '\0';
    state.apRemainingSeconds = 0;
    Serial.println("[network] setup AP start FAILED");
  }
  publishAddress(state, apActive_, config_.hostname, mdnsStarted_);
  state.apActive = apActive_;
}

void NetworkService::requestSetupAp(DeviceState& state, uint32_t now) {
  if (apActive_) {
    // Already up: restart the window so the user gets a fresh countdown.
    apWindowEndsAt_ = now + defaults::kSetupApWindowMs;
    state.apRemainingSeconds = defaults::kSetupApWindowMs / 1000UL;
    Serial.println("[network] setup AP window restarted");
    return;
  }
  startSetupAp(state, now);
}

void NetworkService::stopSetupAp(DeviceState& state) {
  if (!apActive_) return;
  WiFi.softAPdisconnect(true);
  // Back to station-only when a network is configured, radio off otherwise.
  if (config_.wifiSsid[0] != '\0') {
    WiFi.mode(WIFI_STA);
  } else {
    WiFi.mode(WIFI_OFF);
  }
  apActive_ = false;
  state.apSsid[0] = '\0';
  state.apPassword[0] = '\0';
  state.apAddress[0] = '\0';
  state.apRemainingSeconds = 0;
  publishAddress(state, apActive_, config_.hostname, mdnsStarted_);
  state.apActive = apActive_;
  Serial.println("[network] setup AP stopped");
}
