#include <unity.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "control/AhSlopeEstimator.h"
#include "control/HeaterThermostat.h"
#include "control/HumidityMath.h"
#include "control/PidController.h"
#include "control/PurgeScheduler.h"
#include "config/AppConfig.h"
#include "config/Defaults.h"
#include "core/DryingStateMachine.h"
#include "domain/Interfaces.h"
#include "services/ActuatorService.h"
#include "services/ControlService.h"
#include "services/SafetyService.h"

namespace {
unsigned g_callTick = 0;
}  // namespace

class MockHeaterOutput final : public IHeaterOutput {
 public:
  void begin() override {}
  void setPower(float percent, uint32_t) override {
    power = percent;
    tick = ++g_callTick;
  }
  void off() override { power = 0.0f; }
  float power = 0.0f;
  unsigned tick = 0;
};

class MockFanOutput final : public IFanOutput {
 public:
  void begin() override {}
  void setPower(uint8_t percent) override {
    power = percent;
    tick = ++g_callTick;
  }
  void off() override { power = 0; }
  uint8_t power = 0;
  unsigned tick = 0;
};

class MockVentOutput final : public IVentOutput {
 public:
  void begin() override {}
  void setAngle(uint16_t angle) override { angle_ = angle; }
  uint16_t angle_ = 0;
};

void setUp() {}
void tearDown() {}

void test_absolute_humidity_round_trip() {
  const float absolute = relativeToAbsoluteHumidity(25.0f, 50.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 11.5f, absolute);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 50.0f, absoluteToRelativeHumidity(25.0f, absolute));
}

void test_pid_output_is_bounded_and_rate_limited() {
  const PidConfig config{2.0f, 0.1f, 0.0f, 0.0f, 100.0f};
  PidController pid;
  pid.begin(config, 1000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, pid.compute(0.0f, 100.0f, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, pid.compute(0.0f, 100.0f, 500));
  TEST_ASSERT(pid.compute(90.0f, 100.0f, 1000) < 100.0f);
}

void test_state_machine_timed_run() {
  DeviceState state;
  state.air.valid = true;
  state.air.temperatureC = 40.0f;
  DryingStateMachine machine;
  machine.begin(state, 0);
  Setpoints setpoints;
  setpoints.airTemperatureC = 45.0f;
  setpoints.relativeHumidity = 20.0f;
  setpoints.heaterLimitC = 105.0f;
  setpoints.durationSeconds = 10;
  TEST_ASSERT_TRUE(machine.start(state, DryingMode::TimedManual, setpoints, 0));
  machine.update(state, 0);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Warmup), static_cast<int>(state.phase));
  state.air.temperatureC = 44.0f;
  machine.update(state, 1000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Drying), static_cast<int>(state.phase));
  machine.update(state, 11000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Finish), static_cast<int>(state.phase));
}

void test_state_machine_rejects_non_run_modes() {
  DeviceState state;
  DryingStateMachine machine;
  machine.begin(state, 0);
  TEST_ASSERT_FALSE(machine.start(state, DryingMode::Idle, Setpoints{}, 0));
}

void test_weight_sensors_are_optional_for_safety() {
  DeviceState state;
  state.air.valid = true;
  state.air.temperatureC = 25.0f;
  state.air.relativeHumidity = 40.0f;
  state.heater.valid = true;
  state.heater.temperatureC = 40.0f;
  state.setpoints.heaterLimitC = 105.0f;
  // Both weight sensors are disconnected, but this must not be an error.
  SafetyService safety(75.0f);
  FaultCode fault = FaultCode::WatchdogReset;
  TEST_ASSERT_TRUE(safety.evaluate(state, 0, fault));
  TEST_ASSERT_EQUAL(static_cast<int>(FaultCode::None), static_cast<int>(fault));
}

void test_actuator_interlock_forces_fan_when_heating() {
  MockHeaterOutput heater;
  MockFanOutput fan;
  MockVentOutput vent;
  ActuatorService actuators(heater, fan, vent);
  actuators.begin(90, 25);
  ActuatorState outputs;
  outputs.heaterPower = 60.0f;
  outputs.fanPower = 0;
  actuators.apply(outputs, 0);
  TEST_ASSERT_EQUAL_UINT8(25, fan.power);
  TEST_ASSERT_EQUAL_UINT8(25, outputs.fanPower);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, heater.power);
  TEST_ASSERT_TRUE(heater.tick > fan.tick);
}

void test_actuator_interlock_idle_without_heat() {
  MockHeaterOutput heater;
  MockFanOutput fan;
  MockVentOutput vent;
  ActuatorService actuators(heater, fan, vent);
  actuators.begin(90, 25);
  ActuatorState outputs;
  outputs.heaterPower = 0.0f;
  outputs.fanPower = 10;
  actuators.apply(outputs, 0);
  TEST_ASSERT_EQUAL_UINT8(10, fan.power);
  outputs.fanPower = 80;
  actuators.apply(outputs, 1000);
  TEST_ASSERT_EQUAL_UINT8(80, fan.power);
}

void test_thermostat_holds_band() {
  HeaterThermostat thermostat;
  TEST_ASSERT_TRUE(thermostat.update(25.0f, 70.0f, 0));      // below band -> on
  TEST_ASSERT_TRUE(thermostat.update(69.0f, 70.0f, 3000));   // inside band -> hold on
  TEST_ASSERT_FALSE(thermostat.update(71.5f, 70.0f, 6000));  // above band -> off
  TEST_ASSERT_FALSE(thermostat.update(69.5f, 70.0f, 9000));  // inside band -> hold off
  TEST_ASSERT_TRUE(thermostat.update(50.0f, 70.0f, 12000));  // below band -> on
}

void test_thermostat_respects_min_switch_interval() {
  HeaterThermostat thermostat;
  TEST_ASSERT_TRUE(thermostat.update(25.0f, 70.0f, 0));     // on at t=0
  TEST_ASSERT_TRUE(thermostat.update(71.5f, 70.0f, 1000));  // <2s since switch -> hold on
  TEST_ASSERT_FALSE(thermostat.update(71.5f, 70.0f, 2500)); // now switching off is allowed
}

void test_heater_setpoint_ceiling() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, heaterSetpointCeilingC(105.0f, 105.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, heaterSetpointCeilingC(95.0f, 105.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, heaterSetpointCeilingC(105.0f, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, heaterSetpointCeilingC(0.0f, 105.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, heaterSetpointCeilingC(0.0f, 0.0f));
}

void test_cascade_bounds_heater_setpoint_and_power() {
  AppConfig config;
  setDefaultConfig(config);
  SafetyService safety(75.0f);
  DryingStateMachine machine;
  ControlService control(config, safety, machine);
  control.begin();
  DeviceState state;
  state.air.valid = true;
  state.air.temperatureC = 25.0f;
  state.air.relativeHumidity = 40.0f;
  state.heater.valid = true;
  state.heater.temperatureC = 25.0f;
  state.setpoints.airTemperatureC = 65.0f;
  state.setpoints.relativeHumidity = 20.0f;
  state.setpoints.heaterLimitC = 105.0f;
  state.setpoints.durationSeconds = 3600;
  machine.begin(state, 0);
  TEST_ASSERT_TRUE(machine.start(state, DryingMode::TimedManual, state.setpoints, 0));
  control.update(state, 0);
  // Large error: outer PID clamps to the ceiling (105 - 5) and the cold
  // heater switches fully on; the interlock keeps the fan at its floor.
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, state.heaterSetpointC);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, state.actuators.heaterPower);
  TEST_ASSERT_TRUE(state.actuators.fanPower >= 25);
  // Heater above the ceiling band: fully off.
  state.heater.temperatureC = 101.6f;
  control.update(state, 5000);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, state.actuators.heaterPower);
}

void test_purge_scheduler_cycle() {
  PurgeScheduler purge;
  PurgeParams params;
  params.minSealMs = 600000;
  params.settleMs = 180000;
  params.maxPurgeMs = 45000;
  params.riseTriggerGm3 = 0.5f;
  purge.begin(params, 1000);
  // First purge is allowed immediately: room air above the floor vents now.
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Sealed), static_cast<int>(purge.phase()));
  purge.update(5.0f, 4.0f, 2000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Purge), static_cast<int>(purge.phase()));
  // Purge ends early once AH drops to the floor.
  purge.update(3.9f, 4.0f, 10000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Settle), static_cast<int>(purge.phase()));
  // Still settling: nothing changes before settleMs elapses.
  purge.update(3.9f, 4.0f, 100000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Settle), static_cast<int>(purge.phase()));
  // Settle completes: sealed again with the current AH as baseline.
  purge.update(3.5f, 4.0f, 200000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Sealed), static_cast<int>(purge.phase()));
  // Small rise below the trigger: stays sealed even after minSeal.
  purge.update(3.7f, 4.0f, 900000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Sealed), static_cast<int>(purge.phase()));
  // Rise above the trigger after the minimum seal time: purge.
  purge.update(4.2f, 4.0f, 1000000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Purge), static_cast<int>(purge.phase()));
  // Purge runs to its cap when AH never drops to the floor.
  purge.update(4.1f, 4.0f, 1000000 + 45000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Settle), static_cast<int>(purge.phase()));
}

void test_purge_scheduler_respects_min_seal() {
  PurgeScheduler purge;
  PurgeParams params;
  params.minSealMs = 600000;
  params.settleMs = 180000;
  params.maxPurgeMs = 45000;
  params.riseTriggerGm3 = 0.5f;
  purge.begin(params, 0);
  // Saturated room air: the first purge may start immediately.
  purge.update(5.0f, 4.0f, 1000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Purge), static_cast<int>(purge.phase()));
  // Air exchanged below the floor: settle, then seal with baseline 1.0.
  purge.update(1.0f, 4.0f, 10000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Settle), static_cast<int>(purge.phase()));
  purge.update(1.0f, 4.0f, 190000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Sealed), static_cast<int>(purge.phase()));
  // Rise of 0.4 (< trigger): stays sealed.
  purge.update(1.4f, 4.0f, 300000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Sealed), static_cast<int>(purge.phase()));
  // Rise of 0.8 (>= trigger) but only 310 s sealed (< 600 s): still sealed.
  purge.update(1.8f, 4.0f, 500000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Sealed), static_cast<int>(purge.phase()));
  // Once the minimum seal time has elapsed the pending trigger fires.
  purge.update(1.8f, 4.0f, 800000);
  TEST_ASSERT_EQUAL(static_cast<int>(PurgePhase::Purge), static_cast<int>(purge.phase()));
}

void test_ah_slope_estimator_linear_rise() {
  AhSlopeEstimator estimator;
  estimator.reset();
  // 0.1 g/m3 every 30 s -> 12 g/m3/h.
  for (uint8_t i = 0; i < 12; ++i) {
    estimator.addSample(i * 30000UL, 1.0f + 0.1f * i);
  }
  TEST_ASSERT_TRUE(estimator.slopeValid());
  TEST_ASSERT_EQUAL_UINT8(12, estimator.sampleCount());
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 12.0f, estimator.slopeGm3PerHour());
}

void test_ah_slope_estimator_flat_and_reset() {
  AhSlopeEstimator estimator;
  for (uint8_t i = 0; i < 10; ++i) {
    estimator.addSample(i * 30000UL, 2.0f);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, estimator.slopeGm3PerHour());
  estimator.reset();
  TEST_ASSERT_EQUAL_UINT8(0, estimator.sampleCount());
  TEST_ASSERT_FALSE(estimator.slopeValid());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, estimator.slopeGm3PerHour());
}

void test_control_pulse_ventilation_and_dryness() {
  AppConfig config;
  setDefaultConfig(config);
  SafetyService safety(75.0f);
  DryingStateMachine machine;
  ControlService control(config, safety, machine);
  control.begin();
  DeviceState state;
  state.air.valid = true;
  state.air.temperatureC = 45.0f;
  state.air.relativeHumidity = 30.0f;
  // Room-level AH below the rise trigger baseline so the run starts sealed.
  state.air.absoluteHumidityGm3 = 0.3f;
  state.heater.valid = true;
  state.heater.temperatureC = 45.0f;
  state.setpoints.airTemperatureC = 45.0f;
  state.setpoints.relativeHumidity = 20.0f;
  state.setpoints.heaterLimitC = 95.0f;
  state.setpoints.durationSeconds = 3600;
  machine.begin(state, 0);
  TEST_ASSERT_TRUE(machine.start(state, DryingMode::TimedManual, state.setpoints, 0));
  // Warmup: air already at target, so the machine reaches Drying quickly.
  control.update(state, 0);
  control.update(state, 1000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Drying), static_cast<int>(state.phase));
  // Sealed with low AH: vent closed, fan at circulation duty.
  TEST_ASSERT_EQUAL_UINT8(0, state.purgePhase);
  TEST_ASSERT_EQUAL_UINT16(15, state.actuators.ventAngle);
  TEST_ASSERT_EQUAL_UINT8(25, state.actuators.fanPower);
  // AH jumps far above the floor: first purge starts at once.
  state.air.absoluteHumidityGm3 = 6.0f;
  control.update(state, 2000);
  TEST_ASSERT_EQUAL_UINT8(2, state.purgePhase);
  TEST_ASSERT_EQUAL_UINT16(90, state.actuators.ventAngle);
  TEST_ASSERT_EQUAL_UINT8(100, state.actuators.fanPower);
  TEST_ASSERT_EQUAL_UINT8(0, state.ahSlopeSamples);
}

void test_state_machine_preset_dry_then_hold() {
  DeviceState state;
  state.air.valid = true;
  state.air.temperatureC = 60.0f;
  DryingStateMachine machine;
  machine.begin(state, 0);
  Setpoints setpoints;
  setpoints.airTemperatureC = 65.0f;
  setpoints.holdTemperatureC = 50.0f;
  setpoints.relativeHumidity = 15.0f;
  setpoints.drynessSlopeGm3PerHour = 0.3f;
  setpoints.heaterLimitC = 105.0f;
  setpoints.minDurationSeconds = 10;
  setpoints.maxDurationSeconds = 60;
  TEST_ASSERT_TRUE(machine.start(state, DryingMode::TimedPreset, setpoints, 0));
  machine.update(state, 0);
  state.air.temperatureC = 64.0f;
  machine.update(state, 1000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Drying), static_cast<int>(state.phase));
  // Min time served but the spool still releases moisture: keep drying.
  machine.update(state, 12000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Drying), static_cast<int>(state.phase));
  // Two consecutive quiet windows: dry -> Hold.
  state.drynessChecks = 2;
  machine.update(state, 13000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Hold), static_cast<int>(state.phase));
  TEST_ASSERT_EQUAL_UINT32(0, state.remainingSeconds);
  // Fresh moisture detected while holding: back to Drying with a fresh clock.
  state.drynessChecks = 0;
  machine.update(state, 20000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Drying), static_cast<int>(state.phase));
  TEST_ASSERT_EQUAL_UINT32(60, state.remainingSeconds);
  // Safety ceiling: settle into Hold even if never quiet.
  state.drynessChecks = 0;
  machine.update(state, 20000 + 61000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Hold), static_cast<int>(state.phase));
}

void test_state_machine_manual_stops_at_duration() {
  DeviceState state;
  state.air.valid = true;
  state.air.temperatureC = 44.0f;
  DryingStateMachine machine;
  machine.begin(state, 0);
  Setpoints setpoints;
  setpoints.airTemperatureC = 45.0f;
  setpoints.relativeHumidity = 20.0f;
  setpoints.heaterLimitC = 105.0f;
  setpoints.durationSeconds = 10;
  TEST_ASSERT_TRUE(machine.start(state, DryingMode::TimedManual, setpoints, 0));
  machine.update(state, 0);
  machine.update(state, 1000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Drying), static_cast<int>(state.phase));
  // Manual runs ignore dryness: the countdown decides.
  state.drynessChecks = 5;
  machine.update(state, 11000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Finish), static_cast<int>(state.phase));
}

void test_control_dryness_streak_from_sealed_windows() {
  AppConfig config;
  setDefaultConfig(config);
  SafetyService safety(75.0f);
  DryingStateMachine machine;
  ControlService control(config, safety, machine);
  control.begin();
  DeviceState state;
  state.air.valid = true;
  state.air.temperatureC = 45.0f;
  state.air.relativeHumidity = 30.0f;
  state.air.absoluteHumidityGm3 = 0.3f;
  state.heater.valid = true;
  state.heater.temperatureC = 45.0f;
  state.setpoints.airTemperatureC = 45.0f;
  state.setpoints.relativeHumidity = 20.0f;
  state.setpoints.heaterLimitC = 95.0f;
  state.setpoints.durationSeconds = 3600;
  state.setpoints.drynessSlopeGm3PerHour = 0.3f;
  machine.begin(state, 0);
  TEST_ASSERT_TRUE(machine.start(state, DryingMode::TimedPreset, state.setpoints, 0));
  control.update(state, 0);
  control.update(state, 1000);
  TEST_ASSERT_EQUAL(static_cast<int>(DryingPhase::Drying), static_cast<int>(state.phase));
  // Quiet sealed window: flat AH for ~5 min of samples, then purge.
  for (uint32_t t = 2000; t <= 302000; t += 30000) {
    state.air.absoluteHumidityGm3 = 0.3f;
    control.update(state, t);
  }
  state.air.absoluteHumidityGm3 = 6.0f;  // saturated -> purge begins
  control.update(state, 310000);
  TEST_ASSERT_EQUAL_UINT8(2, state.purgePhase);
  TEST_ASSERT_EQUAL_UINT8(1, state.drynessChecks);
  // Wet sealed window: AH climbing at ~18 g/m3/h after the settle completes
  // (t=491000), well above the 0.3 threshold. The rise triggers the next
  // purge exactly at t=1091000 (600 s sealed + rise above trigger); the
  // window judgement must reset the streak. The purge itself then ends
  // immediately (AH below the floor), so assert right at that tick.
  for (uint32_t t = 311000; t <= 1091000; t += 5000) {
    const float rise = t > 491000 ? 0.005f * static_cast<float>(t - 491000) / 1000.0f : 0.0f;
    state.air.absoluteHumidityGm3 = 0.5f + rise;
    control.update(state, t);
  }
  TEST_ASSERT_EQUAL_UINT8(2, state.purgePhase);
  TEST_ASSERT_EQUAL_UINT8(0, state.drynessChecks);
}

void runTests() {
  UNITY_BEGIN();
  RUN_TEST(test_absolute_humidity_round_trip);
  RUN_TEST(test_pid_output_is_bounded_and_rate_limited);
  RUN_TEST(test_state_machine_timed_run);
  RUN_TEST(test_state_machine_rejects_non_run_modes);
  RUN_TEST(test_weight_sensors_are_optional_for_safety);
  RUN_TEST(test_actuator_interlock_forces_fan_when_heating);
  RUN_TEST(test_actuator_interlock_idle_without_heat);
  RUN_TEST(test_thermostat_holds_band);
  RUN_TEST(test_thermostat_respects_min_switch_interval);
  RUN_TEST(test_heater_setpoint_ceiling);
  RUN_TEST(test_cascade_bounds_heater_setpoint_and_power);
  RUN_TEST(test_purge_scheduler_cycle);
  RUN_TEST(test_purge_scheduler_respects_min_seal);
  RUN_TEST(test_ah_slope_estimator_linear_rise);
  RUN_TEST(test_ah_slope_estimator_flat_and_reset);
  RUN_TEST(test_control_pulse_ventilation_and_dryness);
  RUN_TEST(test_state_machine_preset_dry_then_hold);
  RUN_TEST(test_state_machine_manual_stops_at_duration);
  RUN_TEST(test_control_dryness_streak_from_sealed_windows);
  UNITY_END();
}

#ifdef ARDUINO
void setup() {
  delay(1000);
  runTests();
}

void loop() {}
#else
int main(int argc, char** argv) {
  runTests();
  return 0;
}
#endif
