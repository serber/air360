#include "air360/sensors/drivers/gps_nmea_sensor.hpp"

#include <cstdint>
#include <memory>
#include <new>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.gps";
constexpr TickType_t kGpsReadTimeoutTicks = pdMS_TO_TICKS(100U);
constexpr std::size_t kGpsReadBufferSize = 256U;
constexpr std::size_t kGpsMaxBytesPerPoll = 2048U;

}  // namespace

GpsNmeaSensor::GpsNmeaSensor() = default;

GpsNmeaSensor::~GpsNmeaSensor() = default;

SensorType GpsNmeaSensor::type() const {
    return SensorType::kGpsNmea;
}

void GpsNmeaSensor::resetParser() {
    // Placement-new avoids heap churn on re-init: the parser lives in-place
    // as a data member, so destroy-then-reconstruct keeps allocations out of
    // the sensor poll/init hot path.
    parser_.~TinyGPSPlus();
    new (&parser_) TinyGPSPlus();
}

esp_err_t GpsNmeaSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    resetParser();
    measurement_.clear();
    last_error_.clear();
    soft_fail_policy_.onPollOk();
    initialized_ = false;

    if (uart_port_manager_ == nullptr) {
        setError("UART port manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = uart_port_manager_->open(
        record_.uart_port_id,
        record_.uart_rx_gpio_pin,
        record_.uart_tx_gpio_pin,
        record_.uart_baud_rate);
    if (err != ESP_OK) {
        setError("Failed to open configured UART for GPS sensor.");
        return err;
    }

    uart_port_manager_->flush(record_.uart_port_id);
    initialized_ = true;
    return ESP_OK;
}

esp_err_t GpsNmeaSensor::poll() {
    if (!initialized_ || uart_port_manager_ == nullptr) {
        setError("GPS sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    std::uint8_t buffer[kGpsReadBufferSize]{};
    std::size_t total_bytes_read = 0U;
    int bytes_read = uart_port_manager_->read(
        record_.uart_port_id,
        buffer,
        sizeof(buffer),
        kGpsReadTimeoutTicks);
    if (bytes_read < 0) {
        setError("Failed to read GPS UART data.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return ESP_FAIL;
    }

    while (bytes_read > 0) {
        total_bytes_read += static_cast<std::size_t>(bytes_read);
        for (int index = 0; index < bytes_read; ++index) {
            parser_.encode(static_cast<char>(buffer[index]));
        }

        if (total_bytes_read >= kGpsMaxBytesPerPoll) {
            break;
        }

        const std::size_t remaining_capacity = kGpsMaxBytesPerPoll - total_bytes_read;
        const std::size_t next_chunk_size =
            remaining_capacity < sizeof(buffer) ? remaining_capacity : sizeof(buffer);
        bytes_read = uart_port_manager_->read(
            record_.uart_port_id,
            buffer,
            next_chunk_size,
            0);
        if (bytes_read < 0) {
            setError("Failed to read GPS UART data.");
            if (soft_fail_policy_.onPollErr()) {
                ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
                initialized_ = false;
            } else if (soft_fail_policy_.soft_fails == 1U) {
                ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            }
            return ESP_FAIL;
        }
    }

    if (total_bytes_read == 0U) {
        soft_fail_policy_.onPollOk();
        setError("No GPS UART data received.");
        return ESP_OK;
    }

    rebuildMeasurement();
    soft_fail_policy_.onPollOk();
    return ESP_OK;
}

SensorMeasurement GpsNmeaSensor::latestMeasurement() const {
    return measurement_;
}

std::string GpsNmeaSensor::lastError() const {
    return last_error_;
}

void GpsNmeaSensor::rebuildMeasurement() {
    measurement_.clear();

    bool has_values = false;
    const bool location_valid = parser_.location.isValid();
    if (location_valid) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kLatitudeDeg,
            static_cast<float>(parser_.location.lat()));
        measurement_.addValue(
            SensorValueKind::kLongitudeDeg,
            static_cast<float>(parser_.location.lng()));
    }
    if (parser_.altitude.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kAltitudeM,
            static_cast<float>(parser_.altitude.meters()));
    }
    if (parser_.satellites.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kSatellites,
            static_cast<float>(parser_.satellites.value()));
    }
    if (parser_.speed.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kSpeedKnots,
            static_cast<float>(parser_.speed.knots()));
    }
    if (parser_.course.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kCourseDeg,
            static_cast<float>(parser_.course.deg()));
    }
    if (parser_.hdop.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kHdop,
            static_cast<float>(parser_.hdop.hdop()));
    }

    if (location_valid) {
        last_error_.clear();
    } else {
        setError("No GPS fix yet.");
    }
}

void GpsNmeaSensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createGpsNmeaSensor() {
    return std::make_unique<GpsNmeaSensor>();
}

}  // namespace air360
