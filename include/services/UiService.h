#pragma once

#include "core/DryingStateMachine.h"
#include "domain/Interfaces.h"

class UiService {
 public:
  UiService(IDisplay& display, IInput& input, DryingStateMachine& stateMachine);
  bool begin();
  void update(DeviceState& state, uint32_t now);

 private:
  IDisplay& display_;
  IInput& input_;
  DryingStateMachine& stateMachine_;
  uint8_t selectedMode_ = 0;
  DisplayView view_ = DisplayView::Dashboard;
  uint32_t lastRenderAt_ = 0;
};
