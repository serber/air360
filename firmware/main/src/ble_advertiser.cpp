#include "air360/ble_advertiser.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "air360/ble_encoding.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/tuning.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sdkconfig.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.ble";
constexpr EventBits_t kSyncedBit = (1U << 0);
constexpr std::uint32_t kUpdateIntervalMs = tuning::ble::kPayloadRefreshIntervalMs;
// Poll NimBLE host sync once per second so startup stays responsive while the
// BLE task still feeds TWDT during long controller/host bring-up windows.
constexpr TickType_t kSyncWaitSlice = pdMS_TO_TICKS(1000U);
// 2 s stop wait covers the BLE task's notification wake + semaphore ack path
// without leaving stop() stuck if the task is already unhealthy.
constexpr TickType_t kStopTimeout = pdMS_TO_TICKS(2000U);
// BTHome v2 service UUID `0xFCD2`, split into bytes for the NimBLE payload.
constexpr std::uint8_t kBthomeUuidLo = 0xD2U;
constexpr std::uint8_t kBthomeUuidHi = 0xFCU;
constexpr std::uint8_t kBthomeDeviceInfo = 0x40U;  // no encryption, BTHome v2
// Legacy ADV packets are capped at 31 bytes, so payload packing must budget
// against this limit even though NimBLE also supports larger extended PDUs.
constexpr std::uint8_t kLegacyAdvPacketMaxLen = 31U;

static StaticEventGroup_t g_sync_event_buf;
static EventGroupHandle_t g_sync_event = nullptr;

struct BthomeEntry {
    SensorValueKind kind;
    std::uint8_t object_id;
    std::uint8_t value_bytes;
    bool is_signed;
    float factor;
};

static_assert(sizeof(BthomeEntry) == 8U,
    "BthomeEntry layout changed — update kBthomeMap designated initializers");

// Encoding priority: most important first. Total packet budget = 27 bytes
// (31 byte limit − flags 3B − name AD ~8B − service data header 4B − device_info 1B = ~15B free).
// Each entry = 1B ID + value_bytes.
constexpr std::array<BthomeEntry, 7U> kBthomeMap{{
    {.kind = SensorValueKind::kTemperatureC,    .object_id = 0x02U, .value_bytes = 2U, .is_signed = true,  .factor = 100.0f},
    {.kind = SensorValueKind::kHumidityPercent, .object_id = 0x03U, .value_bytes = 2U, .is_signed = false, .factor = 100.0f},
    {.kind = SensorValueKind::kCo2Ppm,          .object_id = 0x12U, .value_bytes = 2U, .is_signed = false, .factor =   1.0f},
    {.kind = SensorValueKind::kPm2_5UgM3,       .object_id = 0x0DU, .value_bytes = 2U, .is_signed = false, .factor = 100.0f},
    {.kind = SensorValueKind::kPm10_0UgM3,      .object_id = 0x0EU, .value_bytes = 2U, .is_signed = false, .factor = 100.0f},
    {.kind = SensorValueKind::kPressureHpa,     .object_id = 0x04U, .value_bytes = 3U, .is_signed = false, .factor = 100.0f},
    {.kind = SensorValueKind::kIlluminanceLux,  .object_id = 0x05U, .value_bytes = 3U, .is_signed = false, .factor = 100.0f},
}};

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
    // FreeRTOS task entry signature requires the parameter; NimBLE state is global.
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
    // Stopping a non-advertising GAP instance is acceptable during shutdown.
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
            // TWDT reset failure is non-actionable inside the subscribed task loop.
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
        // The notification count only wakes the advertiser loop for shutdown/update checks.
        static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kUpdateIntervalMs)));
        if (wdt_subscribed) {
            // TWDT reset failure is non-actionable inside the subscribed task loop.
            static_cast<void>(esp_task_wdt_reset());
        }
        if (!enabled_.load(std::memory_order_acquire)) {
            break;
        }
        updateAdvertisement();
    }

    task_handle_.store(nullptr, std::memory_order_release);
    if (wdt_subscribed) {
        // Task teardown continues even if TWDT already removed this task.
        static_cast<void>(esp_task_wdt_delete(nullptr));
    }
    if (stop_done_ != nullptr) {
        xSemaphoreGive(stop_done_);
    }
    vTaskDelete(nullptr);
}

void BleAdvertiser::updateAdvertisement() {
    std::uint8_t adv_buf[kLegacyAdvPacketMaxLen];
    std::uint8_t adv_len = 0U;
    std::array<std::uint8_t, 28U> svc_data{};
    const std::uint8_t svc_payload_len = buildPayload(svc_data.data(), svc_data.size());
    const std::uint8_t name_len = static_cast<std::uint8_t>(std::strlen(device_name_));

    struct ble_hs_adv_fields fixed_fields = {};
    fixed_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    if (3U + 2U + name_len <= kLegacyAdvPacketMaxLen) {
        fixed_fields.name = reinterpret_cast<const std::uint8_t*>(device_name_);
        fixed_fields.name_len = name_len;
        fixed_fields.name_is_complete = 1;
    }
    const int rc_fields =
        ble_hs_adv_set_fields(&fixed_fields, adv_buf, &adv_len, sizeof(adv_buf));
    if (rc_fields != 0) {
        ESP_LOGW(kTag, "ble_hs_adv_set_fields failed: %d", rc_fields);
        return;
    }

    if (adv_len + 2U + svc_payload_len <= sizeof(adv_buf)) {
        adv_buf[adv_len++] = static_cast<std::uint8_t>(1U + svc_payload_len);
        adv_buf[adv_len++] = BLE_HS_ADV_TYPE_SVC_DATA_UUID16;
        std::memcpy(&adv_buf[adv_len], svc_data.data(), svc_payload_len);
        adv_len = static_cast<std::uint8_t>(adv_len + svc_payload_len);
    }

    ble_gap_adv_stop();

    const int rc_set_data = ble_gap_adv_set_data(adv_buf, static_cast<int>(adv_len));
    if (rc_set_data != 0) {
        ESP_LOGW(kTag, "ble_gap_adv_set_data failed: %d", rc_set_data);
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

    for (std::size_t map_idx = 0U; map_idx < kBthomeMap.size(); ++map_idx) {
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
                const auto ival = static_cast<std::int16_t>(
                    std::clamp(raw, -32768.0f, 32767.0f));
                ble::writeLe16(&buf[offset], static_cast<std::uint16_t>(ival));
            } else {
                const auto uval = static_cast<std::uint16_t>(
                    std::clamp(raw, 0.0f, 65535.0f));
                ble::writeLe16(&buf[offset], uval);
            }
        } else {
            const auto uval = static_cast<std::uint32_t>(
                std::clamp(raw, 0.0f, 16777215.0f));
            ble::writeLe24(&buf[offset], uval);
        }

        offset += bte.value_bytes;
    }

    return offset;
}

}  // namespace air360
