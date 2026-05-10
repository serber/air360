#include "air360/ota_service.hpp"

#include <cstring>
#include <utility>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.ota";
constexpr std::uint32_t kMutexTakeSliceMs = 100U;
constexpr std::uint32_t kRebootDelayMs = 1500U;
constexpr std::uint32_t kRebootTaskStackBytes = 2048U;
constexpr UBaseType_t kRebootTaskPriority = 1U;
constexpr char kProjectName[] = "air360_firmware";

std::uint64_t uptimeMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL;
}

std::string partitionLabel(const esp_partition_t* part) {
    if (part == nullptr) {
        return "";
    }
    return std::string(part->label);
}

void rebootTaskEntry(void* /*arg*/) {
    vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));
    ESP_LOGW(kTag, "Rebooting after successful OTA");
    esp_restart();
}

void scheduleDeferredReboot() {
    const BaseType_t res = xTaskCreate(
        rebootTaskEntry,
        "air360_ota_reboot",
        kRebootTaskStackBytes,
        nullptr,
        kRebootTaskPriority,
        nullptr);
    if (res != pdPASS) {
        // Fall back to an immediate restart from the calling context.  Losing
        // the HTTP response is preferable to leaving the device in a half-
        // updated state with the new slot already marked as boot target.
        ESP_LOGE(kTag, "Failed to spawn reboot task — restarting immediately");
        esp_restart();
    }
}

}  // namespace

OtaService::OtaService() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    lock();
    populateRunningInfoLocked();
    unlock();
}

void OtaService::lock() const {
    if (mutex_ == nullptr) {
        return;
    }
    while (xSemaphoreTake(mutex_, pdMS_TO_TICKS(kMutexTakeSliceMs)) != pdTRUE) {
        // Spin politely.  Callers may be in tasks subscribed to TWDT; the
        // slice is short enough to avoid pet timeouts.
    }
}

void OtaService::unlock() const {
    if (mutex_ != nullptr) {
        static_cast<void>(xSemaphoreGive(mutex_));
    }
}

void OtaService::resetTransferLocked() {
    handle_ = 0;
    target_ = nullptr;
    transfer_active_ = false;
    status_.bytes_written = 0U;
    status_.content_length = 0U;
    status_.started_uptime_ms = 0U;
    status_.finished_uptime_ms = 0U;
    status_.target_slot_label.clear();
    status_.target_slot_size_bytes = 0U;
    status_.pending_version.clear();
}

void OtaService::populateRunningInfoLocked() {
    const esp_app_desc_t* app = esp_app_get_description();
    if (app != nullptr && app->version[0] != '\0') {
        status_.running_version = app->version;
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    status_.running_slot_label = partitionLabel(running);

    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    if (running != nullptr &&
        esp_ota_get_state_partition(running, &image_state) == ESP_OK) {
        status_.rollback_armed = (image_state == ESP_OTA_IMG_PENDING_VERIFY);
    } else {
        status_.rollback_armed = false;
    }
}

void OtaService::confirmRunningImage() {
    lock();
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    if (running == nullptr ||
        esp_ota_get_state_partition(running, &image_state) != ESP_OK) {
        unlock();
        return;
    }

    if (image_state == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(kTag, "Marked running image valid (rollback cancelled)");
            status_.rollback_armed = false;
        } else {
            ESP_LOGW(
                kTag,
                "Failed to mark running image valid: %s",
                esp_err_to_name(err));
        }
    }
    unlock();
}

esp_err_t OtaService::begin(std::uint32_t content_length) {
    lock();
    if (transfer_active_) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) {
        ESP_LOGE(kTag, "No OTA target partition available");
        status_.state = OtaState::kError;
        status_.error = "no_target_partition";
        unlock();
        return ESP_ERR_NOT_FOUND;
    }
    if (target == running) {
        ESP_LOGE(kTag, "Target partition equals running partition");
        status_.state = OtaState::kError;
        status_.error = "target_equals_running";
        unlock();
        return ESP_ERR_INVALID_STATE;
    }

    esp_ota_handle_t handle = 0;
    const esp_err_t err =
        esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
        status_.state = OtaState::kError;
        status_.error = esp_err_to_name(err);
        unlock();
        return err;
    }

    handle_ = handle;
    target_ = target;
    transfer_active_ = true;

    status_.state = OtaState::kInProgress;
    status_.bytes_written = 0U;
    status_.content_length = content_length;
    status_.started_uptime_ms = uptimeMilliseconds();
    status_.finished_uptime_ms = 0U;
    status_.error.clear();
    status_.target_slot_label = partitionLabel(target);
    status_.target_slot_size_bytes = target->size;
    status_.pending_version.clear();

    ESP_LOGI(
        kTag,
        "OTA started: target=%s size=%u content_length=%u",
        status_.target_slot_label.c_str(),
        static_cast<unsigned>(target->size),
        static_cast<unsigned>(content_length));

    unlock();
    return ESP_OK;
}

esp_err_t OtaService::writeChunk(const void* data, std::size_t size) {
    if (size == 0U) {
        return ESP_OK;
    }

    lock();
    if (!transfer_active_) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = esp_ota_write(handle_, data, size);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_ota_write failed: %s", esp_err_to_name(err));
        static_cast<void>(esp_ota_abort(handle_));
        resetTransferLocked();
        status_.state = OtaState::kError;
        status_.error = esp_err_to_name(err);
        status_.finished_uptime_ms = uptimeMilliseconds();
        unlock();
        return err;
    }

    status_.bytes_written += static_cast<std::uint32_t>(size);
    unlock();
    return ESP_OK;
}

esp_err_t OtaService::commitAndScheduleReboot() {
    lock();
    if (!transfer_active_) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t end_err = esp_ota_end(handle_);
    if (end_err != ESP_OK) {
        ESP_LOGE(
            kTag, "esp_ota_end failed: %s", esp_err_to_name(end_err));
        resetTransferLocked();
        status_.state = OtaState::kError;
        status_.error = esp_err_to_name(end_err);
        status_.finished_uptime_ms = uptimeMilliseconds();
        unlock();
        return end_err;
    }

    // Validate the new image's project name against ours so an operator cannot
    // accidentally flash an unrelated ESP-IDF binary.  Anti-rollback and code
    // signing are out of scope for v1; this is the lightweight guard.
    esp_app_desc_t pending_desc{};
    const esp_err_t desc_err =
        esp_ota_get_partition_description(target_, &pending_desc);
    if (desc_err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Could not read pending image description: %s",
            esp_err_to_name(desc_err));
        resetTransferLocked();
        status_.state = OtaState::kError;
        status_.error = "invalid_image_description";
        status_.finished_uptime_ms = uptimeMilliseconds();
        unlock();
        return desc_err;
    }

    if (std::strncmp(
            pending_desc.project_name,
            kProjectName,
            sizeof(pending_desc.project_name)) != 0) {
        ESP_LOGE(
            kTag,
            "Refusing image with project_name='%s' (expected '%s')",
            pending_desc.project_name,
            kProjectName);
        resetTransferLocked();
        status_.state = OtaState::kError;
        status_.error = "project_name_mismatch";
        status_.finished_uptime_ms = uptimeMilliseconds();
        unlock();
        return ESP_ERR_INVALID_ARG;
    }

    status_.pending_version = pending_desc.version;

    const esp_err_t boot_err = esp_ota_set_boot_partition(target_);
    if (boot_err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "esp_ota_set_boot_partition failed: %s",
            esp_err_to_name(boot_err));
        resetTransferLocked();
        status_.state = OtaState::kError;
        status_.error = esp_err_to_name(boot_err);
        status_.finished_uptime_ms = uptimeMilliseconds();
        unlock();
        return boot_err;
    }

    ESP_LOGI(
        kTag,
        "OTA committed: bytes=%u target=%s version=%s",
        static_cast<unsigned>(status_.bytes_written),
        status_.target_slot_label.c_str(),
        status_.pending_version.c_str());

    // Mark transfer as inactive so /ota/status reports success and a second
    // begin() would be rejected.  Leave target_slot_label/pending_version in
    // status_ so the UI can show them until reboot.
    handle_ = 0;
    target_ = nullptr;
    transfer_active_ = false;
    status_.state = OtaState::kSuccess;
    status_.error.clear();
    status_.finished_uptime_ms = uptimeMilliseconds();

    unlock();

    scheduleDeferredReboot();
    return ESP_OK;
}

void OtaService::abortTransfer(const char* reason) {
    lock();
    if (transfer_active_) {
        static_cast<void>(esp_ota_abort(handle_));
        resetTransferLocked();
        ESP_LOGW(
            kTag,
            "OTA aborted: %s",
            (reason != nullptr) ? reason : "unspecified");
    }
    status_.state = OtaState::kError;
    status_.error = (reason != nullptr) ? reason : "aborted";
    status_.finished_uptime_ms = uptimeMilliseconds();
    unlock();
}

esp_err_t OtaService::requestRollback() {
    lock();
    if (transfer_active_) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    unlock();

    ESP_LOGW(kTag, "Operator-initiated rollback requested");
    // mark_app_invalid_rollback_and_reboot returns only on failure.  On
    // success it triggers an immediate restart and never returns.
    const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(kTag, "Rollback request failed: %s", esp_err_to_name(err));
    return err;
}

OtaStatus OtaService::snapshot() const {
    lock();
    OtaStatus copy = status_;
    unlock();
    return copy;
}

}  // namespace air360
