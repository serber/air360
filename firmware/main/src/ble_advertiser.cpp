#include "air360/ble_advertiser.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "air360/sensors/sensor_types.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sdkconfig.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.ble";
constexpr EventBits_t kSyncedBit = (1U << 0);
constexpr std::uint32_t kUpdateIntervalMs = 5000U;
constexpr TickType_t kSyncWaitSlice = pdMS_TO_TICKS(1000U);
constexpr TickType_t kStopTimeout = pdMS_TO_TICKS(2000U);
constexpr std::uint8_t kBthomeUuidLo = 0xD2U;
constexpr std::uint8_t kBthomeUuidHi = 0xFCU;
constexpr std::uint8_t kBthomeDeviceInfo = 0x40U;  // no encryption, BTHome v2

static StaticEventGroup_t g_sync_event_buf;
static EventGroupHandle_t g_sync_event = nullptr;

struct BthomeEntry {
    SensorValueKind kind;
    std::uint8_t object_id;
    std::uint8_t value_bytes;
    bool is_signed;
    float factor;
};

// Encoding priority: most important first. Total packet budget = 27 bytes
// (31 byte limit − flags 3B − name AD ~8B − service data header 4B − device_info 1B = ~15B free).
// Each entry = 1B ID + value_bytes.
constexpr BthomeEntry kBthomeMap[] = {
    {SensorValueKind::kTemperatureC,    0x02U, 2U, true,  100.0f},
    {SensorValueKind::kHumidityPercent, 0x03U, 2U, false, 100.0f},
    {SensorValueKind::kCo2Ppm,          0x12U, 2U, false,   1.0f},
    {SensorValueKind::kPm2_5UgM3,       0x0DU, 2U, false, 100.0f},
    {SensorValueKind::kPm10_0UgM3,      0x0EU, 2U, false, 100.0f},
    {SensorValueKind::kPressureHpa,     0x04U, 3U, false, 100.0f},
    {SensorValueKind::kIlluminanceLux,  0x05U, 3U, false, 100.0f},
};
constexpr std::size_t kBthomeMapSize = sizeof(kBthomeMap) / sizeof(kBthomeMap[0]);

void onSync() {
    ESP_LOGI(kTag, "NimBLE host synced");
    if (g_sync_event != nullptr) {
        xEventGroupSetBits(g_sync_event, kSyncedBit);
    }
}

void onReset(int reason) {
    ESP_LOGW(kTag, "NimBLE host reset: reason=%d", reason);
}

void nimbleHostTask(void* param) {
    static_cast<void>(param);
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool deinitNimblePortIfSupported() {
#if CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC && CONFIG_BT_NIMBLE_SECURITY_ENABLE && \
    !CONFIG_BT_NIMBLE_ROLE_PERIPHERAL && !CONFIG_BT_NIMBLE_ROLE_CENTRAL
    ESP_LOGW(
        kTag,
        "Skipping NimBLE port deinit in broadcaster-only config; host task was stopped");
    return false;
#else
    const esp_err_t deinit_err = nimble_port_deinit();
    if (deinit_err != ESP_OK) {
        ESP_LOGW(kTag, "NimBLE deinit failed: %s", esp_err_to_name(deinit_err));
        return false;
    }
    return true;
#endif
}

}  // namespace

void BleAdvertiser::start(const DeviceConfig& config, MeasurementStore& store) {
    if (running_.load(std::memory_order_acquire)) {
        ESP_LOGW(kTag, "BLE advertiser already running, stopping before restart");
        stop();
        if (running_.load(std::memory_order_acquire)) {
            ESP_LOGE(kTag, "BLE advertiser restart aborted because previous task did not stop");
            return;
        }
    }

    if (config.ble_advertise_enabled == 0U) {
        ESP_LOGI(kTag, "BLE advertising disabled in config");
        enabled_.store(false, std::memory_order_release);
        return;
    }

    if (stop_done_ == nullptr) {
        stop_done_ = xSemaphoreCreateBinaryStatic(&stop_done_buf_);
        if (stop_done_ == nullptr) {
            ESP_LOGE(kTag, "Failed to create BLE stop semaphore");
            return;
        }
    }
    while (xSemaphoreTake(stop_done_, 0) == pdTRUE) {
    }

    store_ = &store;
    enabled_.store(true, std::memory_order_release);

    const std::uint8_t idx = config.ble_adv_interval_index < kBleAdvIntervalCount
        ? config.ble_adv_interval_index : kBleAdvIntervalDefaultIndex;
    adv_interval_ms_ = kBleAdvIntervalTable[idx];

    std::strncpy(device_name_, config.device_name, sizeof(device_name_) - 1U);
    device_name_[sizeof(device_name_) - 1U] = '\0';

    g_sync_event = xEventGroupCreateStatic(&g_sync_event_buf);
    xEventGroupClearBits(g_sync_event, kSyncedBit);

    if (!nimble_initialized_) {
        const esp_err_t init_err = nimble_port_init();
        if (init_err != ESP_OK) {
            ESP_LOGE(kTag, "NimBLE init failed: %s", esp_err_to_name(init_err));
            enabled_.store(false, std::memory_order_release);
            return;
        }
        nimble_initialized_ = true;
    }
    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.reset_cb = onReset;
    nimble_port_freertos_init(nimbleHostTask);

    TaskHandle_t task = nullptr;
    const BaseType_t ret = xTaskCreate(
        taskEntry,
        "air360_ble",
        4096U,
        this,
        3U,
        &task);
    if (ret != pdPASS) {
        ESP_LOGE(kTag, "Failed to create BLE task");
        enabled_.store(false, std::memory_order_release);
        const int stop_err = nimble_port_stop();
        if (stop_err != 0) {
            ESP_LOGW(kTag, "NimBLE stop after task create failure failed: %d", stop_err);
        }
        if (deinitNimblePortIfSupported()) {
            nimble_initialized_ = false;
        }
        return;
    }

    task_handle_.store(task, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "BLE advertiser started, interval=%" PRIu16 " ms", adv_interval_ms_);
}

void BleAdvertiser::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        enabled_.store(false, std::memory_order_release);
        return;
    }

    enabled_.store(false, std::memory_order_release);
    static_cast<void>(ble_gap_adv_stop());

    TaskHandle_t task = task_handle_.load(std::memory_order_acquire);
    if (task != nullptr) {
        xTaskNotifyGive(task);
        if (stop_done_ == nullptr ||
            xSemaphoreTake(stop_done_, kStopTimeout) != pdTRUE) {
            ESP_LOGE(kTag, "Timed out waiting for BLE advertiser task to stop");
            return;
        }
    }

    task_handle_.store(nullptr, std::memory_order_release);

    const int stop_err = nimble_port_stop();
    if (stop_err != 0) {
        ESP_LOGW(kTag, "NimBLE stop failed: %d", stop_err);
    }
    if (deinitNimblePortIfSupported()) {
        nimble_initialized_ = false;
    }
    running_.store(false, std::memory_order_release);
}

BleState BleAdvertiser::state() const {
    return {
        enabled_.load(std::memory_order_acquire),
        running_.load(std::memory_order_acquire),
        adv_interval_ms_};
}

void BleAdvertiser::taskEntry(void* arg) {
    static_cast<BleAdvertiser*>(arg)->taskMain();
}

void BleAdvertiser::taskMain() {
    bool wdt_subscribed = false;
    const esp_err_t wdt_err = esp_task_wdt_add(nullptr);
    if (wdt_err == ESP_OK) {
        wdt_subscribed = true;
        ESP_LOGI(kTag, "TWDT: air360_ble subscribed");
    } else {
        ESP_LOGW(kTag, "TWDT subscribe failed: %s", esp_err_to_name(wdt_err));
    }

    while (enabled_.load(std::memory_order_acquire)) {
        const EventBits_t bits =
            xEventGroupWaitBits(g_sync_event, kSyncedBit, pdFALSE, pdTRUE, kSyncWaitSlice);
        if (wdt_subscribed) {
            static_cast<void>(esp_task_wdt_reset());
        }
        if ((bits & kSyncedBit) != 0U) {
            break;
        }
    }

    if (enabled_.load(std::memory_order_acquire)) {
        updateAdvertisement();
    }

    while (enabled_.load(std::memory_order_acquire)) {
        static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kUpdateIntervalMs)));
        if (wdt_subscribed) {
            static_cast<void>(esp_task_wdt_reset());
        }
        if (!enabled_.load(std::memory_order_acquire)) {
            break;
        }
        updateAdvertisement();
    }

    task_handle_.store(nullptr, std::memory_order_release);
    if (wdt_subscribed) {
        static_cast<void>(esp_task_wdt_delete(nullptr));
    }
    if (stop_done_ != nullptr) {
        xSemaphoreGive(stop_done_);
    }
    vTaskDelete(nullptr);
}

void BleAdvertiser::updateAdvertisement() {
    // Build raw advertisement packet manually to avoid ble_hs_adv_fields API differences
    // across NimBLE versions (flags_is_present was removed in newer ESP-IDF).
    std::uint8_t adv_buf[31];
    std::uint8_t adv_len = 0U;

    // AD: Flags (3 bytes)
    adv_buf[adv_len++] = 2U;                                          // length
    adv_buf[adv_len++] = 0x01U;                                       // type: Flags
    adv_buf[adv_len++] = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // AD: Complete Local Name
    const std::uint8_t name_len = static_cast<std::uint8_t>(std::strlen(device_name_));
    if (adv_len + 2U + name_len <= sizeof(adv_buf)) {
        adv_buf[adv_len++] = static_cast<std::uint8_t>(1U + name_len);  // length
        adv_buf[adv_len++] = 0x09U;                                      // type: Complete Local Name
        std::memcpy(&adv_buf[adv_len], device_name_, name_len);
        adv_len = static_cast<std::uint8_t>(adv_len + name_len);
    }

    // AD: Service Data — 16-bit UUID (BTHome v2)
    std::uint8_t svc_data[28];
    const std::uint8_t svc_payload_len = buildPayload(svc_data, sizeof(svc_data));
    if (adv_len + 2U + svc_payload_len <= sizeof(adv_buf)) {
        adv_buf[adv_len++] = static_cast<std::uint8_t>(1U + svc_payload_len);  // length
        adv_buf[adv_len++] = 0x16U;                                             // type: Service Data 16-bit
        std::memcpy(&adv_buf[adv_len], svc_data, svc_payload_len);
        adv_len = static_cast<std::uint8_t>(adv_len + svc_payload_len);
    }

    ble_gap_adv_stop();

    const int rc_fields = ble_gap_adv_set_data(adv_buf, static_cast<int>(adv_len));
    if (rc_fields != 0) {
        ESP_LOGW(kTag, "ble_gap_adv_set_data failed: %d", rc_fields);
        return;
    }

    // BLE time units: 1 unit = 0.625 ms → multiply ms by 1000 / 625 = 8/5
    const std::uint16_t interval_units = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(adv_interval_ms_) * 8U) / 5U);

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = interval_units;
    adv_params.itvl_max = interval_units;

    const int rc_adv = ble_gap_adv_start(
        BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv_params, nullptr, nullptr);
    if (rc_adv != 0) {
        ESP_LOGW(kTag, "ble_gap_adv_start failed: %d", rc_adv);
    }
}

std::uint8_t BleAdvertiser::buildPayload(std::uint8_t* buf, std::uint8_t max_len) {
    if (max_len < 3U) {
        return 0U;
    }

    buf[0] = kBthomeUuidLo;
    buf[1] = kBthomeUuidHi;
    buf[2] = kBthomeDeviceInfo;
    std::uint8_t offset = 3U;

    if (store_ == nullptr) {
        return offset;
    }

    std::array<MeasurementRuntimeInfo, kMaxConfiguredSensors> latest{};
    const std::size_t latest_count =
        store_->allLatestMeasurements(latest.data(), latest.size());

    for (std::size_t map_idx = 0U; map_idx < kBthomeMapSize; ++map_idx) {
        const BthomeEntry& bte = kBthomeMap[map_idx];
        const std::uint8_t needed = 1U + bte.value_bytes;

        if (offset + needed > max_len) {
            break;
        }

        bool found = false;
        float val = 0.0f;

        for (std::size_t entry_index = 0U; entry_index < latest_count; ++entry_index) {
            const MeasurementRuntimeInfo& entry = latest[entry_index];
            for (std::uint8_t i = 0U; i < entry.measurement.value_count; ++i) {
                if (entry.measurement.values[i].kind == bte.kind) {
                    val = entry.measurement.values[i].value;
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }

        if (!found) {
            continue;
        }

        buf[offset] = bte.object_id;
        ++offset;

        const float raw = val * bte.factor;

        if (bte.value_bytes == 2U) {
            if (bte.is_signed) {
                const float clamped = std::fmaxf(-32768.0f, std::fminf(32767.0f, raw));
                const auto ival = static_cast<std::int16_t>(clamped);
                buf[offset]      = static_cast<std::uint8_t>(static_cast<std::uint16_t>(ival) & 0xFFU);
                buf[offset + 1U] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(ival) >> 8U);
            } else {
                const float clamped = std::fmaxf(0.0f, std::fminf(65535.0f, raw));
                const auto uval = static_cast<std::uint16_t>(clamped);
                buf[offset]      = static_cast<std::uint8_t>(uval & 0xFFU);
                buf[offset + 1U] = static_cast<std::uint8_t>(uval >> 8U);
            }
        } else {
            const float clamped = std::fmaxf(0.0f, std::fminf(16777215.0f, raw));
            const auto uval = static_cast<std::uint32_t>(clamped);
            buf[offset]      = static_cast<std::uint8_t>(uval & 0xFFU);
            buf[offset + 1U] = static_cast<std::uint8_t>((uval >> 8U) & 0xFFU);
            buf[offset + 2U] = static_cast<std::uint8_t>((uval >> 16U) & 0xFFU);
        }

        offset += bte.value_bytes;
    }

    return offset;
}

}  // namespace air360
