#include <unity.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "control/HumidityMath.h"
#include "control/PidController.h"
#include "core/DryingStateMachine.h"
#include "services/SafetyService.h"

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

void runTests() {
  UNITY_BEGIN();
  RUN_TEST(test_absolute_humidity_round_trip);
  RUN_TEST(test_pid_output_is_bounded_and_rate_limited);
  RUN_TEST(test_state_machine_timed_run);
  RUN_TEST(test_state_machine_rejects_non_run_modes);
  RUN_TEST(test_weight_sensors_are_optional_for_safety);
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
