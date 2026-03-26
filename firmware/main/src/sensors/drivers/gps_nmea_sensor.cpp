#include "air360/sensors/drivers/gps_nmea_sensor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr TickType_t kGpsReadTimeoutTicks = pdMS_TO_TICKS(100U);
constexpr std::size_t kGpsReadBufferSize = 256U;
constexpr std::size_t kMaxLineBufferSize = 512U;

std::vector<std::string> splitCsv(const std::string& input) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    while (start <= input.size()) {
        const std::size_t comma = input.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(input.substr(start));
            break;
        }
        fields.push_back(input.substr(start, comma - start));
        start = comma + 1U;
    }
    return fields;
}

std::string trimSentence(const std::string& input) {
    std::size_t start = 0U;
    while (start < input.size() && (input[start] == '\r' || input[start] == '\n')) {
        ++start;
    }

    std::size_t end = input.size();
    while (end > start && (input[end - 1U] == '\r' || input[end - 1U] == '\n')) {
        --end;
    }

    return input.substr(start, end - start);
}

}  // namespace

SensorType GpsNmeaSensor::type() const {
    return SensorType::kGpsNmea;
}

esp_err_t GpsNmeaSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    measurement_.clear();
    line_buffer_.clear();
    last_error_.clear();
    initialized_ = false;
    has_latitude_ = false;
    has_longitude_ = false;
    has_altitude_ = false;
    has_satellites_ = false;
    has_speed_knots_ = false;
    latitude_deg_ = 0.0F;
    longitude_deg_ = 0.0F;
    altitude_m_ = 0.0F;
    satellites_ = 0.0F;
    speed_knots_ = 0.0F;

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

    bool saw_valid_fix = false;
    if (bytes_read > 0) {
        const esp_err_t err = processBytes(buffer, static_cast<std::size_t>(bytes_read), saw_valid_fix);
        if (err != ESP_OK) {
            return err;
        }
    }

    rebuildMeasurement(saw_valid_fix || (has_latitude_ && has_longitude_));
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement GpsNmeaSensor::latestMeasurement() const {
    return measurement_;
}

std::string GpsNmeaSensor::lastError() const {
    return last_error_;
}

esp_err_t GpsNmeaSensor::processBytes(
    const std::uint8_t* data,
    std::size_t size,
    bool& saw_valid_fix) {
    if (data == nullptr || size == 0U) {
        return ESP_OK;
    }

    line_buffer_.append(reinterpret_cast<const char*>(data), size);
    if (line_buffer_.size() > kMaxLineBufferSize) {
        line_buffer_.erase(0, line_buffer_.size() - kMaxLineBufferSize);
    }

    std::size_t newline_pos = std::string::npos;
    while ((newline_pos = line_buffer_.find('\n')) != std::string::npos) {
        const std::string raw_sentence = line_buffer_.substr(0, newline_pos + 1U);
        line_buffer_.erase(0, newline_pos + 1U);
        const std::string sentence = trimSentence(raw_sentence);
        if (sentence.empty()) {
            continue;
        }
        processSentence(sentence, saw_valid_fix);
    }

    return ESP_OK;
}

bool GpsNmeaSensor::processSentence(const std::string& sentence, bool& saw_valid_fix) {
    if (sentence.size() < 6U || sentence[0] != '$' || !validateChecksum(sentence)) {
        return false;
    }

    const std::size_t star = sentence.find('*');
    const std::string payload = sentence.substr(1U, star == std::string::npos ? std::string::npos : star - 1U);
    if (payload.size() < 5U) {
        return false;
    }

    const std::string type = payload.substr(2U, 3U);
    if (type == "GGA") {
        return processGga(payload, saw_valid_fix);
    }
    if (type == "RMC") {
        return processRmc(payload, saw_valid_fix);
    }

    return false;
}

bool GpsNmeaSensor::processGga(const std::string& payload, bool& saw_valid_fix) {
    const std::vector<std::string> fields = splitCsv(payload);
    if (fields.size() < 10U) {
        return false;
    }

    unsigned long fix_quality = 0UL;
    if (!parseUnsigned(fields[6], fix_quality) || fix_quality == 0UL) {
        has_latitude_ = false;
        has_longitude_ = false;
        has_altitude_ = false;
        has_satellites_ = false;
        return false;
    }

    float latitude = 0.0F;
    float longitude = 0.0F;
    if (!parseCoordinate(fields[2], fields[3], true, latitude) ||
        !parseCoordinate(fields[4], fields[5], false, longitude)) {
        return false;
    }

    latitude_deg_ = latitude;
    longitude_deg_ = longitude;
    has_latitude_ = true;
    has_longitude_ = true;
    saw_valid_fix = true;

    unsigned long satellites = 0UL;
    if (parseUnsigned(fields[7], satellites)) {
        satellites_ = static_cast<float>(satellites);
        has_satellites_ = true;
    }

    float altitude = 0.0F;
    if (parseFloat(fields[9], altitude)) {
        altitude_m_ = altitude;
        has_altitude_ = true;
    }

    return true;
}

bool GpsNmeaSensor::processRmc(const std::string& payload, bool& saw_valid_fix) {
    const std::vector<std::string> fields = splitCsv(payload);
    if (fields.size() < 8U) {
        return false;
    }

    if (fields[2] != "A") {
        return false;
    }

    float latitude = 0.0F;
    float longitude = 0.0F;
    if (!parseCoordinate(fields[3], fields[4], true, latitude) ||
        !parseCoordinate(fields[5], fields[6], false, longitude)) {
        return false;
    }

    latitude_deg_ = latitude;
    longitude_deg_ = longitude;
    has_latitude_ = true;
    has_longitude_ = true;
    saw_valid_fix = true;

    float speed_knots = 0.0F;
    if (parseFloat(fields[7], speed_knots)) {
        speed_knots_ = speed_knots;
        has_speed_knots_ = true;
    }

    return true;
}

bool GpsNmeaSensor::validateChecksum(const std::string& sentence) const {
    const std::size_t star = sentence.find('*');
    if (star == std::string::npos) {
        return true;
    }
    if (star + 2U >= sentence.size()) {
        return false;
    }

    unsigned char checksum = 0U;
    for (std::size_t index = 1U; index < star; ++index) {
        checksum ^= static_cast<unsigned char>(sentence[index]);
    }

    char* end = nullptr;
    const unsigned long expected = std::strtoul(sentence.substr(star + 1U, 2U).c_str(), &end, 16);
    return end != nullptr && *end == '\0' && checksum == static_cast<unsigned char>(expected);
}

bool GpsNmeaSensor::parseCoordinate(
    const std::string& raw_value,
    const std::string& hemisphere,
    bool latitude,
    float& out_value) {
    if (raw_value.empty() || hemisphere.empty()) {
        return false;
    }

    const double numeric = std::strtod(raw_value.c_str(), nullptr);
    if (numeric <= 0.0) {
        return false;
    }

    const double degrees_width = latitude ? 2.0 : 3.0;
    const double divisor = latitude ? 100.0 : 100.0;
    static_cast<void>(degrees_width);
    static_cast<void>(divisor);

    const int deg_digits = latitude ? 2 : 3;
    if (raw_value.size() <= static_cast<std::size_t>(deg_digits)) {
        return false;
    }

    const double degrees = std::strtod(raw_value.substr(0, static_cast<std::size_t>(deg_digits)).c_str(), nullptr);
    const double minutes = std::strtod(raw_value.substr(static_cast<std::size_t>(deg_digits)).c_str(), nullptr);
    double decimal = degrees + minutes / 60.0;

    if (hemisphere == "S" || hemisphere == "W") {
        decimal = -decimal;
    } else if (hemisphere != "N" && hemisphere != "E") {
        return false;
    }

    out_value = static_cast<float>(decimal);
    return true;
}

bool GpsNmeaSensor::parseFloat(const std::string& input, float& out_value) {
    if (input.empty()) {
        return false;
    }

    char* end = nullptr;
    const float parsed = std::strtof(input.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        return false;
    }

    out_value = parsed;
    return true;
}

bool GpsNmeaSensor::parseUnsigned(const std::string& input, unsigned long& out_value) {
    if (input.empty()) {
        return false;
    }

    char* end = nullptr;
    out_value = std::strtoul(input.c_str(), &end, 10);
    return end != nullptr && *end == '\0';
}

void GpsNmeaSensor::rebuildMeasurement(bool has_fix) {
    measurement_.clear();
    if (!has_fix || !has_latitude_ || !has_longitude_) {
        return;
    }

    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kLatitudeDeg, latitude_deg_);
    measurement_.addValue(SensorValueKind::kLongitudeDeg, longitude_deg_);
    if (has_altitude_) {
        measurement_.addValue(SensorValueKind::kAltitudeM, altitude_m_);
    }
    if (has_satellites_) {
        measurement_.addValue(SensorValueKind::kSatellites, satellites_);
    }
    if (has_speed_knots_) {
        measurement_.addValue(SensorValueKind::kSpeedKnots, speed_knots_);
    }
}

void GpsNmeaSensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createGpsNmeaSensor() {
    return std::make_unique<GpsNmeaSensor>();
}

}  // namespace air360
