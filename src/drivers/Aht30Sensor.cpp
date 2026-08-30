#include "drivers/Aht30Sensor.h"

#include <cmath>

#include "control/HumidityMath.h"

Aht30Sensor::Aht30Sensor(TwoWire& wire, uint8_t address)
    : wire_(wire), address_(address) {}

bool Aht30Sensor::begin() {
  return sensor_.begin(&wire_, address_);
}

bool Aht30Sensor::update(uint32_t now) {
  sensors_event_t humidity;
  sensors_event_t temperature;
  sensor_.getEvent(&humidity, &temperature);
  if (!std::isfinite(temperature.temperature) ||
      !std::isfinite(humidity.relative_humidity) ||
      temperature.temperature < -40.0f || temperature.temperature > 100.0f ||
      humidity.relative_humidity < 0.0f || humidity.relative_humidity > 100.0f) {
    reading_.valid = false;
    return false;
  }
  reading_.temperatureC = temperature.temperature;
  reading_.relativeHumidity = humidity.relative_humidity;
  reading_.absoluteHumidityGm3 =
      relativeToAbsoluteHumidity(reading_.temperatureC, reading_.relativeHumidity);
  reading_.timestamp = now;
  reading_.valid = true;
  return true;
}
