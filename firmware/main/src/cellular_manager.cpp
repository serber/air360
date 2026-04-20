#include "air360/cellular_manager.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "air360/connectivity_checker.hpp"
#include "air360/modem_gpio.hpp"
#include "air360/network_manager.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
// esp_modem_api.h (C API) uses PdpContext from the C++ layer — pull it in first.
#include "cxx_include/esp_modem_types.hpp"
using namespace esp_modem;  // NOLINT(google-build-using-namespace)
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
// Maximum sleep slice between watchdog resets during backoff waits.
constexpr std::uint32_t kWdtFeedSliceMs = 5000U;

// Sleep for total_ms while feeding the task watchdog every kWdtFeedSliceMs.
// Must only be called from a task subscribed to the TWDT.
void wdtFeedingDelay(std::uint32_t total_ms) {
    while (total_ms > 0U) {
        const std::uint32_t slice = std::min(total_ms, kWdtFeedSliceMs);
        vTaskDelay(pdMS_TO_TICKS(slice));
        esp_task_wdt_reset();
        total_ms -= slice;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

CellularManager::CellularManager() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}

void CellularManager::lock() const {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
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
    esp_task_wdt_add(nullptr);
    ESP_LOGI(kTag, "TWDT: air360_cellular subscribed");

    for (;;) {
        // Ensure the modem is awake before connecting.
        setModemSleepPin(config_.sleep_gpio, false);

        // attemptConnect() blocks until the session is fully over
        // (either never established, or established then dropped).
        const bool was_connected = attemptConnect();
        esp_task_wdt_reset();

        if (was_connected) {
            // Clean disconnect — reset backoff counter for next cycle.
            lock();
            state_.reconnect_attempts = 0U;
            state_.next_reconnect_uptime_ms = 0U;
            unlock();
            continue;
        }

        // Setup failure — apply backoff.
        std::uint32_t reconnect_attempts = 0U;
        lock();
        state_.reconnect_attempts++;
        reconnect_attempts = state_.reconnect_attempts;
        unlock();

        ESP_LOGW(kTag, "Attempt %" PRIu32 " failed", reconnect_attempts);

        if (reconnect_attempts >= kMaxReconnectAttempts) {
            ESP_LOGW(kTag, "Max attempts reached — hardware reset");
            doHardwareReset();
            esp_task_wdt_reset();
            lock();
            state_.reconnect_attempts = 0U;
            state_.next_reconnect_uptime_ms = 0U;
            unlock();
            // doHardwareReset() already waits for modem boot — go straight to
            // next attempt without an extra backoff sleep.
            continue;
        }

        const std::uint32_t backoff_ms = computeBackoffMs(reconnect_attempts);
        const std::uint64_t now_ms =
            static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL;
        lock();
        state_.next_reconnect_uptime_ms = now_ms + backoff_ms;
        unlock();

        ESP_LOGI(kTag, "Backoff %" PRIu32 " ms", backoff_ms);

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
    ppp_event_group_ = xEventGroupCreate();
    if (ppp_event_group_ == nullptr) {
        lock();
        state_.last_error = "event group alloc failed";
        unlock();
        teardownModem();
        return false;
    }

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
        }
    }

    // --- 6. Wait for network registration -----------------------------------
    // Poll signal quality; rssi == 99 means "no signal / not registered".
    {
        bool registered = false;
        for (std::uint32_t waited = 0U; waited < kRegMaxWaitMs; waited += kRegPollMs) {
            int rssi = 99;
            int ber  = 0;
            if (esp_modem_get_signal_quality(dce, rssi, ber) == ESP_OK && rssi != 99) {
                // Convert AT+CSQ rssi value to dBm: 0 = -113 dBm, step 2 dBm
                const int rssi_dbm = (rssi == 0) ? -113 : (-113 + 2 * rssi);
                lock();
                state_.rssi_dbm = rssi_dbm;
                state_.registered = true;
                unlock();
                registered = true;
                ESP_LOGI(kTag, "Network registered, RSSI %d dBm", rssi_dbm);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kRegPollMs));
        }
        if (!registered) {
            lock();
            state_.last_error = "Network registration timeout";
            unlock();
            teardownModem();
            return false;
        }
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
    EventBits_t bits = xEventGroupWaitBits(
        ppp_event_group_,
        kGotIpBit | kLostIpBit,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(kPppIpTimeoutMs));

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

    // --- 11. Block until PPP drops ------------------------------------------
    xEventGroupWaitBits(
        ppp_event_group_, kLostIpBit, pdTRUE, pdFALSE, portMAX_DELAY);

    // onPppDisconnected() was already called by the onLostIpEvent handler.
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
    if (ppp_event_group_ != nullptr) {
        vEventGroupDelete(ppp_event_group_);
        ppp_event_group_ = nullptr;
    }
    if (dce_ != nullptr) {
        // Best-effort: try to exit data mode before destroying the DTE/DCE.
        esp_modem_set_mode(static_cast<esp_modem_dce_t*>(dce_), ESP_MODEM_MODE_COMMAND);
        vTaskDelay(pdMS_TO_TICKS(500U));
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

void CellularManager::doHardwareReset() {
    ESP_LOGI(kTag, "HW reset: power-off pulse (%" PRIu32 " ms)", kPwrkeyPowerOffMs);
    pulseModemPwrkey(config_.pwrkey_gpio, kPwrkeyPowerOffMs);
    vTaskDelay(pdMS_TO_TICKS(kModemShutdownWaitMs));

    ESP_LOGI(kTag, "HW reset: power-on pulse (%" PRIu32 " ms)", kPwrkeyPowerOnMs);
    pulseModemPwrkey(config_.pwrkey_gpio, kPwrkeyPowerOnMs);
    vTaskDelay(pdMS_TO_TICKS(kModemBootWaitMs));

    ESP_LOGI(kTag, "HW reset complete");
}

// ---------------------------------------------------------------------------
// Backoff
// ---------------------------------------------------------------------------

// static
std::uint32_t CellularManager::computeBackoffMs(std::uint32_t attempt) {
    std::uint32_t backoff = kBaseBackoffMs;
    for (std::uint32_t i = 0U; i < attempt && backoff < kMaxBackoffMs; ++i) {
        backoff = (backoff <= kMaxBackoffMs / 2U) ? backoff * 2U : kMaxBackoffMs;
    }
    return (backoff < kMaxBackoffMs) ? backoff : kMaxBackoffMs;
}

// ---------------------------------------------------------------------------
// PPP session callbacks
// ---------------------------------------------------------------------------

void CellularManager::onPppConnected(const char* ip_address, const char* check_host) {
    const std::string ip = (ip_address != nullptr) ? ip_address : "";
    lock();
    state_.ppp_connected = true;
    state_.ip_address = ip;
    state_.last_error.clear();
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
