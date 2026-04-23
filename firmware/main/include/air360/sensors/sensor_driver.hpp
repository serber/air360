#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "air360/sensors/sensor_config.hpp"
#include "esp_err.h"

namespace air360 {

constexpr std::uint32_t kSensorPollFailureReinitThreshold = 3U;

// Tracks consecutive soft-poll failures per driver instance.
// A soft fail retains initialized_ = true; a hard fail (threshold reached)
// signals the caller to set initialized_ = false and trigger re-init.
struct SoftFailPolicy {
    std::uint32_t soft_fails = 0U;

    void onPollOk() { soft_fails = 0U; }

    // Returns true when soft_fails reaches kSensorPollFailureReinitThreshold.
    bool onPollErr() { return ++soft_fails >= kSensorPollFailureReinitThreshold; }
};

class I2cBusManager;
class UartPortManager;

struct SensorValue {
    SensorValueKind kind = SensorValueKind::kUnknown;
    float value = 0.0F;
};

struct SensorMeasurement {
    std::uint64_t sample_time_ms = 0U;
    std::uint8_t value_count = 0U;
    std::array<SensorValue, kMaxMeasurementValues> values{};

    void clear() {
        sample_time_ms = 0U;
        value_count = 0U;
        values.fill(SensorValue{});
    }

    bool addValue(SensorValueKind kind, float value) {
        if (value_count >= values.size()) {
            return false;
        }

        values[value_count++] = SensorValue{kind, value};
        return true;
    }

    bool empty() const {
        return value_count == 0U;
    }

    const SensorValue* findValue(SensorValueKind kind) const {
        for (std::size_t index = 0; index < value_count; ++index) {
            if (values[index].kind == kind) {
                return &values[index];
            }
        }
        return nullptr;
    }
};

struct SensorDriverContext {
    I2cBusManager* i2c_bus_manager = nullptr;
    UartPortManager* uart_port_manager = nullptr;
};

class SensorDriver {
  public:
    virtual ~SensorDriver() = default;

    virtual SensorType type() const = 0;
    virtual esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) = 0;
    virtual esp_err_t poll() = 0;
    virtual SensorMeasurement latestMeasurement() const = 0;
    virtual std::string lastError() const = 0;

    std::uint32_t softFailCount() const { return soft_fail_policy_.soft_fails; }

  protected:
    SoftFailPolicy soft_fail_policy_{};
};

}  // namespace air360
