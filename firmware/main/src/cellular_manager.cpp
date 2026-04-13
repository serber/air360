#include "air360/cellular_manager.hpp"

#include <cinttypes>

#include "air360/connectivity_checker.hpp"
#include "air360/modem_gpio.hpp"
#include "air360/network_manager.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.cellular";
constexpr std::uint32_t kCheckTimeoutMs = 5000U;
constexpr std::uint32_t kCheckRetries = 3U;

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CellularManager::init(NetworkManager& network_manager) {
    network_manager_ = &network_manager;
}

void CellularManager::start(const CellularConfig& config) {
    if (task_handle_ != nullptr) {
        ESP_LOGW(kTag, "start() called more than once — ignoring");
        return;
    }

    config_ = config;
    state_.enabled = (config.enabled != 0U);

    initModemGpios(config);

    if (!state_.enabled) {
        ESP_LOGI(kTag, "Cellular disabled — skipping task start");
        return;
    }

    const BaseType_t res = xTaskCreate(
        taskEntry,
        "cellular",
        kTaskStackBytes,
        this,
        kTaskPriority,
        &task_handle_);

    if (res != pdPASS) {
        ESP_LOGE(kTag, "Failed to create cellular task");
        task_handle_ = nullptr;
    } else {
        ESP_LOGI(kTag, "Cellular reconnect task started");
    }
}

const CellularState& CellularManager::state() const {
    return state_;
}

// ---------------------------------------------------------------------------
// FreeRTOS task
// ---------------------------------------------------------------------------

// static
void CellularManager::taskEntry(void* arg) {
    static_cast<CellularManager*>(arg)->taskBody();
    vTaskDelete(nullptr);
}

void CellularManager::taskBody() {
    ESP_LOGI(kTag, "Cellular task running");

    for (;;) {
        // Ensure the modem is awake before each connection attempt.
        setModemSleepPin(config_.sleep_gpio, false);

        const bool connected = attemptConnect();

        if (connected) {
            // PPP is up.  Block here until onPppDisconnected() clears the flag.
            while (state_.ppp_connected) {
                vTaskDelay(pdMS_TO_TICKS(1000U));
            }
            // Clean reconnect — reset backoff counter.
            state_.reconnect_attempts = 0U;
            state_.next_reconnect_uptime_ms = 0U;
            continue;
        }

        // Attempt failed.
        state_.reconnect_attempts++;
        ESP_LOGW(kTag, "Connection attempt %" PRIu32 " failed", state_.reconnect_attempts);

        if (state_.reconnect_attempts >= kMaxReconnectAttempts) {
            ESP_LOGW(kTag, "Max attempts reached — performing hardware reset");
            doHardwareReset();
            state_.reconnect_attempts = 0U;
            state_.next_reconnect_uptime_ms = 0U;
            // doHardwareReset() already waits for the modem to boot; go
            // straight to the next attempt without an additional backoff sleep.
            continue;
        }

        const std::uint32_t backoff_ms = computeBackoffMs(state_.reconnect_attempts);
        const std::uint64_t now_ms =
            static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL;
        state_.next_reconnect_uptime_ms = now_ms + backoff_ms;

        ESP_LOGI(kTag, "Reconnect backoff: %" PRIu32 " ms", backoff_ms);

        // Assert sleep/DTR during the wait to conserve modem power.
        setModemSleepPin(config_.sleep_gpio, true);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        setModemSleepPin(config_.sleep_gpio, false);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool CellularManager::attemptConnect() {
    // Phase 1 placeholder — real esp_modem bring-up replaces this body.
    ESP_LOGI(kTag, "attemptConnect: stub (Phase 1 not yet implemented)");
    return false;
}

void CellularManager::doHardwareReset() {
    ESP_LOGI(kTag, "Hardware reset: asserting power-off pulse (%" PRIu32 " ms)",
             kPwrkeyPowerOffMs);
    pulseModemPwrkey(config_.pwrkey_gpio, kPwrkeyPowerOffMs);
    vTaskDelay(pdMS_TO_TICKS(kModemShutdownWaitMs));

    ESP_LOGI(kTag, "Hardware reset: asserting power-on pulse (%" PRIu32 " ms)",
             kPwrkeyPowerOnMs);
    pulseModemPwrkey(config_.pwrkey_gpio, kPwrkeyPowerOnMs);
    vTaskDelay(pdMS_TO_TICKS(kModemBootWaitMs));

    ESP_LOGI(kTag, "Hardware reset complete");
}

// static
std::uint32_t CellularManager::computeBackoffMs(std::uint32_t attempt) {
    std::uint32_t backoff = kBaseBackoffMs;
    for (std::uint32_t i = 0U; i < attempt && backoff < kMaxBackoffMs; ++i) {
        backoff = (backoff <= kMaxBackoffMs / 2U) ? backoff * 2U : kMaxBackoffMs;
    }
    return (backoff < kMaxBackoffMs) ? backoff : kMaxBackoffMs;
}

// ---------------------------------------------------------------------------
// PPP session callbacks (called from the cellular task, Phase 1)
// ---------------------------------------------------------------------------

void CellularManager::onPppConnected(const char* ip_address, const char* check_host) {
    state_.ppp_connected = true;
    state_.ip_address = (ip_address != nullptr) ? ip_address : "";
    state_.last_error.clear();

    if (network_manager_ != nullptr) {
        network_manager_->setCellularStatus(true, ip_address);
    }

    ESP_LOGI(kTag, "PPP connected, IP: %s", state_.ip_address.c_str());

    const ConnectivityCheckResult result =
        runConnectivityCheck(check_host, kCheckTimeoutMs, kCheckRetries);

    switch (result) {
        case ConnectivityCheckResult::kSkipped:
            state_.connectivity_check_skipped = true;
            state_.connectivity_ok = false;
            ESP_LOGI(kTag, "Connectivity check skipped (no host configured)");
            break;
        case ConnectivityCheckResult::kOk:
            state_.connectivity_ok = true;
            state_.connectivity_check_skipped = false;
            ESP_LOGI(kTag, "Connectivity check OK (%s)", check_host);
            break;
        case ConnectivityCheckResult::kFailed:
            state_.connectivity_ok = false;
            state_.connectivity_check_skipped = false;
            ESP_LOGW(
                kTag,
                "Connectivity check failed (%s)",
                (check_host != nullptr) ? check_host : "");
            break;
    }
}

void CellularManager::onPppDisconnected(const char* reason) {
    state_.ppp_connected = false;
    state_.connectivity_ok = false;
    state_.ip_address.clear();

    if (network_manager_ != nullptr) {
        network_manager_->setCellularStatus(false, nullptr);
    }

    if (reason != nullptr && reason[0] != '\0') {
        state_.last_error = reason;
        ESP_LOGW(kTag, "PPP disconnected: %s", reason);
    } else {
        ESP_LOGI(kTag, "PPP disconnected");
    }
}

}  // namespace air360
