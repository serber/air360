#include "air360/sensors/drivers/sds011_sensor.hpp"

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {
constexpr char kTag[] = "air360.sensor.sds011";
constexpr std::size_t kSds011FrameSize = 10U;
constexpr std::size_t kSds011CommandSize = 19U;
constexpr std::size_t kSds011ReadBufferSize = 64U;
constexpr std::size_t kSds011EventQueueSize = 8U;
constexpr std::uint32_t kSds011ReadTimeoutMs = 50U;
constexpr std::uint8_t kSds011Head = 0xAAU;
constexpr std::uint8_t kSds011CommandId = 0xB4U;
constexpr std::uint8_t kSds011MeasurementId = 0xC0U;
constexpr std::uint8_t kSds011Tail = 0xABU;
constexpr std::uint8_t kSds011ReportModeCommand = 0x02U;
constexpr std::uint8_t kSds011QueryDataCommand = 0x04U;
constexpr std::uint8_t kSds011SleepWorkCommand = 0x06U;
constexpr std::uint8_t kSds011WorkPeriodCommand = 0x08U;
constexpr std::uint8_t kSds011ReadOperation = 0x00U;
constexpr std::uint8_t kSds011WriteOperation = 0x01U;
constexpr std::uint8_t kSds011PassiveReportMode = 0x01U;
constexpr std::uint8_t kSds011WorkMode = 0x01U;
constexpr std::uint8_t kSds011ContinuousWorkPeriod = 0x00U;

std::array<std::uint8_t, kSds011CommandSize> makeCommand(
    std::uint8_t command,
    std::uint8_t operation,
    std::uint8_t value) {
    std::array<std::uint8_t, kSds011CommandSize> frame{};
    frame[0] = kSds011Head;
    frame[1] = kSds011CommandId;
    frame[2] = command;
    frame[3] = operation;
    frame[4] = value;
    frame[15] = 0xFFU;
    frame[16] = 0xFFU;

    std::uint8_t checksum = 0U;
    for (std::size_t index = 2U; index <= 16U; ++index) {
        checksum = static_cast<std::uint8_t>(checksum + frame[index]);
    }
    frame[17] = checksum;
    frame[18] = kSds011Tail;
    return frame;
}
}  // namespace

SensorType Sds011Sensor::type() const {
    return SensorType::kSds011;
}

esp_err_t Sds011Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    measurement_.clear();
    last_error_.clear();
    soft_fail_policy_.onPollOk();
    resetParser();
    uart_overrun_count_ = 0U;
    initialized_ = false;

    if (uart_port_manager_ == nullptr) {
        setError("UART port manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t open_err = uart_port_manager_->open(
        record_.uart_port_id,
        record_.uart_rx_gpio_pin,
        record_.uart_tx_gpio_pin,
        record_.uart_baud_rate,
        UartPortManager::kDefaultRxBufferSize,
        kSds011EventQueueSize);
    if (open_err != ESP_OK) {
        setError("Failed to open configured UART for SDS011 sensor.");
        return open_err;
    }

    const esp_err_t configure_err = configureSensor();
    if (configure_err != ESP_OK) {
        setError("Failed to configure SDS011 sensor.");
        return configure_err;
    }

    const esp_err_t flush_err = uart_port_manager_->flush(record_.uart_port_id);
    if (flush_err != ESP_OK) {
        setError("Failed to flush SDS011 UART input.");
        return flush_err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Sds011Sensor::poll() {
    if (!initialized_ || uart_port_manager_ == nullptr) {
        setError("SDS011 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = drainUartEvents();
    if (err != ESP_OK) {
        return handlePollError(err, "Failed to process SDS011 UART events.");
    }

    err = wakeSensor();
    if (err != ESP_OK) {
        return handlePollError(err, "Failed to wake SDS011 sensor.");
    }

    err = sendCommand(kSds011QueryDataCommand, kSds011ReadOperation, 0U);
    if (err != ESP_OK) {
        return handlePollError(err, "Failed to send SDS011 query command.");
    }

    bool found_frame = false;
    err = readAvailableFrames(found_frame);
    if (err != ESP_OK) {
        return handlePollError(err, "Failed to read SDS011 UART data.");
    }

    err = drainUartEvents();
    if (err != ESP_OK) {
        return handlePollError(err, "Failed to process SDS011 UART events.");
    }

    soft_fail_policy_.onPollOk();
    if (found_frame) {
        last_error_.clear();
    }
    return ESP_OK;
}

SensorMeasurement Sds011Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Sds011Sensor::lastError() const {
    return last_error_;
}

esp_err_t Sds011Sensor::drainUartEvents() {
    if (uart_port_manager_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    UartPortManager::EventSummary event_summary{};
    const esp_err_t err = uart_port_manager_->drainEvents(record_.uart_port_id, event_summary);
    if (err != ESP_OK) {
        return err;
    }

    if (event_summary.overrun_count > 0U) {
        uart_overrun_count_ += event_summary.overrun_count;
        ESP_LOGW(
            kTag,
            "SDS011 UART overrun events=%u total=%" PRIu32 " port=%u",
            static_cast<unsigned>(event_summary.overrun_count),
            uart_overrun_count_,
            static_cast<unsigned>(record_.uart_port_id));
        resetParser();
    }

    return ESP_OK;
}

esp_err_t Sds011Sensor::sendCommand(
    std::uint8_t command,
    std::uint8_t operation,
    std::uint8_t value) {
    if (uart_port_manager_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::array<std::uint8_t, kSds011CommandSize> frame =
        makeCommand(command, operation, value);
    const int bytes_written = uart_port_manager_->write(
        record_.uart_port_id,
        frame.data(),
        frame.size());
    return bytes_written == static_cast<int>(frame.size())
        ? ESP_OK
        : ESP_FAIL;
}

esp_err_t Sds011Sensor::wakeSensor() {
    return sendCommand(kSds011SleepWorkCommand, kSds011WriteOperation, kSds011WorkMode);
}

esp_err_t Sds011Sensor::configureSensor() {
    esp_err_t err = wakeSensor();
    if (err != ESP_OK) {
        return err;
    }

    err = sendCommand(
        kSds011WorkPeriodCommand,
        kSds011WriteOperation,
        kSds011ContinuousWorkPeriod);
    if (err != ESP_OK) {
        return err;
    }

    return sendCommand(
        kSds011ReportModeCommand,
        kSds011WriteOperation,
        kSds011PassiveReportMode);
}

esp_err_t Sds011Sensor::readAvailableFrames(bool& out_found_frame) {
    out_found_frame = false;
    Reading latest_reading{};
    std::uint8_t buffer[kSds011ReadBufferSize]{};
    std::size_t total_bytes_read = 0U;

    while (true) {
        const TickType_t timeout_ticks =
            total_bytes_read == 0U ? pdMS_TO_TICKS(kSds011ReadTimeoutMs) : 0;
        const int bytes_read = uart_port_manager_->read(
            record_.uart_port_id,
            buffer,
            sizeof(buffer),
            timeout_ticks);
        if (bytes_read < 0) {
            return ESP_FAIL;
        }

        if (bytes_read == 0) {
            break;
        }

        total_bytes_read += static_cast<std::size_t>(bytes_read);
        for (int index = 0; index < bytes_read; ++index) {
            bool frame_ready = false;
            Reading reading{};
            feedByte(buffer[index], frame_ready, reading);
            if (frame_ready) {
                latest_reading = reading;
                out_found_frame = true;
            }
        }

        std::size_t buffered_length = 0U;
        const esp_err_t buffered_err =
            uart_port_manager_->bufferedDataLength(record_.uart_port_id, buffered_length);
        if (buffered_err != ESP_OK) {
            return buffered_err;
        }
        if (buffered_length == 0U) {
            break;
        }
    }

    if (out_found_frame) {
        storeMeasurement(latest_reading);
    } else if (total_bytes_read == 0U) {
        setError("No SDS011 UART data received.");
    } else {
        setError("No valid SDS011 frame received.");
    }

    return ESP_OK;
}

void Sds011Sensor::feedByte(
    std::uint8_t byte,
    bool& out_frame_ready,
    Reading& out_reading) {
    out_frame_ready = false;
    if (frame_index_ == 0U) {
        if (byte == 0xAAU) {
            frame_[frame_index_++] = byte;
        }
        return;
    }

    if (frame_index_ == 1U && byte != kSds011MeasurementId) {
        frame_index_ = byte == kSds011Head ? 1U : 0U;
        frame_[0] = byte == kSds011Head ? byte : 0U;
        return;
    }

    frame_[frame_index_++] = byte;
    if (frame_index_ < kSds011FrameSize) {
        return;
    }

    Reading reading{};
    if (decodeFrame(reading)) {
        out_reading = reading;
        out_frame_ready = true;
    }
    frame_index_ = 0U;
}

bool Sds011Sensor::decodeFrame(Reading& out_reading) const {
    if (frame_[0] != kSds011Head ||
        frame_[1] != kSds011MeasurementId ||
        frame_[9] != kSds011Tail) {
        return false;
    }

    std::uint8_t checksum = 0U;
    for (std::size_t index = 2U; index <= 7U; ++index) {
        checksum = static_cast<std::uint8_t>(checksum + frame_[index]);
    }
    if (checksum != frame_[8]) {
        return false;
    }

    const std::uint16_t pm2_5_raw =
        static_cast<std::uint16_t>(frame_[2]) |
        (static_cast<std::uint16_t>(frame_[3]) << 8U);
    const std::uint16_t pm10_raw =
        static_cast<std::uint16_t>(frame_[4]) |
        (static_cast<std::uint16_t>(frame_[5]) << 8U);
    out_reading.pm2_5_ug_m3 = static_cast<float>(pm2_5_raw) / 10.0F;
    out_reading.pm10_ug_m3 = static_cast<float>(pm10_raw) / 10.0F;
    return true;
}

void Sds011Sensor::storeMeasurement(const Reading& reading) {
    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kPm2_5UgM3, reading.pm2_5_ug_m3);
    measurement_.addValue(SensorValueKind::kPm10_0UgM3, reading.pm10_ug_m3);
}

esp_err_t Sds011Sensor::handlePollError(esp_err_t err, const char* message) {
    setError(message != nullptr ? message : esp_err_to_name(err));
    if (soft_fail_policy_.onPollErr()) {
        ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        initialized_ = false;
        resetParser();
    } else if (soft_fail_policy_.soft_fails == 1U) {
        ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
    }
    return err;
}

void Sds011Sensor::resetParser() {
    frame_.fill(0U);
    frame_index_ = 0U;
}

void Sds011Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createSds011Sensor() {
    return std::make_unique<Sds011Sensor>();
}

}  // namespace air360
