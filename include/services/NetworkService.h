#pragma once

#include "config/AppConfig.h"
#include "domain/Types.h"

class NetworkService {
 public:
  explicit NetworkService(const AppConfig& config);
  bool begin(DeviceState& state);
  void update(DeviceState& state, uint32_t now);
  // Raises the WPA2-protected setup hotspot for defaults::kSetupApWindowMs
  // (or restarts the window when already up). The per-boot random password
  // and the SSID are published into DeviceState for the display.
  void requestSetupAp(DeviceState& state, uint32_t now);
  // Drops the setup hotspot before its window expires.
  void stopSetupAp(DeviceState& state);

 private:
  // Publishes the configured UTC offset to libc as a POSIX TZ string so
  // localtime works once the SNTP layer sets the system clock.
  void applyTimezone();
  // Starts (or restarts, after a config change) the SNTP time service and
  // stops it when NTP is disabled.
  void syncNtpState(bool connected);
  void startSetupAp(DeviceState& state, uint32_t now);

  const AppConfig& config_;
  uint32_t lastReconnectAt_ = 0;
  uint32_t lastAddressRefreshAt_ = 0;
  uint32_t apWindowEndsAt_ = 0;
  int16_t appliedTzOff_ = -32768;
  char appliedNtpServer_[48] = {0};
  char apPassword_[13] = {0};
  bool timeServiceStarted_ = false;
  bool mdnsStarted_ = false;
  bool connectionStateKnown_ = false;
  bool wasConnected_ = false;
  bool apActive_ = false;
};
