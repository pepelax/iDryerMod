#include "control/HumidityMath.h"

#include <cmath>

namespace {
// kg/mol; the result below is converted to g/m^3 exactly once.
constexpr float kWaterVaporMolarMass = 0.018016f;
constexpr float kGasConstant = 8.314462618f;
constexpr float kKelvinOffset = 273.15f;

float saturationVaporPressurePa(float temperatureC) {
  return 610.94f * std::exp((17.625f * temperatureC) / (temperatureC + 243.04f));
}
}  // namespace

float relativeToAbsoluteHumidity(float temperatureC, float relativeHumidity) {
  const float rh = std::fmax(0.0f, std::fmin(100.0f, relativeHumidity));
  const float vaporPressure = saturationVaporPressurePa(temperatureC) * rh / 100.0f;
  const float temperatureK = temperatureC + kKelvinOffset;
  if (temperatureK <= 0.0f) return 0.0f;
  return 1000.0f * kWaterVaporMolarMass * vaporPressure /
         (kGasConstant * temperatureK);
}

float absoluteToRelativeHumidity(float temperatureC, float absoluteHumidityGm3) {
  const float saturation = relativeToAbsoluteHumidity(temperatureC, 100.0f);
  if (saturation <= 0.0f) return 0.0f;
  const float rh = 100.0f * absoluteHumidityGm3 / saturation;
  return std::fmax(0.0f, std::fmin(100.0f, rh));
}
