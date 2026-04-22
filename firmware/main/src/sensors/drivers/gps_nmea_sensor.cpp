#include "air360/sensors/drivers/gps_nmea_sensor.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include <TinyGPSPlus.h>
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr TickType_t kGpsReadTimeoutTicks = pdMS_TO_TICKS(100U);
constexpr std::size_t kGpsReadBufferSize = 256U;
constexpr std::size_t kGpsMaxBytesPerPoll = 2048U;

}  // namespace

GpsNmeaSensor::GpsNmeaSensor() : parser_(std::make_unique<TinyGPSPlus>()) {}

GpsNmeaSensor::~GpsNmeaSensor() = default;

SensorType GpsNmeaSensor::type() const {
    return SensorType::kGpsNmea;
}

esp_err_t GpsNmeaSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    if (parser_ != nullptr) {
        *parser_ = TinyGPSPlus();
    }
    measurement_.clear();
    last_error_.clear();
    poll_failure_count_ = 0U;
    initialized_ = false;

    if (uart_port_manager_ == nullptr || parser_ == nullptr) {
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
        if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
            initialized_ = false;
        }
        return ESP_FAIL;
    }

    while (bytes_read > 0) {
        total_bytes_read += static_cast<std::size_t>(bytes_read);
        for (int index = 0; index < bytes_read; ++index) {
            parser_->encode(static_cast<char>(buffer[index]));
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
            if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
                initialized_ = false;
            }
            return ESP_FAIL;
        }
    }

    if (total_bytes_read == 0U) {
        poll_failure_count_ = 0U;
        setError("No GPS UART data received.");
        return ESP_OK;
    }

    rebuildMeasurement();
    poll_failure_count_ = 0U;
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
    if (parser_ == nullptr) {
        setError("No GPS fix yet.");
        return;
    }

    bool has_values = false;
    const bool location_valid = parser_->location.isValid();
    if (location_valid) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kLatitudeDeg,
            static_cast<float>(parser_->location.lat()));
        measurement_.addValue(
            SensorValueKind::kLongitudeDeg,
            static_cast<float>(parser_->location.lng()));
    }
    if (parser_->altitude.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kAltitudeM,
            static_cast<float>(parser_->altitude.meters()));
    }
    if (parser_->satellites.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kSatellites,
            static_cast<float>(parser_->satellites.value()));
    }
    if (parser_->speed.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kSpeedKnots,
            static_cast<float>(parser_->speed.knots()));
    }
    if (parser_->course.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kCourseDeg,
            static_cast<float>(parser_->course.deg()));
    }
    if (parser_->hdop.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kHdop,
            static_cast<float>(parser_->hdop.hdop()));
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
