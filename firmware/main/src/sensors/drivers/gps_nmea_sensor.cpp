#include "air360/sensors/drivers/gps_nmea_sensor.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "TinyGPS++.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr TickType_t kGpsReadTimeoutTicks = pdMS_TO_TICKS(100U);
constexpr std::size_t kGpsReadBufferSize = 256U;

}  // namespace

GpsNmeaSensor::~GpsNmeaSensor() = default;

SensorType GpsNmeaSensor::type() const {
    return SensorType::kGpsNmea;
}

esp_err_t GpsNmeaSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    parser_ = std::make_unique<TinyGPSPlus>();
    measurement_.clear();
    last_error_.clear();
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
    const int bytes_read = uart_port_manager_->read(
        record_.uart_port_id,
        buffer,
        sizeof(buffer),
        kGpsReadTimeoutTicks);
    if (bytes_read < 0) {
        setError("Failed to read GPS UART data.");
        initialized_ = false;
        return ESP_FAIL;
    }

    if (bytes_read > 0) {
        for (int index = 0; index < bytes_read; ++index) {
            parser_->encode(static_cast<char>(buffer[index]));
        }
    }

    rebuildMeasurement();
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
    if (parser_ == nullptr || !parser_->location.isValid()) {
        setError("No GPS fix yet.");
        return;
    }

    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(
        SensorValueKind::kLatitudeDeg,
        static_cast<float>(parser_->location.lat()));
    measurement_.addValue(
        SensorValueKind::kLongitudeDeg,
        static_cast<float>(parser_->location.lng()));
    if (parser_->altitude.isValid()) {
        measurement_.addValue(
            SensorValueKind::kAltitudeM,
            static_cast<float>(parser_->altitude.meters()));
    }
    if (parser_->satellites.isValid()) {
        measurement_.addValue(
            SensorValueKind::kSatellites,
            static_cast<float>(parser_->satellites.value()));
    }
    if (parser_->speed.isValid()) {
        measurement_.addValue(
            SensorValueKind::kSpeedKnots,
            static_cast<float>(parser_->speed.knots()));
    }

    last_error_.clear();
}

void GpsNmeaSensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createGpsNmeaSensor() {
    return std::make_unique<GpsNmeaSensor>();
}

}  // namespace air360
