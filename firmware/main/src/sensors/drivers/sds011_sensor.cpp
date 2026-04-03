#include "air360/sensors/drivers/sds011_sensor.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr TickType_t kSds011ReadTimeoutTicks = pdMS_TO_TICKS(100U);
constexpr std::size_t kSds011ReadBufferSize = 64U;
constexpr std::size_t kSds011MaxBytesPerPoll = 256U;
constexpr std::uint8_t kFrameStartByte = 0xAAU;
constexpr std::uint8_t kFrameDataByte = 0xC0U;
constexpr std::uint8_t kFrameEndByte = 0xABU;
constexpr std::size_t kFrameSize = 10U;

}  // namespace

SensorType Sds011Sensor::type() const {
    return SensorType::kSds011;
}

esp_err_t Sds011Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    measurement_.clear();
    last_error_.clear();
    frame_.fill(0U);
    frame_size_ = 0U;
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
        setError("Failed to open configured UART for SDS011 sensor.");
        return err;
    }

    uart_port_manager_->flush(record_.uart_port_id);
    initialized_ = true;
    return ESP_OK;
}

esp_err_t Sds011Sensor::poll() {
    if (!initialized_ || uart_port_manager_ == nullptr) {
        setError("SDS011 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    std::uint8_t buffer[kSds011ReadBufferSize]{};
    std::size_t total_bytes_read = 0U;
    bool saw_valid_frame = false;

    int bytes_read = uart_port_manager_->read(
        record_.uart_port_id,
        buffer,
        sizeof(buffer),
        kSds011ReadTimeoutTicks);
    if (bytes_read < 0) {
        setError("Failed to read SDS011 UART data.");
        initialized_ = false;
        return ESP_FAIL;
    }

    while (bytes_read > 0) {
        total_bytes_read += static_cast<std::size_t>(bytes_read);
        for (int index = 0; index < bytes_read; ++index) {
            if (consumeByte(buffer[index])) {
                saw_valid_frame = true;
            }
        }

        if (total_bytes_read >= kSds011MaxBytesPerPoll) {
            break;
        }

        const std::size_t remaining_capacity = kSds011MaxBytesPerPoll - total_bytes_read;
        const std::size_t next_chunk_size =
            remaining_capacity < sizeof(buffer) ? remaining_capacity : sizeof(buffer);
        bytes_read = uart_port_manager_->read(
            record_.uart_port_id,
            buffer,
            next_chunk_size,
            0);
        if (bytes_read < 0) {
            setError("Failed to read SDS011 UART data.");
            initialized_ = false;
            return ESP_FAIL;
        }
    }

    if (total_bytes_read == 0U) {
        setError("No SDS011 UART data received.");
        return ESP_OK;
    }

    if (!saw_valid_frame) {
        setError("No valid SDS011 frame received yet.");
        return ESP_OK;
    }

    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Sds011Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Sds011Sensor::lastError() const {
    return last_error_;
}

bool Sds011Sensor::consumeByte(std::uint8_t byte) {
    if (frame_size_ == 0U) {
        if (byte == kFrameStartByte) {
            frame_[0] = byte;
            frame_size_ = 1U;
        }
        return false;
    }

    if (frame_size_ >= frame_.size()) {
        frame_size_ = 0U;
    }

    frame_[frame_size_++] = byte;

    if (frame_size_ == 2U && frame_[1] != kFrameDataByte) {
        frame_size_ = byte == kFrameStartByte ? 1U : 0U;
        if (frame_size_ == 1U) {
            frame_[0] = kFrameStartByte;
        }
        return false;
    }

    if (frame_size_ < kFrameSize) {
        return false;
    }

    const bool has_valid_footer = frame_[9] == kFrameEndByte;
    std::uint8_t checksum = 0U;
    for (std::size_t index = 2U; index <= 7U; ++index) {
        checksum = static_cast<std::uint8_t>(checksum + frame_[index]);
    }

    const bool has_valid_checksum = checksum == frame_[8];
    frame_size_ = 0U;

    if (!has_valid_footer || !has_valid_checksum) {
        return false;
    }

    const std::uint16_t pm2_5_raw =
        static_cast<std::uint16_t>(frame_[2]) |
        (static_cast<std::uint16_t>(frame_[3]) << 8U);
    const std::uint16_t pm10_raw =
        static_cast<std::uint16_t>(frame_[4]) |
        (static_cast<std::uint16_t>(frame_[5]) << 8U);

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kPm2_5UgM3, static_cast<float>(pm2_5_raw) / 10.0F);
    measurement_.addValue(SensorValueKind::kPm10_0UgM3, static_cast<float>(pm10_raw) / 10.0F);
    return true;
}

void Sds011Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createSds011Sensor() {
    return std::make_unique<Sds011Sensor>();
}

}  // namespace air360
