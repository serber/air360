#include "air360/cellular_manager.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "air360/connectivity_checker.hpp"
#include "air360/modem_gpio.hpp"
#include "air360/network_manager.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
// esp_modem_api.h (C API) uses PdpContext from the C++ layer — pull it in first.
#include "cxx_include/esp_modem_types.hpp"
using esp_modem::PdpContext;
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.cellular";
constexpr std::uint32_t kCheckTimeoutMs = 5000U;
constexpr std::uint32_t kCheckRetries   = 3U;
constexpr std::uint32_t kMutexTakeSliceMs = 1000U;
constexpr std::uint32_t kPppMonitorWaitMs = 25000U;
constexpr std::uint32_t kPppProbeTimeoutMs = 1000U;
constexpr std::uint32_t kPppProbeRetries = 1U;
constexpr std::uint8_t kPppProbeFailureThreshold = 2U;
// Maximum wait/sleep slice between watchdog resets.
constexpr std::uint32_t kWdtFeedSliceMs = 2000U;

std::uint64_t uptimeMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL;
}

void resetTaskWatchdog() {
    (void)esp_task_wdt_reset();
}

// Sleep for total_ms while feeding the task watchdog every kWdtFeedSliceMs.
// Must only be called from a task subscribed to the TWDT.
void wdtFeedingDelay(std::uint32_t total_ms) {
    while (total_ms > 0U) {
        const std::uint32_t slice = std::min(total_ms, kWdtFeedSliceMs);
        vTaskDelay(pdMS_TO_TICKS(slice));
        resetTaskWatchdog();
        total_ms -= slice;
    }
}

EventBits_t waitEventBitsWithWatchdog(
    EventGroupHandle_t event_group,
    EventBits_t bits_to_wait_for,
    BaseType_t clear_on_exit,
    BaseType_t wait_for_all_bits,
    std::uint32_t timeout_ms) {
    EventBits_t bits = 0U;
    std::uint32_t waited_ms = 0U;
    while (waited_ms < timeout_ms) {
        const std::uint32_t remaining_ms = timeout_ms - waited_ms;
        const std::uint32_t slice_ms = std::min(remaining_ms, kWdtFeedSliceMs);
        bits = xEventGroupWaitBits(
            event_group, bits_to_wait_for, clear_on_exit, wait_for_all_bits,
            pdMS_TO_TICKS(slice_ms));
        resetTaskWatchdog();
        if ((bits & bits_to_wait_for) != 0U) {
            return bits;
        }
        waited_ms += slice_ms;
    }
    return bits;
}

bool registrationStateIsRegistered(int state) {
    return state == 1 || state == 5 || state == 6 || state == 7 ||
           state == 9 || state == 10;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

CellularManager::CellularManager() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    ppp_event_group_ = xEventGroupCreateStatic(&ppp_event_group_buf_);
}

void CellularManager::lock() const {
    if (mutex_ != nullptr) {
        while (xSemaphoreTake(mutex_, pdMS_TO_TICKS(kMutexTakeSliceMs)) != pdTRUE) {
            resetTaskWatchdog();
        }
    }
}

void CellularManager::unlock() const {
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void CellularManager::init(NetworkManager& network_manager) {
    network_manager_ = &network_manager;
}

void CellularManager::start(const CellularConfig& config) {
    if (task_handle_ != nullptr) {
        ESP_LOGW(kTag, "start() called more than once — ignoring");
        return;
    }

    lock();
    config_ = config;
    state_.enabled = (config.enabled != 0U);
    unlock();

    initModemGpios(config);

    if (config.enabled == 0U) {
        ESP_LOGI(kTag, "Cellular disabled — skipping task start");
        return;
    }

    const BaseType_t res = xTaskCreate(
        taskEntry, "cellular", kTaskStackBytes, this, kTaskPriority, &task_handle_);

    if (res != pdPASS) {
        ESP_LOGE(kTag, "Failed to create cellular task");
        task_handle_ = nullptr;
    } else {
        ESP_LOGI(kTag, "Cellular reconnect task started");
    }
}

CellularState CellularManager::state() const {
    lock();
    const CellularState snapshot = state_;
    unlock();
    return snapshot;
}

std::size_t CellularManager::taskStackHighWaterMarkBytes() const {
    if (task_handle_ == nullptr) {
        return 0U;
    }

    return static_cast<std::size_t>(uxTaskGetStackHighWaterMark(task_handle_)) *
           sizeof(StackType_t);
}

// ---------------------------------------------------------------------------
// FreeRTOS task
// ---------------------------------------------------------------------------

// static
void CellularManager::taskEntry(void* arg) {
    static_cast<CellularManager*>(arg)->taskBody();
    esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
}

void CellularManager::taskBody() {
    ESP_LOGI(kTag, "Cellular task running");
    const esp_err_t wdt_err = esp_task_wdt_add(nullptr);
    if (wdt_err != ESP_OK) {
        ESP_LOGW(kTag, "TWDT subscribe failed: %s", esp_err_to_name(wdt_err));
    }
    ESP_LOGI(kTag, "TWDT: air360_cellular subscribed");

    resetFailureWindow();

    for (;;) {
        // Ensure the modem is awake before connecting.
        setModemSleepPin(config_.sleep_gpio, false);

        // attemptConnect() blocks until the session is fully over
        // (either never established, or established then dropped).
        const bool was_connected = attemptConnect();
        resetTaskWatchdog();

        if (was_connected) {
            resetFailureWindow();
            continue;
        }

        // Setup failure. Escalation is based on elapsed failure-window time,
        // not raw attempt count.
        const std::uint64_t now_ms = uptimeMilliseconds();
        if (failure_window_start_ms_ == 0U) {
            failure_window_start_ms_ = now_ms;
        }

        std::uint32_t consecutive_failures = 0U;
        lock();
        state_.reconnect_attempts++;
        state_.consecutive_failures++;
        consecutive_failures = state_.consecutive_failures;
        unlock();

        const std::uint64_t failure_elapsed_ms = now_ms - failure_window_start_ms_;
        ESP_LOGW(
            kTag,
            "Cellular failure %" PRIu32 ", failure window %" PRIu64 " ms",
            consecutive_failures,
            failure_elapsed_ms);

        if (!hard_retry_logged_ && failure_elapsed_ms >= kHardRetryAfterMs) {
            hard_retry_logged_ = true;
            ESP_LOGE(
                kTag,
                "Cellular escalation: soft retry -> hard command/data retry "
                "after %" PRIu64 " ms of continuous failure",
                failure_elapsed_ms);
        }

        if (failure_elapsed_ms >= kPwrkeyAfterMs) {
            if (pwrkey_cycles_in_failure_window_ >= kMaxPwrkeyBeforeReboot) {
                ESP_LOGE(
                    kTag,
                    "Cellular escalation: PWRKEY -> system reboot after %" PRIu32
                    " PWRKEY cycle(s) in the current failure window",
                    pwrkey_cycles_in_failure_window_);
                esp_restart();
            }

            std::uint64_t last_pwrkey_ms = 0U;
            lock();
            last_pwrkey_ms = state_.last_pwrkey_uptime_ms;
            unlock();
            const bool pwrkey_allowed =
                last_pwrkey_ms == 0U ||
                now_ms >= last_pwrkey_ms + kMinPwrkeyIntervalMs;
            if (pwrkey_allowed) {
                if (!pwrkey_escalation_logged_) {
                    pwrkey_escalation_logged_ = true;
                    ESP_LOGE(
                        kTag,
                        "Cellular escalation: hard retry -> PWRKEY after %" PRIu64
                        " ms of continuous failure",
                        failure_elapsed_ms);
                }

                if (doHardwareReset()) {
                    ++pwrkey_cycles_in_failure_window_;
                    resetTaskWatchdog();
                    continue;
                }
            }
        }

        std::uint32_t backoff_ms = computeBackoffMs(consecutive_failures);
        std::uint64_t last_pwrkey_ms = 0U;
        lock();
        last_pwrkey_ms = state_.last_pwrkey_uptime_ms;
        unlock();
        if (failure_elapsed_ms >= kPwrkeyAfterMs && last_pwrkey_ms != 0U) {
            const std::uint64_t next_allowed_pwrkey_ms =
                last_pwrkey_ms + kMinPwrkeyIntervalMs;
            if (next_allowed_pwrkey_ms > now_ms) {
                const std::uint32_t pwrkey_wait_ms =
                    static_cast<std::uint32_t>(next_allowed_pwrkey_ms - now_ms);
                backoff_ms = std::min(backoff_ms, pwrkey_wait_ms);
            }
        }

        lock();
        state_.next_reconnect_uptime_ms = now_ms + backoff_ms;
        unlock();

        ESP_LOGI(kTag, "Cellular backoff %" PRIu32 " ms", backoff_ms);

        setModemSleepPin(config_.sleep_gpio, true);
        wdtFeedingDelay(backoff_ms);
        setModemSleepPin(config_.sleep_gpio, false);
    }
}

// ---------------------------------------------------------------------------
// PPP session lifecycle
// ---------------------------------------------------------------------------

bool CellularManager::attemptConnect() {
    lock();
    state_.last_error.clear();
    unlock();

    // --- 1. PPP netif -------------------------------------------------------
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_PPP();
    ppp_netif_ = esp_netif_new(&netif_cfg);
    if (ppp_netif_ == nullptr) {
        lock();
        state_.last_error = "netif alloc failed";
        unlock();
        return false;
    }

    // --- 2. DTE config ------------------------------------------------------
    esp_modem_dte_config_t dte_cfg = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_cfg.uart_config.port_num   = static_cast<uart_port_t>(config_.uart_port);
    dte_cfg.uart_config.baud_rate  = static_cast<int>(config_.uart_baud);
    dte_cfg.uart_config.tx_io_num  = config_.uart_tx_gpio;
    dte_cfg.uart_config.rx_io_num  = config_.uart_rx_gpio;
    dte_cfg.uart_config.rts_io_num = -1;  // no hardware flow control
    dte_cfg.uart_config.cts_io_num = -1;
    dte_cfg.uart_config.rx_buffer_size = 4096;

    // --- 3. DCE config and device creation ----------------------------------
    esp_modem_dce_config_t dce_cfg = ESP_MODEM_DCE_DEFAULT_CONFIG(config_.apn);

    dce_ = esp_modem_new_dev(
        ESP_MODEM_DCE_SIM7600, &dte_cfg, &dce_cfg,
        static_cast<esp_netif_t*>(ppp_netif_));
    if (dce_ == nullptr) {
        lock();
        state_.last_error = "DCE creation failed";
        unlock();
        teardownModem();
        return false;
    }

    // --- 4. Event group and event handlers ----------------------------------
    xEventGroupClearBits(ppp_event_group_, kGotIpBit | kLostIpBit);

    {
        esp_event_handler_instance_t h = nullptr;
        const esp_err_t err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_PPP_GOT_IP, onGotIpEvent, this, &h);
        if (err != ESP_OK) {
            lock();
            state_.last_error = std::string("PPP GOT_IP handler registration failed: ") +
                                esp_err_to_name(err);
            unlock();
            teardownModem();
            return false;
        }
        ip_got_handler_ = h;
    }
    {
        esp_event_handler_instance_t h = nullptr;
        const esp_err_t err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_PPP_LOST_IP, onLostIpEvent, this, &h);
        if (err != ESP_OK) {
            lock();
            state_.last_error = std::string("PPP LOST_IP handler registration failed: ") +
                                esp_err_to_name(err);
            unlock();
            teardownModem();
            return false;
        }
        ip_lost_handler_ = h;
    }

    auto* dce = static_cast<esp_modem_dce_t*>(dce_);

    // --- 5. SIM PIN unlock --------------------------------------------------
    if (config_.sim_pin[0] != '\0') {
        bool pin_needed = false;
        if (esp_modem_read_pin(dce, pin_needed) == ESP_OK && pin_needed) {
            ESP_LOGI(kTag, "Unlocking SIM PIN");
            if (esp_modem_set_pin(dce, std::string(config_.sim_pin)) != ESP_OK) {
                lock();
                state_.last_error = "SIM PIN unlock failed";
                unlock();
                teardownModem();
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(3000U));
            resetTaskWatchdog();
        }
    }

    // --- 6. Wait for network registration -----------------------------------
    if (!waitForNetworkRegistration(dce)) {
        teardownModem();
        return false;
    }

    // --- 7. PPP authentication (username/password) --------------------------
    // Requires CONFIG_LWIP_PPP_PAP_SUPPORT=y in sdkconfig when a password is set.
    if (config_.username[0] != '\0') {
        esp_netif_ppp_set_auth(
            static_cast<esp_netif_t*>(ppp_netif_),
            NETIF_PPP_AUTHTYPE_PAP,
            config_.username,
            config_.password);
    }

    // --- 8. Enter PPP / data mode -------------------------------------------
    if (esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA) != ESP_OK) {
        lock();
        state_.last_error = "Failed to enter PPP data mode";
        unlock();
        teardownModem();
        return false;
    }

    // --- 9. Wait for IP assignment ------------------------------------------
    EventBits_t bits = waitEventBitsWithWatchdog(
        ppp_event_group_,
        kGotIpBit | kLostIpBit,
        pdTRUE, pdFALSE,
        kPppIpTimeoutMs);

    if (!(bits & kGotIpBit)) {
        lock();
        state_.last_error = "PPP IP assignment timeout";
        unlock();
        // Return to command mode so the UART DTE can be destroyed cleanly.
        esp_modem_set_mode(dce, ESP_MODEM_MODE_COMMAND);
        teardownModem();
        return false;
    }

    // --- 10. PPP is up ------------------------------------------------------
    // Use PPP netif as default so SNTP resolves through the cellular link.
    esp_netif_set_default_netif(static_cast<esp_netif_t*>(ppp_netif_));

    onPppConnected(pending_ip_, config_.connectivity_check_host);

    // --- 11. Monitor PPP until it drops or stops passing traffic ------------
    std::uint8_t consecutive_probe_failures = 0U;
    for (;;) {
        bits = waitEventBitsWithWatchdog(
            ppp_event_group_, kLostIpBit, pdTRUE, pdFALSE, kPppMonitorWaitMs);
        if ((bits & kLostIpBit) != 0U) {
            break;
        }

        if (probeLink()) {
            consecutive_probe_failures = 0U;
            continue;
        }

        consecutive_probe_failures++;
        ESP_LOGW(kTag, "PPP liveness probe failed (%u/%u)",
                 consecutive_probe_failures, kPppProbeFailureThreshold);
        if (consecutive_probe_failures >= kPppProbeFailureThreshold) {
            forceDisconnect("PPP liveness probe failed");
            break;
        }
    }

    // onPppDisconnected() was already called by the lost-IP handler or by
    // forceDisconnect() after liveness probe failure.
    // Restore WiFi station as default netif if it is available.
    {
        esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta != nullptr) {
            esp_netif_set_default_netif(sta);
        }
    }

    teardownModem();
    return true;
}

void CellularManager::teardownModem() {
    // Unregister event handlers first so no callbacks fire during teardown.
    if (ip_got_handler_ != nullptr) {
        esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_PPP_GOT_IP,
            static_cast<esp_event_handler_instance_t>(ip_got_handler_));
        ip_got_handler_ = nullptr;
    }
    if (ip_lost_handler_ != nullptr) {
        esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_PPP_LOST_IP,
            static_cast<esp_event_handler_instance_t>(ip_lost_handler_));
        ip_lost_handler_ = nullptr;
    }
    if (dce_ != nullptr) {
        // Best-effort: try to exit data mode before destroying the DTE/DCE.
        esp_modem_set_mode(static_cast<esp_modem_dce_t*>(dce_), ESP_MODEM_MODE_COMMAND);
        vTaskDelay(pdMS_TO_TICKS(500U));
        resetTaskWatchdog();
        esp_modem_destroy(static_cast<esp_modem_dce_t*>(dce_));
        dce_ = nullptr;
    }
    if (ppp_netif_ != nullptr) {
        esp_netif_destroy(static_cast<esp_netif_t*>(ppp_netif_));
        ppp_netif_ = nullptr;
    }

    // Clear transient registration state so it is re-evaluated on next attempt.
    lock();
    state_.registered = false;
    state_.rssi_dbm = 0;
    unlock();
}

bool CellularManager::waitForNetworkRegistration(void* dce_handle) {
    auto* dce = static_cast<esp_modem_dce_t*>(dce_handle);
    bool saw_searching = false;

    for (std::uint32_t waited = 0U;; waited += kRegPollMs) {
        int registration_state = -1;
        const esp_err_t reg_err =
            esp_modem_get_network_registration_state(dce, registration_state);
        if (reg_err == ESP_OK) {
            if (registrationStateIsRegistered(registration_state)) {
                int rssi = 99;
                int ber = 0;
                int rssi_dbm = 0;
                if (esp_modem_get_signal_quality(dce, rssi, ber) == ESP_OK && rssi != 99) {
                    rssi_dbm = (rssi == 0) ? -113 : (-113 + 2 * rssi);
                }

                lock();
                state_.rssi_dbm = rssi_dbm;
                state_.registered = true;
                state_.last_error.clear();
                unlock();
                ESP_LOGI(
                    kTag,
                    "Network registered (state %d), RSSI %d dBm",
                    registration_state,
                    rssi_dbm);
                return true;
            }

            if (registration_state == 2) {
                if (!saw_searching) {
                    ESP_LOGI(kTag, "Network registration searching; polling without escalation");
                    saw_searching = true;
                }
                lock();
                state_.registered = false;
                state_.last_error = "Network registration searching";
                unlock();
                wdtFeedingDelay(kRegPollMs);
                continue;
            }

            if (registration_state == 3) {
                lock();
                state_.registered = false;
                state_.last_error = "Network registration denied";
                unlock();
                ESP_LOGW(kTag, "Network registration denied");
                return false;
            }
        }

        if (reg_err != ESP_OK) {
            int rssi = 99;
            int ber = 0;
            if (esp_modem_get_signal_quality(dce, rssi, ber) == ESP_OK && rssi != 99) {
                const int rssi_dbm = (rssi == 0) ? -113 : (-113 + 2 * rssi);
                lock();
                state_.rssi_dbm = rssi_dbm;
                state_.registered = true;
                state_.last_error.clear();
                unlock();
                ESP_LOGI(kTag, "Network registered by CSQ fallback, RSSI %d dBm", rssi_dbm);
                return true;
            }
        }

        if (waited >= kRegMaxWaitMs) {
            lock();
            state_.registered = false;
            state_.last_error = "Network registration timeout";
            unlock();
            return false;
        }

        wdtFeedingDelay(kRegPollMs);
    }
}

// ---------------------------------------------------------------------------
// Static event handlers
// ---------------------------------------------------------------------------

// static
void CellularManager::onGotIpEvent(
    void* arg, esp_event_base_t /*base*/, int32_t /*id*/, void* event_data) {
    auto* self = static_cast<CellularManager*>(arg);
    if (self == nullptr || self->ppp_event_group_ == nullptr) {
        return;
    }

    const auto* ev = static_cast<ip_event_got_ip_t*>(event_data);

    esp_ip4addr_ntoa(&ev->ip_info.ip, self->pending_ip_, sizeof(self->pending_ip_));

    xEventGroupSetBits(self->ppp_event_group_, kGotIpBit);
}

// static
void CellularManager::onLostIpEvent(
    void* arg, esp_event_base_t /*base*/, int32_t /*id*/, void* /*event_data*/) {
    auto* self = static_cast<CellularManager*>(arg);
    if (self == nullptr || self->ppp_event_group_ == nullptr) {
        return;
    }

    // Notify NetworkManager immediately so uplinkStatus() reflects the loss.
    self->onPppDisconnected("PPP link lost");

    xEventGroupSetBits(self->ppp_event_group_, kLostIpBit);
}

// ---------------------------------------------------------------------------
// Hardware reset
// ---------------------------------------------------------------------------

bool CellularManager::doHardwareReset() {
    ESP_LOGI(kTag, "HW reset: power-off pulse (%" PRIu32 " ms)", kPwrkeyPowerOffMs);
    const bool power_off_pulsed = pulseModemPwrkey(config_.pwrkey_gpio, kPwrkeyPowerOffMs);
    if (!power_off_pulsed) {
        ESP_LOGW(kTag, "HW reset skipped: PWRKEY GPIO is not wired");
        return false;
    }
    resetTaskWatchdog();
    wdtFeedingDelay(kModemShutdownWaitMs);

    ESP_LOGI(kTag, "HW reset: power-on pulse (%" PRIu32 " ms)", kPwrkeyPowerOnMs);
    const bool power_on_pulsed = pulseModemPwrkey(config_.pwrkey_gpio, kPwrkeyPowerOnMs);
    if (!power_on_pulsed) {
        ESP_LOGW(kTag, "HW reset power-on pulse skipped: PWRKEY GPIO is not wired");
        return false;
    }
    resetTaskWatchdog();
    wdtFeedingDelay(kModemBootWaitMs);

    lock();
    ++state_.pwrkey_cycles_total;
    state_.last_pwrkey_uptime_ms = uptimeMilliseconds();
    state_.next_reconnect_uptime_ms = 0U;
    unlock();

    ESP_LOGI(kTag, "HW reset complete");
    return true;
}

// ---------------------------------------------------------------------------
// Backoff
// ---------------------------------------------------------------------------

// static
std::uint32_t CellularManager::computeBackoffMs(std::uint32_t attempt) {
    constexpr std::uint32_t kBackoffTableMs[] = {
        10000U,
        30000U,
        60000U,
        120000U,
        300000U,
        600000U,
        900000U,
    };
    if (attempt == 0U) {
        return kBackoffTableMs[0];
    }
    constexpr std::size_t kBackoffTableSize =
        sizeof(kBackoffTableMs) / sizeof(kBackoffTableMs[0]);
    const std::size_t index =
        std::min<std::size_t>(attempt - 1U, kBackoffTableSize - 1U);
    return kBackoffTableMs[index];
}

void CellularManager::resetFailureWindow() {
    failure_window_start_ms_ = 0U;
    pwrkey_cycles_in_failure_window_ = 0U;
    hard_retry_logged_ = false;
    pwrkey_escalation_logged_ = false;

    lock();
    state_.reconnect_attempts = 0U;
    state_.consecutive_failures = 0U;
    state_.next_reconnect_uptime_ms = 0U;
    unlock();
}

// ---------------------------------------------------------------------------
// PPP session callbacks
// ---------------------------------------------------------------------------

void CellularManager::onPppConnected(const char* ip_address, const char* check_host) {
    const std::string ip = (ip_address != nullptr) ? ip_address : "";
    failure_window_start_ms_ = 0U;
    pwrkey_cycles_in_failure_window_ = 0U;
    hard_retry_logged_ = false;
    pwrkey_escalation_logged_ = false;
    lock();
    state_.ppp_connected = true;
    state_.ip_address = ip;
    state_.last_error.clear();
    state_.reconnect_attempts = 0U;
    state_.consecutive_failures = 0U;
    state_.next_reconnect_uptime_ms = 0U;
    unlock();

    if (network_manager_ != nullptr) {
        network_manager_->setCellularStatus(true, ip_address);
    }

    ESP_LOGI(kTag, "PPP connected, IP: %s", ip.c_str());

    const ConnectivityCheckResult result =
        runConnectivityCheck(check_host, kCheckTimeoutMs, kCheckRetries);

    switch (result) {
        case ConnectivityCheckResult::kSkipped:
            lock();
            state_.connectivity_check_skipped = true;
            state_.connectivity_ok = false;
            unlock();
            ESP_LOGI(kTag, "Connectivity check skipped (no host configured)");
            break;
        case ConnectivityCheckResult::kOk:
            lock();
            state_.connectivity_ok = true;
            state_.connectivity_check_skipped = false;
            unlock();
            ESP_LOGI(kTag, "Connectivity check OK (%s)", check_host);
            break;
        case ConnectivityCheckResult::kFailed:
            lock();
            state_.connectivity_ok = false;
            state_.connectivity_check_skipped = false;
            unlock();
            ESP_LOGW(kTag, "Connectivity check failed (%s)",
                     (check_host != nullptr) ? check_host : "");
            break;
    }
}

bool CellularManager::probeLink() {
    const ConnectivityCheckResult result =
        runConnectivityCheck(config_.connectivity_check_host,
                             kPppProbeTimeoutMs,
                             kPppProbeRetries);

    switch (result) {
        case ConnectivityCheckResult::kSkipped:
            lock();
            state_.connectivity_check_skipped = true;
            state_.connectivity_ok = false;
            unlock();
            ESP_LOGD(kTag, "PPP liveness probe skipped (no host configured)");
            return true;
        case ConnectivityCheckResult::kOk:
            lock();
            state_.connectivity_check_skipped = false;
            state_.connectivity_ok = true;
            unlock();
            ESP_LOGD(kTag, "PPP liveness probe OK (%s)",
                     config_.connectivity_check_host);
            return true;
        case ConnectivityCheckResult::kFailed:
            lock();
            state_.connectivity_check_skipped = false;
            state_.connectivity_ok = false;
            unlock();
            return false;
    }

    return false;
}

void CellularManager::forceDisconnect(const char* reason) {
    ESP_LOGW(kTag, "Forcing PPP disconnect: %s",
             (reason != nullptr && reason[0] != '\0') ? reason : "unknown reason");

    if (dce_ != nullptr) {
        const esp_err_t err =
            esp_modem_set_mode(static_cast<esp_modem_dce_t*>(dce_),
                               ESP_MODEM_MODE_COMMAND);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to return modem to command mode: %s",
                     esp_err_to_name(err));
        }
    }

    if (ppp_netif_ != nullptr) {
        esp_netif_action_disconnected(
            ppp_netif_, IP_EVENT, IP_EVENT_PPP_LOST_IP, nullptr);
    }

    onPppDisconnected(reason);

    if (ppp_event_group_ != nullptr) {
        xEventGroupSetBits(ppp_event_group_, kLostIpBit);
    }
}

void CellularManager::onPppDisconnected(const char* reason) {
    std::string reason_text;
    lock();
    state_.ppp_connected = false;
    state_.connectivity_ok = false;
    state_.connectivity_check_skipped = false;
    state_.ip_address.clear();

    if (reason != nullptr && reason[0] != '\0') {
        state_.last_error = reason;
        reason_text = reason;
    } else {
        state_.last_error.clear();
    }
    unlock();

    if (network_manager_ != nullptr) {
        network_manager_->setCellularStatus(false, nullptr);
    }

    if (!reason_text.empty()) {
        ESP_LOGW(kTag, "PPP disconnected: %s", reason_text.c_str());
    } else {
        ESP_LOGI(kTag, "PPP disconnected");
    }
}

}  // namespace air360
