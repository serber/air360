#include "air360/network_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "air360/string_utils.hpp"
#include "air360/time_utils.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_sntp.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/ip4_addr.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.net";
constexpr EventBits_t kStationConnectedBit = BIT0;
constexpr EventBits_t kStationFailedBit = BIT1;
constexpr char kDefaultSntpServer[] = "pool.ntp.org";
constexpr std::uint32_t kSntpPollIntervalMs = 250U;
constexpr std::uint32_t kStationWaitSliceMs = 250U;
constexpr std::uint32_t kReconnectBaseDelayMs = 10000U;
constexpr std::uint32_t kReconnectMaxDelayMs = 300000U;
constexpr std::uint32_t kSetupApRetryDelayMs = 180000U;
constexpr std::uint32_t kDisconnectIgnoreWindowMs = 2000U;
constexpr std::uint32_t kDefaultConnectTimeoutMs = 15000U;
constexpr std::size_t kNetworkWorkerTaskStackSize = 6144U;
constexpr UBaseType_t kNetworkWorkerTaskPriority = tskIDLE_PRIORITY + 2U;
constexpr TickType_t kNetworkWorkerWait = pdMS_TO_TICKS(5000U);
constexpr TickType_t kScanRequestTimeout = pdMS_TO_TICKS(20000U);
constexpr std::uint32_t kWorkerReconnectReq = (1UL << 0);
constexpr std::uint32_t kWorkerSetupApRetryReq = (1UL << 1);
constexpr std::uint32_t kWorkerScanReq = (1UL << 2);
constexpr std::uint32_t kAllWorkerReqBits = std::numeric_limits<std::uint32_t>::max();

TickType_t ticksFromMs(std::uint32_t value_ms) {
    return pdMS_TO_TICKS(value_ms == 0U ? 1U : value_ms);
}

void stopTimerIfRunning(TimerHandle_t timer) {
    if (timer != nullptr) {
        xTimerStop(timer, 0U);
    }
}

void armTimer(TimerHandle_t timer, std::uint32_t delay_ms) {
    if (timer == nullptr) {
        return;
    }

    xTimerStop(timer, 0U);
    xTimerChangePeriod(timer, ticksFromMs(delay_ms), 0U);
    xTimerStart(timer, 0U);
}

bool hasStationConfig(const DeviceConfig& config) {
    return config.wifi_sta_ssid[0] != '\0';
}

std::string stationHostname(const DeviceConfig& config) {
    std::string hostname;
    hostname.reserve(sizeof(config.device_name));

    for (std::size_t index = 0; index < sizeof(config.device_name); ++index) {
        const unsigned char raw = static_cast<unsigned char>(config.device_name[index]);
        if (raw == '\0') {
            break;
        }

        if (std::isalnum(raw)) {
            hostname.push_back(static_cast<char>(std::tolower(raw)));
            continue;
        }

        if (raw == '-' || raw == '_') {
            hostname.push_back(static_cast<char>(raw));
            continue;
        }

        if (!hostname.empty() && hostname.back() != '-') {
            hostname.push_back('-');
        }
    }

    while (!hostname.empty() && hostname.back() == '-') {
        hostname.pop_back();
    }

    if (hostname.empty()) {
        hostname = "air360";
    }

    return hostname;
}

const char* disconnectReasonLabel(std::int32_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
            return "auth_expired";
        case WIFI_REASON_AUTH_FAIL:
            return "auth_failed";
        case WIFI_REASON_ASSOC_FAIL:
            return "association_failed";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "handshake_timeout";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "beacon_timeout";
        case WIFI_REASON_TIMEOUT:
            return "timeout";
        case WIFI_REASON_NO_AP_FOUND:
            return "ap_not_found";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            return "ap_security_incompatible";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "ap_authmode_threshold";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "ap_rssi_threshold";
        case WIFI_REASON_CONNECTION_FAIL:
            return "connection_failed";
        case WIFI_REASON_ASSOC_LEAVE:
            return "assoc_leave";
        case WIFI_REASON_ROAMING:
            return "roaming";
        default:
            return "unknown";
    }
}

std::string disconnectSummary(std::int32_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "Wi-Fi authentication failed";
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "Wi-Fi AP not found";
        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_TIMEOUT:
            return "Wi-Fi connection timed out";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_CONNECTION_FAIL:
            return "Wi-Fi association failed";
        default:
            return "Station disconnected";
    }
}

void setStateError(NetworkState& state, const char* error) {
    state.last_error = error == nullptr ? "" : error;
}

void resetCurrentTaskWatchdogIfSubscribed() {
    if (esp_task_wdt_status(nullptr) == ESP_OK) {
        esp_task_wdt_reset();
    }
}

EventBits_t waitForStationResult(EventGroupHandle_t station_events, std::uint32_t timeout_ms) {
    if (station_events == nullptr) {
        return 0U;
    }

    EventBits_t bits = 0U;
    const std::int64_t started_ms = uptimeMilliseconds();
    while ((uptimeMilliseconds() - started_ms) < timeout_ms) {
        const std::uint32_t elapsed_ms =
            static_cast<std::uint32_t>(uptimeMilliseconds() - started_ms);
        const std::uint32_t remaining_ms = timeout_ms - elapsed_ms;
        const std::uint32_t wait_ms = std::min(kStationWaitSliceMs, remaining_ms);

        bits = xEventGroupWaitBits(
            station_events,
            kStationConnectedBit | kStationFailedBit,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(wait_ms));
        if ((bits & (kStationConnectedBit | kStationFailedBit)) != 0U) {
            return bits;
        }

        resetCurrentTaskWatchdogIfSubscribed();
    }

    return bits;
}

std::uint32_t reconnectDelayMs(std::uint32_t attempt_count) {
    std::uint64_t delay_ms = kReconnectBaseDelayMs;
    for (std::uint32_t attempt = 1U; attempt < attempt_count; ++attempt) {
        delay_ms *= 2ULL;
        if (delay_ms >= kReconnectMaxDelayMs) {
            return kReconnectMaxDelayMs;
        }
    }

    return static_cast<std::uint32_t>(delay_ms);
}

}  // namespace

NetworkManager::NetworkManager() : last_config_(makeDefaultDeviceConfig()) {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}

void NetworkManager::lock() const {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
}

void NetworkManager::unlock() const {
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void NetworkManager::startMdns(const std::string& hostname) {
    if (runtime_.mdns_initialized) {
        return;
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "mDNS init failed: %s", esp_err_to_name(err));
        return;
    }

    err = mdns_hostname_set(hostname.c_str());
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "mDNS hostname set failed: %s", esp_err_to_name(err));
        mdns_free();
        return;
    }

    err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "mDNS service add failed: %s", esp_err_to_name(err));
    }

    runtime_.mdns_initialized = true;
    ESP_LOGI(kTag, "mDNS started: %s.local", hostname.c_str());
}

esp_err_t NetworkManager::ensureWifiInit() {
    RuntimeContext& context = runtime_;

    if (context.station_events == nullptr) {
        context.station_events = xEventGroupCreateStatic(&context.station_events_buf);
    }

    if (context.reconnect_timer == nullptr) {
        context.reconnect_timer = xTimerCreate(
            "wifi_rc",
            ticksFromMs(kReconnectBaseDelayMs),
            pdFALSE,
            this,
            &NetworkManager::reconnectTimerCallback);
        if (context.reconnect_timer == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (context.setup_ap_retry_timer == nullptr) {
        context.setup_ap_retry_timer = xTimerCreate(
            "wifi_ap_rc",
            ticksFromMs(kSetupApRetryDelayMs),
            pdFALSE,
            this,
            &NetworkManager::setupApRetryTimerCallback);
        if (context.setup_ap_retry_timer == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (context.scan_request_mutex == nullptr) {
        context.scan_request_mutex =
            xSemaphoreCreateMutexStatic(&context.scan_request_mutex_buf);
        if (context.scan_request_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (context.scan_done == nullptr) {
        context.scan_done = xSemaphoreCreateBinaryStatic(&context.scan_done_buf);
        if (context.scan_done == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (context.worker_task == nullptr) {
        const BaseType_t result = xTaskCreate(
            &NetworkManager::workerTask,
            "air360_net",
            kNetworkWorkerTaskStackSize,
            this,
            kNetworkWorkerTaskPriority,
            &context.worker_task);
        if (result != pdPASS) {
            context.worker_task = nullptr;
            return ESP_ERR_NO_MEM;
        }
    }

    if (!context.wifi_initialized) {
        const wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&wifi_init);
        if (err == ESP_OK) {
            context.wifi_initialized = true;
        } else if (err == ESP_ERR_INVALID_STATE) {
            context.wifi_initialized = true;
            err = ESP_OK;
        }

        if (err != ESP_OK) {
            return err;
        }

        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (!context.handlers_registered) {
        esp_err_t err = esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &NetworkManager::handleWifiEvent,
            this,
            &context.wifi_handler);
        if (err != ESP_OK) {
            return err;
        }

        err = esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &NetworkManager::handleIpEvent,
            this,
            &context.ip_handler);
        if (err != ESP_OK) {
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, context.wifi_handler);
            context.wifi_handler = nullptr;
            return err;
        }

        context.handlers_registered = true;
    }

    return ESP_OK;
}

esp_err_t NetworkManager::synchronizeTime(std::uint32_t timeout_ms) {
    RuntimeContext& context = runtime_;
    std::string sntp_server;

    lock();
    state_.time_sync_attempted = true;
    state_.time_synchronized = false;
    state_.time_sync_error.clear();
    state_.last_time_sync_unix_ms = 0;
    if (!state_.station_connected) {
        state_.time_sync_error = "station is not connected";
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    sntp_server = configured_sntp_server_;
    unlock();

    if (hasValidUnixTime()) {
        lock();
        state_.time_synchronized = true;
        state_.last_time_sync_unix_ms = air360::currentUnixMilliseconds();
        unlock();
        ESP_LOGI(kTag, "System time already valid, skipping SNTP wait");
        return ESP_OK;
    }

    esp_err_t err = ESP_OK;
    if (!context.sntp_initialized) {
        const char* server = sntp_server.empty() ? kDefaultSntpServer : sntp_server.c_str();
        esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
        ESP_LOGI(kTag, "Initializing SNTP with server: %s", server);
        err = esp_netif_sntp_init(&sntp_config);
        if (err == ESP_OK) {
            context.sntp_initialized = true;
        } else {
            lock();
            state_.time_sync_error = esp_err_to_name(err);
            unlock();
            ESP_LOGW(kTag, "SNTP init failed: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        err = esp_netif_sntp_start();
        if (err != ESP_OK) {
            lock();
            state_.time_sync_error = esp_err_to_name(err);
            unlock();
            ESP_LOGW(kTag, "SNTP start failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(kTag, "Waiting for SNTP time sync");
    const std::int64_t started_ms = uptimeMilliseconds();
    bool synchronized = false;
    while ((uptimeMilliseconds() - started_ms) < timeout_ms) {
        if (hasValidUnixTime()) {
            synchronized = true;
            break;
        }
        resetCurrentTaskWatchdogIfSubscribed();
        vTaskDelay(pdMS_TO_TICKS(kSntpPollIntervalMs));
    }

    if (!synchronized || !hasValidUnixTime()) {
        lock();
        state_.time_sync_error = "time is still invalid after SNTP sync";
        const std::string error = state_.time_sync_error;
        unlock();
        ESP_LOGW(kTag, "SNTP sync failed: %s", error.c_str());
        return ESP_ERR_TIMEOUT;
    }

    lock();
    state_.time_synchronized = true;
    state_.time_sync_error.clear();
    state_.last_time_sync_unix_ms = air360::currentUnixMilliseconds();
    unlock();
    ESP_LOGI(kTag, "SNTP time synchronized");
    return ESP_OK;
}

void NetworkManager::handleWifiEvent(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data) {
    if (event_base != WIFI_EVENT) {
        return;
    }

    auto* manager = static_cast<NetworkManager*>(arg);
    if (manager == nullptr) {
        return;
    }
    RuntimeContext& context = manager->runtime_;
    if (context.station_events == nullptr) {
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_SCAN_DONE: {
            const auto* done_event =
                static_cast<const wifi_event_sta_scan_done_t*>(event_data);
            const bool aborted = (done_event == nullptr || done_event->status != 0U);

            if (!aborted) {
                std::uint16_t ap_count = 0U;
                const esp_err_t num_err = esp_wifi_scan_get_ap_num(&ap_count);
                std::vector<WifiNetworkRecord> networks;
                std::string scan_error;

                if (num_err == ESP_OK && ap_count > 0U) {
                    std::vector<wifi_ap_record_t> records(ap_count);
                    std::uint16_t to_fetch = ap_count;
                    const esp_err_t rec_err =
                        esp_wifi_scan_get_ap_records(&to_fetch, records.data());
                    if (rec_err == ESP_OK) {
                        networks.reserve(to_fetch);
                        for (std::uint16_t i = 0U; i < to_fetch; ++i) {
                            const wifi_ap_record_t& rec = records[i];
                            if (rec.ssid[0] == '\0') {
                                continue;
                            }
                            const std::string ssid(
                                reinterpret_cast<const char*>(rec.ssid));
                            bool dup = false;
                            for (const auto& existing : networks) {
                                if (existing.ssid == ssid) {
                                    dup = true;
                                    break;
                                }
                            }
                            if (!dup) {
                                WifiNetworkRecord net;
                                net.ssid = ssid;
                                net.rssi = rec.rssi;
                                net.auth_mode = rec.authmode;
                                networks.push_back(std::move(net));
                            }
                        }
                    } else {
                        scan_error = esp_err_to_name(rec_err);
                    }
                } else if (num_err == ESP_OK) {
                    esp_wifi_clear_ap_list();
                } else {
                    scan_error = esp_err_to_name(num_err);
                }

                manager->lock();
                manager->available_networks_ = std::move(networks);
                manager->last_scan_error_ = std::move(scan_error);
                manager->last_scan_uptime_ms_ = air360::uptimeMilliseconds();
                manager->scan_in_progress_ = false;
                manager->unlock();
            } else {
                manager->lock();
                manager->scan_in_progress_ = false;
                manager->unlock();
            }

            if (context.scan_done != nullptr) {
                xSemaphoreGive(context.scan_done);
            }
            break;
        }

        case WIFI_EVENT_STA_START:
            if (context.auto_connect_on_sta_start) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            const auto* disconnected =
                static_cast<const wifi_event_sta_disconnected_t*>(event_data);
            const std::uint64_t now_ms = air360::uptimeMilliseconds();
            const std::int32_t reason =
                disconnected != nullptr ? static_cast<std::int32_t>(disconnected->reason) : -1;
            const bool ignore_disconnect = now_ms <= context.ignore_disconnect_until_ms;

            bool schedule_reconnect = false;
            bool schedule_setup_ap_retry = false;
            std::uint32_t reconnect_delay_ms = 0U;

            manager->lock();
            const bool was_connected = manager->state_.station_connected;
            const bool setup_ap_active =
                manager->state_.lab_ap_active || manager->state_.mode == NetworkMode::kSetupAp;

            manager->state_.station_connected = false;
            manager->state_.ip_address.clear();
            manager->state_.last_disconnect_reason = reason;
            manager->state_.last_disconnect_reason_label = disconnectReasonLabel(reason);

            if (!ignore_disconnect) {
                if (setup_ap_active) {
                    manager->state_.mode = NetworkMode::kSetupAp;
                    if (manager->state_.station_config_present) {
                        manager->state_.setup_ap_retry_active = true;
                        manager->state_.next_setup_ap_retry_uptime_ms =
                            now_ms + kSetupApRetryDelayMs;
                        schedule_setup_ap_retry = true;
                    }
                } else {
                    manager->state_.mode = NetworkMode::kOffline;
                    if (manager->state_.station_config_present &&
                        (was_connected || context.reconnect_cycle_active)) {
                        context.reconnect_cycle_active = true;
                        manager->state_.reconnect_attempt_count += 1U;
                        reconnect_delay_ms =
                            reconnectDelayMs(manager->state_.reconnect_attempt_count);
                        manager->state_.reconnect_backoff_active = true;
                        manager->state_.next_reconnect_uptime_ms = now_ms + reconnect_delay_ms;
                        manager->state_.setup_ap_retry_active = false;
                        manager->state_.next_setup_ap_retry_uptime_ms = 0U;
                        schedule_reconnect = true;
                    } else {
                        manager->state_.reconnect_backoff_active = false;
                        manager->state_.next_reconnect_uptime_ms = 0U;
                    }
                }

                char error_buffer[160];
                std::snprintf(
                    error_buffer,
                    sizeof(error_buffer),
                    "%s (%s, reason=%" PRId32 ")",
                    disconnectSummary(reason).c_str(),
                    disconnectReasonLabel(reason),
                    reason);
                manager->state_.last_error = error_buffer;
            }
            manager->unlock();

            xEventGroupSetBits(context.station_events, kStationFailedBit);

            if (ignore_disconnect) {
                break;
            }

            if (schedule_reconnect) {
                armTimer(context.reconnect_timer, reconnect_delay_ms);
                ESP_LOGW(
                    kTag,
                    "Station disconnected: %s (%" PRId32 "), reconnect attempt %lu in %" PRIu32 " ms",
                    disconnectReasonLabel(reason),
                    reason,
                    static_cast<unsigned long>(manager->state().reconnect_attempt_count),
                    reconnect_delay_ms);
            } else if (schedule_setup_ap_retry) {
                armTimer(context.setup_ap_retry_timer, kSetupApRetryDelayMs);
                ESP_LOGW(
                    kTag,
                    "Station unavailable while setup AP is active: %s (%" PRId32 "), retry in %" PRIu32 " ms",
                    disconnectReasonLabel(reason),
                    reason,
                    kSetupApRetryDelayMs);
            }
            break;
        }

        default:
            break;
    }
}

void NetworkManager::handleIpEvent(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data) {
    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    auto* manager = static_cast<NetworkManager*>(arg);
    if (manager == nullptr) {
        return;
    }
    RuntimeContext& context = manager->runtime_;
    if (context.station_events == nullptr) {
        return;
    }

    const auto* got_ip = static_cast<const ip_event_got_ip_t*>(event_data);
    char ip_buffer[16] = "";
    if (got_ip != nullptr) {
        std::snprintf(ip_buffer, sizeof(ip_buffer), IPSTR, IP2STR(&got_ip->ip_info.ip));
    }

    stopTimerIfRunning(context.reconnect_timer);
    stopTimerIfRunning(context.setup_ap_retry_timer);
    context.reconnect_cycle_active = false;

    manager->lock();
    manager->state_.ip_address = ip_buffer;
    if (!manager->state_.lab_ap_active) {
        manager->state_.mode = NetworkMode::kStation;
    }
    manager->state_.station_connected = true;
    manager->state_.last_error.clear();
    manager->state_.reconnect_backoff_active = false;
    manager->state_.next_reconnect_uptime_ms = 0U;
    manager->state_.reconnect_attempt_count = 0U;
    manager->state_.setup_ap_retry_active = false;
    manager->state_.next_setup_ap_retry_uptime_ms = 0U;
    manager->unlock();

    xEventGroupSetBits(context.station_events, kStationConnectedBit);
}

void NetworkManager::reconnectTimerCallback(TimerHandle_t timer) {
    auto* manager = static_cast<NetworkManager*>(pvTimerGetTimerID(timer));
    if (manager != nullptr) {
        manager->notifyWorker(kWorkerReconnectReq);
    }
}

void NetworkManager::setupApRetryTimerCallback(TimerHandle_t timer) {
    auto* manager = static_cast<NetworkManager*>(pvTimerGetTimerID(timer));
    if (manager != nullptr) {
        manager->notifyWorker(kWorkerSetupApRetryReq);
    }
}

void NetworkManager::workerTask(void* arg) {
    static_cast<NetworkManager*>(arg)->workerLoop();
}

void NetworkManager::notifyWorker(std::uint32_t request_bits) {
    const TaskHandle_t worker = runtime_.worker_task;
    if (worker == nullptr) {
        return;
    }

    // Worker requests are level-triggered by bits; a missed notify is visible on the next retry.
    static_cast<void>(xTaskNotify(worker, request_bits, eSetBits));
}

void NetworkManager::workerLoop() {
    bool wdt_subscribed = false;
    const esp_err_t wdt_err = esp_task_wdt_add(nullptr);
    if (wdt_err == ESP_OK) {
        wdt_subscribed = true;
        ESP_LOGI(kTag, "TWDT: air360_net subscribed");
    } else {
        ESP_LOGW(kTag, "TWDT subscribe failed: %s", esp_err_to_name(wdt_err));
    }

    for (;;) {
        std::uint32_t bits = 0U;
        // A timeout simply lets the worker feed TWDT and check for the next request.
        static_cast<void>(xTaskNotifyWait(0U, kAllWorkerReqBits, &bits, kNetworkWorkerWait));
        if (wdt_subscribed) {
            // TWDT reset failure is non-actionable inside the subscribed task loop.
            static_cast<void>(esp_task_wdt_reset());
        }

        if ((bits & (kWorkerReconnectReq | kWorkerSetupApRetryReq)) != 0U) {
            DeviceConfig config = makeDefaultDeviceConfig();
            bool has_config = false;
            lock();
            config = last_config_;
            has_config = has_last_config_;
            unlock();

            if (has_config && (bits & kWorkerReconnectReq) != 0U) {
                const esp_err_t reconnect_err = attemptStationConnect(
                    config,
                    kDefaultConnectTimeoutMs,
                    ConnectAttemptKind::kRuntimeReconnect);
                if (reconnect_err != ESP_OK) {
                    ESP_LOGW(
                        kTag,
                        "Runtime station reconnect failed: %s",
                        esp_err_to_name(reconnect_err));
                }
            }

            if (has_config && (bits & kWorkerSetupApRetryReq) != 0U) {
                const esp_err_t setup_retry_err = attemptStationConnect(
                    config,
                    kDefaultConnectTimeoutMs,
                    ConnectAttemptKind::kSetupApRetry);
                if (setup_retry_err != ESP_OK) {
                    ESP_LOGW(
                        kTag,
                        "Setup AP station retry failed: %s",
                        esp_err_to_name(setup_retry_err));
                }
            }
        }

        if ((bits & kWorkerScanReq) != 0U) {
            startAsyncScanAndWait();
        }
    }
}

esp_err_t NetworkManager::attemptStationConnect(
    const DeviceConfig& config,
    std::uint32_t timeout_ms,
    ConnectAttemptKind kind) {
    const bool preserve_ap = (kind == ConnectAttemptKind::kSetupApRetry);
    RuntimeContext& context = runtime_;

    lock();
    state_.station_config_present = hasStationConfig(config);
    state_.station_connect_attempted = true;
    state_.station_ssid = config.wifi_sta_ssid;
    state_.lab_ap_ssid = config.lab_ap_ssid;
    configured_sntp_server_ =
        (config.sntp_server[0] != '\0') ? std::string(config.sntp_server) : std::string();
    last_config_ = config;
    has_last_config_ = true;
    unlock();

    if (!hasStationConfig(config)) {
        lock();
        setStateError(state_, "missing station configuration");
        unlock();
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureWifiInit();
    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    if (context.sta_netif == nullptr) {
        context.sta_netif = esp_netif_create_default_wifi_sta();
        if (context.sta_netif == nullptr) {
            lock();
            setStateError(state_, "failed to create station netif");
            unlock();
            return ESP_FAIL;
        }
    }

    if (preserve_ap && context.ap_netif == nullptr) {
        context.ap_netif = esp_netif_create_default_wifi_ap();
        if (context.ap_netif == nullptr) {
            lock();
            setStateError(state_, "failed to create AP netif");
            unlock();
            return ESP_FAIL;
        }
    }

    const std::string hostname = stationHostname(config);
    err = esp_netif_set_hostname(context.sta_netif, hostname.c_str());
    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    if (config.sta_use_static_ip != 0U && config.sta_ip[0] != '\0') {
        const char* static_ip_error = nullptr;
        if (!validateStaticIpv4Config(config, static_ip_error)) {
            lock();
            setStateError(
                state_,
                static_ip_error == nullptr ? "invalid static IPv4 configuration"
                                           : static_ip_error);
            unlock();
            return ESP_ERR_INVALID_ARG;
        }

        err = esp_netif_dhcpc_stop(context.sta_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            lock();
            setStateError(state_, esp_err_to_name(err));
            unlock();
            return err;
        }

        esp_netif_ip_info_t ip_info{};
        ip4addr_aton(config.sta_ip, reinterpret_cast<ip4_addr_t*>(&ip_info.ip));
        ip4addr_aton(config.sta_netmask, reinterpret_cast<ip4_addr_t*>(&ip_info.netmask));
        ip4addr_aton(config.sta_gateway, reinterpret_cast<ip4_addr_t*>(&ip_info.gw));
        err = esp_netif_set_ip_info(context.sta_netif, &ip_info);
        if (err != ESP_OK) {
            lock();
            setStateError(state_, esp_err_to_name(err));
            unlock();
            return err;
        }

        if (config.sta_dns[0] != '\0') {
            esp_netif_dns_info_t dns_info{};
            ip4addr_aton(
                config.sta_dns,
                reinterpret_cast<ip4_addr_t*>(&dns_info.ip.u_addr.ip4));
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(context.sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        }

        ESP_LOGI(
            kTag,
            "Static IP: ip=%s netmask=%s gw=%s dns=%s",
            config.sta_ip,
            config.sta_netmask,
            config.sta_gateway,
            config.sta_dns);
    } else {
        err = esp_netif_dhcpc_start(context.sta_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            lock();
            setStateError(state_, esp_err_to_name(err));
            unlock();
            return err;
        }
    }

    xEventGroupClearBits(context.station_events, kStationConnectedBit | kStationFailedBit);

    wifi_config_t wifi_config{};
    copyString(
        reinterpret_cast<char*>(wifi_config.sta.ssid),
        sizeof(wifi_config.sta.ssid),
        config.wifi_sta_ssid);
    copyString(
        reinterpret_cast<char*>(wifi_config.sta.password),
        sizeof(wifi_config.sta.password),
        config.wifi_sta_password);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    if (kind == ConnectAttemptKind::kInitial) {
        context.reconnect_cycle_active = false;
    } else if (kind == ConnectAttemptKind::kRuntimeReconnect) {
        context.reconnect_cycle_active = true;
    }

    if (!preserve_ap) {
        context.auto_connect_on_sta_start = true;
        context.ignore_disconnect_until_ms =
            air360::uptimeMilliseconds() + kDisconnectIgnoreWindowMs;

        err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
            ESP_LOGW(kTag, "esp_wifi_stop before STA start failed: %s", esp_err_to_name(err));
        }

        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) {
            err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        }
        if (err == ESP_OK) {
            err = esp_wifi_start();
        }
        if (err == ESP_OK) {
            const wifi_ps_type_t ps_mode =
                config.wifi_power_save_enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
            err = esp_wifi_set_ps(ps_mode);
        }
    } else {
        context.auto_connect_on_sta_start = false;

        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err == ESP_OK) {
            err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        }
        if (err == ESP_OK) {
            const wifi_ps_type_t ps_mode =
                config.wifi_power_save_enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
            err = esp_wifi_set_ps(ps_mode);
        }
        if (err == ESP_OK) {
            err = esp_wifi_disconnect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED &&
                err != ESP_ERR_WIFI_NOT_CONNECT) {
                ESP_LOGW(kTag, "esp_wifi_disconnect before APSTA retry failed: %s", esp_err_to_name(err));
            }
            err = esp_wifi_connect();
        }
    }

    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    ESP_LOGI(
        kTag,
        "Station Wi-Fi power save: %s",
        config.wifi_power_save_enabled ? "MIN_MODEM" : "disabled");
    ESP_LOGI(kTag, "Station hostname: %s", hostname.c_str());
    ESP_LOGI(
        kTag,
        "%s station join: ssid=%s",
        preserve_ap ? "Retrying" : "Attempting",
        config.wifi_sta_ssid);

    const EventBits_t bits = waitForStationResult(context.station_events, timeout_ms);

    if ((bits & kStationConnectedBit) != 0U) {
        if (preserve_ap) {
            err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (err != ESP_OK) {
                lock();
                setStateError(state_, esp_err_to_name(err));
                unlock();
                return err;
            }

            lock();
            state_.mode = NetworkMode::kStation;
            state_.lab_ap_active = false;
            state_.setup_ap_retry_active = false;
            state_.next_setup_ap_retry_uptime_ms = 0U;
            unlock();
        }

        startMdns(hostname);

        const esp_err_t time_err = synchronizeTime();
        if (time_err != ESP_OK) {
            ESP_LOGW(kTag, "Time sync failed after station join: %s", esp_err_to_name(time_err));
        }
        return ESP_OK;
    }

    if ((bits & kStationFailedBit) == 0U) {
        std::uint32_t reconnect_delay_ms = 0U;

        lock();
        setStateError(state_, "station connect timeout (DHCP or IP assignment not completed)");
        state_.station_connected = false;
        state_.ip_address.clear();
        if (preserve_ap) {
            state_.mode = NetworkMode::kSetupAp;
            state_.setup_ap_retry_active = true;
            state_.next_setup_ap_retry_uptime_ms =
                air360::uptimeMilliseconds() + kSetupApRetryDelayMs;
        } else {
            state_.mode = NetworkMode::kOffline;
            if (kind == ConnectAttemptKind::kRuntimeReconnect) {
                context.reconnect_cycle_active = true;
                state_.reconnect_attempt_count += 1U;
                reconnect_delay_ms = reconnectDelayMs(state_.reconnect_attempt_count);
                state_.reconnect_backoff_active = true;
                state_.next_reconnect_uptime_ms =
                    air360::uptimeMilliseconds() + reconnect_delay_ms;
            }
        }
        unlock();

        if (preserve_ap) {
            armTimer(context.setup_ap_retry_timer, kSetupApRetryDelayMs);
        } else {
            context.ignore_disconnect_until_ms =
                air360::uptimeMilliseconds() + kDisconnectIgnoreWindowMs;
            const esp_err_t stop_err = esp_wifi_stop();
            if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_INIT &&
                stop_err != ESP_ERR_WIFI_NOT_STARTED) {
                ESP_LOGW(kTag, "esp_wifi_stop after STA timeout failed: %s", esp_err_to_name(stop_err));
            }
            if (kind == ConnectAttemptKind::kRuntimeReconnect && reconnect_delay_ms > 0U) {
                armTimer(context.reconnect_timer, reconnect_delay_ms);
            }
        }

        return ESP_ERR_TIMEOUT;
    }

    if (!preserve_ap) {
        context.ignore_disconnect_until_ms =
            air360::uptimeMilliseconds() + kDisconnectIgnoreWindowMs;
        const esp_err_t stop_err = esp_wifi_stop();
        if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_INIT &&
            stop_err != ESP_ERR_WIFI_NOT_STARTED) {
            ESP_LOGW(kTag, "esp_wifi_stop after STA failure failed: %s", esp_err_to_name(stop_err));
        }
    }

    return ESP_FAIL;
}

esp_err_t NetworkManager::connectStation(const DeviceConfig& config, std::uint32_t timeout_ms) {
    return attemptStationConnect(config, timeout_ms, ConnectAttemptKind::kInitial);
}

esp_err_t NetworkManager::startLabAp(const DeviceConfig& config) {
    RuntimeContext& context = runtime_;

    lock();
    state_.station_config_present = hasStationConfig(config);
    state_.station_connect_attempted = hasStationConfig(config);
    state_.station_ssid = config.wifi_sta_ssid;
    state_.lab_ap_ssid = config.lab_ap_ssid;
    configured_sntp_server_ =
        (config.sntp_server[0] != '\0') ? std::string(config.sntp_server) : std::string();
    last_config_ = config;
    has_last_config_ = true;
    unlock();

    esp_err_t err = ensureWifiInit();
    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    if (context.ap_netif == nullptr) {
        context.ap_netif = esp_netif_create_default_wifi_ap();
        if (context.ap_netif == nullptr) {
            lock();
            setStateError(state_, "failed to create AP netif");
            unlock();
            return ESP_FAIL;
        }
    }

    if (context.sta_netif == nullptr) {
        context.sta_netif = esp_netif_create_default_wifi_sta();
        if (context.sta_netif == nullptr) {
            lock();
            setStateError(state_, "failed to create station netif for scanning");
            unlock();
            return ESP_FAIL;
        }
    }

    stopTimerIfRunning(context.reconnect_timer);
    context.reconnect_cycle_active = false;
    context.auto_connect_on_sta_start = false;
    context.ignore_disconnect_until_ms =
        air360::uptimeMilliseconds() + kDisconnectIgnoreWindowMs;

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(kTag, "esp_wifi_stop before AP start failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    wifi_config_t ap_config{};
    copyString(
        reinterpret_cast<char*>(ap_config.ap.ssid),
        sizeof(ap_config.ap.ssid),
        config.lab_ap_ssid);
    copyString(
        reinterpret_cast<char*>(ap_config.ap.password),
        sizeof(ap_config.ap.password),
        config.lab_ap_password);
    ap_config.ap.ssid_len = std::strlen(config.lab_ap_ssid);
    ap_config.ap.channel = CONFIG_AIR360_LAB_AP_CHANNEL;
    ap_config.ap.max_connection = CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS;
    ap_config.ap.authmode =
        std::strlen(config.lab_ap_password) == 0U ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    if (hasStationConfig(config)) {
        wifi_config_t sta_config{};
        copyString(
            reinterpret_cast<char*>(sta_config.sta.ssid),
            sizeof(sta_config.sta.ssid),
            config.wifi_sta_ssid);
        copyString(
            reinterpret_cast<char*>(sta_config.sta.password),
            sizeof(sta_config.sta.password),
            config.wifi_sta_password);
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        sta_config.sta.pmf_cfg.capable = true;
        sta_config.sta.pmf_cfg.required = false;

        err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        if (err != ESP_OK) {
            lock();
            setStateError(state_, esp_err_to_name(err));
            unlock();
            return err;
        }
    }

    err = esp_netif_dhcps_stop(context.ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    esp_netif_ip_info_t ip_info{};
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    err = esp_netif_set_ip_info(context.ap_netif, &ip_info);
    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    err = esp_netif_dhcps_start(context.ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        lock();
        setStateError(state_, esp_err_to_name(err));
        unlock();
        return err;
    }

    char ip_buffer[16];
    std::snprintf(ip_buffer, sizeof(ip_buffer), IPSTR, IP2STR(&ip_info.ip));

    lock();
    state_.mode = NetworkMode::kSetupAp;
    state_.lab_ap_active = true;
    state_.station_connected = false;
    state_.ip_address = ip_buffer;
    state_.reconnect_backoff_active = false;
    state_.next_reconnect_uptime_ms = 0U;
    state_.reconnect_attempt_count = 0U;
    state_.setup_ap_retry_active = hasStationConfig(config);
    state_.next_setup_ap_retry_uptime_ms =
        hasStationConfig(config) ? (air360::uptimeMilliseconds() + kSetupApRetryDelayMs) : 0U;
    unlock();

    if (hasStationConfig(config)) {
        armTimer(context.setup_ap_retry_timer, kSetupApRetryDelayMs);
    } else {
        stopTimerIfRunning(context.setup_ap_retry_timer);
    }

    ESP_LOGI(kTag, "Setup AP active: ssid=%s ip=%s", config.lab_ap_ssid, ip_buffer);

    const esp_err_t scan_err = scanAvailableNetworks();
    if (scan_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Initial Wi-Fi scan in setup AP mode failed: %s",
            esp_err_to_name(scan_err));
    }

    return ESP_OK;
}

esp_err_t NetworkManager::scanAvailableNetworks() {
    RuntimeContext& context = runtime_;
    const esp_err_t init_err = ensureWifiInit();
    if (init_err != ESP_OK) {
        return init_err;
    }

    lock();
    if (scan_in_progress_) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    scan_in_progress_ = true;
    unlock();

    if (xTaskGetCurrentTaskHandle() == context.worker_task) {
        return startAsyncScanAndWait();
    }

    if (context.scan_done == nullptr) {
        lock();
        scan_in_progress_ = false;
        unlock();
        return ESP_ERR_INVALID_STATE;
    }

    notifyWorker(kWorkerScanReq);
    return ESP_OK;
}

esp_err_t NetworkManager::startAsyncScanAndWait() {
    RuntimeContext& context = runtime_;

    const auto fail = [this](esp_err_t err, const char* message = nullptr) -> esp_err_t {
        lock();
        available_networks_.clear();
        last_scan_uptime_ms_ = 0U;
        last_scan_error_ =
            message != nullptr ? std::string(message) : std::string(esp_err_to_name(err));
        scan_in_progress_ = false;
        unlock();
        return err;
    };

    wifi_mode_t mode = WIFI_MODE_NULL;
    const esp_err_t mode_err = esp_wifi_get_mode(&mode);
    if (mode_err != ESP_OK) {
        return fail(mode_err);
    }

    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        return fail(ESP_ERR_WIFI_MODE, "Wi-Fi scan requires STA or APSTA mode");
    }

    if (context.scan_done == nullptr) {
        return fail(ESP_ERR_INVALID_STATE);
    }

    // Drain any stale signal from a prior scan cycle
    while (xSemaphoreTake(context.scan_done, 0U) == pdTRUE) {
    }

    const esp_err_t start_err = esp_wifi_scan_start(nullptr, false);
    if (start_err != ESP_OK) {
        return fail(start_err);
    }

    // Block until WIFI_EVENT_SCAN_DONE gives the semaphore (or timeout)
    const BaseType_t taken = xSemaphoreTake(context.scan_done, kScanRequestTimeout);
    if (taken != pdTRUE) {
        esp_wifi_scan_stop();
        return fail(ESP_ERR_TIMEOUT, "Wi-Fi scan timeout");
    }

    // Results were stored and scan_in_progress_ cleared by the event handler
    return ESP_OK;
}

SntpCheckResult NetworkManager::checkSntp(const std::string& server, std::uint32_t timeout_ms) {
    SntpCheckResult result;

    if (server.empty() || server.size() > 63U) {
        result.error = "invalid_input";
        return result;
    }

    for (const char ch : server) {
        if (ch <= ' ' || ch > '~') {
            result.error = "invalid_input";
            return result;
        }
    }

    lock();
    const bool station_connected = state_.station_connected;
    unlock();
    if (!station_connected) {
        result.error = "not_connected";
        return result;
    }

    RuntimeContext& context = runtime_;

    if (context.sntp_initialized) {
        esp_netif_sntp_deinit();
        context.sntp_initialized = false;
    }

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server.c_str());
    const esp_err_t init_err = esp_netif_sntp_init(&sntp_config);
    if (init_err != ESP_OK) {
        result.error = "sync_failed";
        ESP_LOGW(
            kTag,
            "Check SNTP init failed for '%s': %s",
            server.c_str(),
            esp_err_to_name(init_err));
        return result;
    }
    context.sntp_initialized = true;

    ESP_LOGI(kTag, "Checking SNTP server: %s (timeout %" PRIu32 " ms)", server.c_str(), timeout_ms);
    const std::int64_t started_ms = uptimeMilliseconds();
    bool synced = false;
    while ((uptimeMilliseconds() - started_ms) < timeout_ms) {
        const esp_err_t wait_err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(kSntpPollIntervalMs));
        if (wait_err == ESP_OK) {
            synced = true;
            break;
        }
        resetCurrentTaskWatchdogIfSubscribed();
    }

    if (synced) {
        result.success = true;
        lock();
        state_.time_synchronized = true;
        state_.time_sync_error.clear();
        state_.last_time_sync_unix_ms = air360::currentUnixMilliseconds();
        unlock();
        ESP_LOGI(kTag, "SNTP check succeeded for server: %s", server.c_str());
    } else {
        result.error = "sync_failed";
        ESP_LOGW(kTag, "SNTP check timed out for server: %s", server.c_str());
        esp_netif_sntp_deinit();
        context.sntp_initialized = false;
    }

    return result;
}

esp_err_t NetworkManager::ensureStationTime(std::uint32_t timeout_ms) {
    lock();
    const bool ready = (state_.mode == NetworkMode::kStation && state_.station_connected);
    if (!ready) {
        state_.time_sync_error = "station is not connected";
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    unlock();

    if (hasValidUnixTime()) {
        lock();
        state_.time_sync_attempted = true;
        state_.time_synchronized = true;
        state_.time_sync_error.clear();
        state_.last_time_sync_unix_ms = air360::currentUnixMilliseconds();
        unlock();
        return ESP_OK;
    }

    return synchronizeTime(timeout_ms);
}

UplinkStatus NetworkManager::uplinkStatus() const {
    UplinkStatus status;
    const NetworkState snapshot = state();

    if (!snapshot.cellular_ip.empty() && hasValidUnixTime()) {
        status.uplink_ready = true;
        status.active_bearer = UplinkBearer::kCellular;
        return status;
    }

    if (snapshot.mode == NetworkMode::kStation &&
        snapshot.station_connected &&
        hasValidUnixTime()) {
        status.uplink_ready = true;
        status.active_bearer = UplinkBearer::kWifi;
    }
    return status;
}

void NetworkManager::setCellularStatus(bool ppp_connected, const char* ip_address) {
    lock();
    if (ppp_connected && ip_address != nullptr && ip_address[0] != '\0') {
        state_.cellular_ip = ip_address;
    } else {
        state_.cellular_ip.clear();
    }
    unlock();
}

esp_err_t NetworkManager::stopStation() {
    RuntimeContext& context = runtime_;
    stopTimerIfRunning(context.reconnect_timer);
    stopTimerIfRunning(context.setup_ap_retry_timer);
    context.reconnect_cycle_active = false;
    context.auto_connect_on_sta_start = false;
    context.ignore_disconnect_until_ms =
        air360::uptimeMilliseconds() + kDisconnectIgnoreWindowMs;

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED &&
        err != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(kTag, "esp_wifi_disconnect on stop: %s", esp_err_to_name(err));
    }

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(kTag, "esp_wifi_stop: %s", esp_err_to_name(err));
        return err;
    }

    lock();
    state_.station_connected = false;
    state_.ip_address.clear();
    state_.reconnect_backoff_active = false;
    state_.next_reconnect_uptime_ms = 0U;
    state_.setup_ap_retry_active = false;
    state_.next_setup_ap_retry_uptime_ms = 0U;
    if (state_.mode == NetworkMode::kStation) {
        state_.mode = NetworkMode::kOffline;
    }
    unlock();

    ESP_LOGI(kTag, "Wi-Fi station stopped");
    return ESP_OK;
}

NetworkState NetworkManager::state() const {
    lock();
    const NetworkState snapshot = state_;
    unlock();
    return snapshot;
}

WifiScanSnapshot NetworkManager::wifiScanSnapshot() const {
    lock();
    WifiScanSnapshot snapshot;
    snapshot.networks = available_networks_;
    snapshot.last_scan_error = last_scan_error_;
    snapshot.last_scan_uptime_ms = last_scan_uptime_ms_;
    snapshot.scan_in_progress = scan_in_progress_;
    unlock();
    return snapshot;
}

bool NetworkManager::hasValidTime() const {
    return hasValidUnixTime();
}

std::int64_t NetworkManager::currentUnixMilliseconds() const {
    return air360::currentUnixMilliseconds();
}

}  // namespace air360
