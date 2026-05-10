#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace air360 {

enum class OtaState : std::uint8_t {
    kIdle = 0,
    kInProgress = 1,
    kSuccess = 2,
    kError = 3,
};

struct OtaStatus {
    OtaState state = OtaState::kIdle;
    std::uint32_t bytes_written = 0U;
    std::uint32_t content_length = 0U;
    std::uint64_t started_uptime_ms = 0U;
    std::uint64_t finished_uptime_ms = 0U;
    std::string error;
    std::string running_version;
    std::string running_slot_label;
    std::string target_slot_label;
    std::uint32_t target_slot_size_bytes = 0U;
    std::string pending_version;
    bool rollback_armed = false;
};

// Owns the ESP-IDF OTA write session. One transfer at a time; protected by an
// instance mutex so HTTP handlers and the status renderer can call snapshot()
// without racing the streaming writer.
class OtaService {
  public:
    OtaService();
    OtaService(const OtaService&) = delete;
    OtaService& operator=(const OtaService&) = delete;
    OtaService(OtaService&&) = delete;
    OtaService& operator=(OtaService&&) = delete;

    // Cancels any pending rollback by calling
    // esp_ota_mark_app_valid_cancel_rollback() when the running image is in
    // ESP_OTA_IMG_PENDING_VERIFY. Call this once boot reaches a known-good
    // state (after WebServer + managers are running).
    void confirmRunningImage();

    // Starts an OTA session targeting the next update partition.
    // content_length is informational only (used for progress UI); it may be 0
    // if the client did not send Content-Length.
    [[nodiscard]] esp_err_t begin(std::uint32_t content_length);

    // Streams a chunk to the target slot. Must be called only after begin().
    [[nodiscard]] esp_err_t writeChunk(const void* data, std::size_t size);

    // Finalizes the active transfer: esp_ota_end + image header validation +
    // esp_ota_set_boot_partition + schedules an esp_restart() ~1.5 s later
    // so the caller HTTP handler can flush its response.
    [[nodiscard]] esp_err_t commitAndScheduleReboot();

    // Cancels the active transfer; safe to call from any state.
    void abortTransfer(const char* reason);

    // Forces the running image to be marked invalid and reboots; bootloader
    // will fall back to the previous OTA slot. Returns ESP_ERR_INVALID_STATE
    // if the running image cannot be marked invalid (e.g. already committed).
    [[nodiscard]] esp_err_t requestRollback();

    OtaStatus snapshot() const;

  private:
    void lock() const;
    void unlock() const;
    void resetTransferLocked();
    void populateRunningInfoLocked();

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;

    esp_ota_handle_t handle_ = 0;
    const esp_partition_t* target_ = nullptr;
    bool transfer_active_ = false;
    OtaStatus status_{};
};

}  // namespace air360
