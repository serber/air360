#pragma once

#include <cstddef>
#include <cstdint>

namespace air360 {

constexpr std::size_t kMaxConfiguredSensors = 8U;

enum class SensorType : std::uint8_t {
    kUnknown = 0U,
    kBme280 = 1U,
    kGpsNmea = 2U,
    kDht11 = 3U,
    kDht22 = 4U,
    kBme680 = 5U,
    kSps30 = 6U,
    kEns160 = 7U,
    kMe3No2 = 8U,
    kVeml7700 = 9U,
    kDs18b20 = 10U,
};

enum class TransportKind : std::uint8_t {
    kUnknown = 0U,
    kI2c = 1U,
    kAnalog = 2U,
    kUart = 3U,
    kGpio = 4U,
};

enum class SensorRuntimeState : std::uint8_t {
    kDisabled = 0U,
    kConfigured = 1U,
    kInitialized = 2U,
    kPolling = 3U,
    kAbsent = 4U,
    kUnsupported = 5U,
    kError = 6U,
};

inline const char* transportKindKey(TransportKind kind) {
    switch (kind) {
        case TransportKind::kI2c:
            return "i2c";
        case TransportKind::kAnalog:
            return "analog";
        case TransportKind::kUart:
            return "uart";
        case TransportKind::kGpio:
            return "gpio";
        case TransportKind::kUnknown:
        default:
            return "unknown";
    }
}

constexpr std::size_t kMaxMeasurementValues = 16U;

enum class SensorValueKind : std::uint8_t {
    kUnknown = 0U,
    kTemperatureC = 1U,
    kHumidityPercent = 2U,
    kPressureHpa = 3U,
    kLatitudeDeg = 4U,
    kLongitudeDeg = 5U,
    kAltitudeM = 6U,
    kSatellites = 7U,
    kSpeedKnots = 8U,
    kGasResistanceOhms = 9U,
    kPm1_0UgM3 = 10U,
    kPm2_5UgM3 = 11U,
    kPm4_0UgM3 = 12U,
    kPm10_0UgM3 = 13U,
    kNc0_5PerCm3 = 14U,
    kNc1_0PerCm3 = 15U,
    kNc2_5PerCm3 = 16U,
    kNc4_0PerCm3 = 17U,
    kNc10_0PerCm3 = 18U,
    kTypicalParticleSizeUm = 19U,
    kAqi = 20U,
    kTvocPpb = 21U,
    kEco2Ppm = 22U,
    kAdcRaw = 23U,
    kVoltageMv = 24U,
    kCourseDeg = 25U,
    kHdop = 26U,
    kIlluminanceLux = 27U,
};

inline const char* sensorValueKindKey(SensorValueKind kind) {
    switch (kind) {
        case SensorValueKind::kTemperatureC:
            return "temperature_c";
        case SensorValueKind::kHumidityPercent:
            return "humidity_percent";
        case SensorValueKind::kPressureHpa:
            return "pressure_hpa";
        case SensorValueKind::kLatitudeDeg:
            return "latitude_deg";
        case SensorValueKind::kLongitudeDeg:
            return "longitude_deg";
        case SensorValueKind::kAltitudeM:
            return "altitude_m";
        case SensorValueKind::kSatellites:
            return "satellites";
        case SensorValueKind::kSpeedKnots:
            return "speed_knots";
        case SensorValueKind::kGasResistanceOhms:
            return "gas_resistance_ohms";
        case SensorValueKind::kPm1_0UgM3:
            return "pm1_0_ug_m3";
        case SensorValueKind::kPm2_5UgM3:
            return "pm2_5_ug_m3";
        case SensorValueKind::kPm4_0UgM3:
            return "pm4_0_ug_m3";
        case SensorValueKind::kPm10_0UgM3:
            return "pm10_0_ug_m3";
        case SensorValueKind::kNc0_5PerCm3:
            return "nc0_5_per_cm3";
        case SensorValueKind::kNc1_0PerCm3:
            return "nc1_0_per_cm3";
        case SensorValueKind::kNc2_5PerCm3:
            return "nc2_5_per_cm3";
        case SensorValueKind::kNc4_0PerCm3:
            return "nc4_0_per_cm3";
        case SensorValueKind::kNc10_0PerCm3:
            return "nc10_0_per_cm3";
        case SensorValueKind::kTypicalParticleSizeUm:
            return "typical_particle_size_um";
        case SensorValueKind::kAqi:
            return "aqi";
        case SensorValueKind::kTvocPpb:
            return "tvoc_ppb";
        case SensorValueKind::kEco2Ppm:
            return "eco2_ppm";
        case SensorValueKind::kAdcRaw:
            return "adc_raw";
        case SensorValueKind::kVoltageMv:
            return "voltage_mv";
        case SensorValueKind::kCourseDeg:
            return "course_deg";
        case SensorValueKind::kHdop:
            return "hdop";
        case SensorValueKind::kIlluminanceLux:
            return "illuminance_lux";
        case SensorValueKind::kUnknown:
        default:
            return "unknown";
    }
}

inline const char* sensorValueKindLabel(SensorValueKind kind) {
    switch (kind) {
        case SensorValueKind::kTemperatureC:
            return "Temperature";
        case SensorValueKind::kHumidityPercent:
            return "Humidity";
        case SensorValueKind::kPressureHpa:
            return "Pressure";
        case SensorValueKind::kLatitudeDeg:
            return "Latitude";
        case SensorValueKind::kLongitudeDeg:
            return "Longitude";
        case SensorValueKind::kAltitudeM:
            return "Altitude";
        case SensorValueKind::kSatellites:
            return "Satellites";
        case SensorValueKind::kSpeedKnots:
            return "Speed";
        case SensorValueKind::kGasResistanceOhms:
            return "Gas resistance";
        case SensorValueKind::kPm1_0UgM3:
            return "PM1.0";
        case SensorValueKind::kPm2_5UgM3:
            return "PM2.5";
        case SensorValueKind::kPm4_0UgM3:
            return "PM4.0";
        case SensorValueKind::kPm10_0UgM3:
            return "PM10";
        case SensorValueKind::kNc0_5PerCm3:
            return "NC0.5";
        case SensorValueKind::kNc1_0PerCm3:
            return "NC1.0";
        case SensorValueKind::kNc2_5PerCm3:
            return "NC2.5";
        case SensorValueKind::kNc4_0PerCm3:
            return "NC4.0";
        case SensorValueKind::kNc10_0PerCm3:
            return "NC10";
        case SensorValueKind::kTypicalParticleSizeUm:
            return "Particle size";
        case SensorValueKind::kAqi:
            return "AQI";
        case SensorValueKind::kTvocPpb:
            return "TVOC";
        case SensorValueKind::kEco2Ppm:
            return "eCO2";
        case SensorValueKind::kAdcRaw:
            return "ADC raw";
        case SensorValueKind::kVoltageMv:
            return "Voltage";
        case SensorValueKind::kCourseDeg:
            return "Course";
        case SensorValueKind::kHdop:
            return "HDOP";
        case SensorValueKind::kIlluminanceLux:
            return "Illuminance";
        case SensorValueKind::kUnknown:
        default:
            return "Value";
    }
}

inline const char* sensorValueKindUnit(SensorValueKind kind) {
    switch (kind) {
        case SensorValueKind::kTemperatureC:
            return "C";
        case SensorValueKind::kHumidityPercent:
            return "%";
        case SensorValueKind::kPressureHpa:
            return "hPa";
        case SensorValueKind::kLatitudeDeg:
        case SensorValueKind::kLongitudeDeg:
            return "deg";
        case SensorValueKind::kAltitudeM:
            return "m";
        case SensorValueKind::kSatellites:
            return "";
        case SensorValueKind::kSpeedKnots:
            return "kn";
        case SensorValueKind::kCourseDeg:
            return "deg";
        case SensorValueKind::kHdop:
            return "";
        case SensorValueKind::kGasResistanceOhms:
            return "Ohm";
        case SensorValueKind::kPm1_0UgM3:
        case SensorValueKind::kPm2_5UgM3:
        case SensorValueKind::kPm4_0UgM3:
        case SensorValueKind::kPm10_0UgM3:
            return "ug/m3";
        case SensorValueKind::kNc0_5PerCm3:
        case SensorValueKind::kNc1_0PerCm3:
        case SensorValueKind::kNc2_5PerCm3:
        case SensorValueKind::kNc4_0PerCm3:
        case SensorValueKind::kNc10_0PerCm3:
            return "#/cm3";
        case SensorValueKind::kTypicalParticleSizeUm:
            return "um";
        case SensorValueKind::kAqi:
            return "";
        case SensorValueKind::kTvocPpb:
            return "ppb";
        case SensorValueKind::kEco2Ppm:
            return "ppm";
        case SensorValueKind::kAdcRaw:
            return "";
        case SensorValueKind::kVoltageMv:
            return "mV";
        case SensorValueKind::kIlluminanceLux:
            return "lux";
        case SensorValueKind::kUnknown:
        default:
            return "";
    }
}

inline int sensorValueKindPrecision(SensorValueKind kind) {
    switch (kind) {
        case SensorValueKind::kTemperatureC:
        case SensorValueKind::kHumidityPercent:
        case SensorValueKind::kPressureHpa:
        case SensorValueKind::kAltitudeM:
        case SensorValueKind::kSpeedKnots:
        case SensorValueKind::kCourseDeg:
        case SensorValueKind::kHdop:
            return 1;
        case SensorValueKind::kGasResistanceOhms:
            return 0;
        case SensorValueKind::kPm1_0UgM3:
        case SensorValueKind::kPm2_5UgM3:
        case SensorValueKind::kPm4_0UgM3:
        case SensorValueKind::kPm10_0UgM3:
        case SensorValueKind::kNc0_5PerCm3:
        case SensorValueKind::kNc1_0PerCm3:
        case SensorValueKind::kNc2_5PerCm3:
        case SensorValueKind::kNc4_0PerCm3:
        case SensorValueKind::kNc10_0PerCm3:
        case SensorValueKind::kTypicalParticleSizeUm:
            return 1;
        case SensorValueKind::kAqi:
        case SensorValueKind::kTvocPpb:
        case SensorValueKind::kEco2Ppm:
        case SensorValueKind::kAdcRaw:
        case SensorValueKind::kVoltageMv:
            return 0;
        case SensorValueKind::kIlluminanceLux:
            return 1;
        case SensorValueKind::kLatitudeDeg:
        case SensorValueKind::kLongitudeDeg:
            return 6;
        case SensorValueKind::kSatellites:
            return 0;
        case SensorValueKind::kUnknown:
        default:
            return 2;
    }
}

inline const char* sensorRuntimeStateKey(SensorRuntimeState state) {
    switch (state) {
        case SensorRuntimeState::kDisabled:
            return "disabled";
        case SensorRuntimeState::kConfigured:
            return "configured";
        case SensorRuntimeState::kInitialized:
            return "initialized";
        case SensorRuntimeState::kPolling:
            return "polling";
        case SensorRuntimeState::kAbsent:
            return "absent";
        case SensorRuntimeState::kUnsupported:
            return "unsupported";
        case SensorRuntimeState::kError:
        default:
            return "error";
    }
}

}  // namespace air360
