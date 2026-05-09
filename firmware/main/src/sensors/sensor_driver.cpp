#include "air360/sensors/sensor_driver.hpp"

#include <utility>

#include "esp_log.h"

namespace air360 {

esp_err_t SensorDriver::reportPollFailure(
    const char* tag, std::string message, esp_err_t err) {
    last_error_ = std::move(message);
    if (soft_fail_policy_.onPollErr()) {
        ESP_LOGE(
            tag,
            "hard error after %u soft fails: %s",
            kSensorPollFailureReinitThreshold,
            last_error_.c_str());
        initialized_ = false;
    } else if (soft_fail_policy_.soft_fails == 1U) {
        ESP_LOGW(
            tag,
            "soft fail 1/%u: %s",
            kSensorPollFailureReinitThreshold,
            last_error_.c_str());
    }
    return err;
}

void SensorDriver::notePollSuccess() {
    soft_fail_policy_.onPollOk();
    last_error_.clear();
}

}  // namespace air360
