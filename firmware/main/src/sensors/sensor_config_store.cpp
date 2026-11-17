#include "air360/sensors/sensor_config_store.hpp"

#include <utility>

#include "air360/sensors/sensor_types.hpp"

namespace air360 {

void mergeFirmwareOwnedSensorFields(
    SensorConfigList& next,
    const SensorConfigList& base,
    const SensorConfigList& live) {
    for (std::size_t index = 0U; index < next.sensor_count && index < next.sensors.size();
         ++index) {
        SensorRecord& record = next.sensors[index];
        const SensorRecord* base_record = findSensorRecordById(base, record.id);
        const SensorRecord* live_record = findSensorRecordById(live, record.id);
        if (base_record == nullptr || live_record == nullptr) {
            // Newly added in this edit, or deleted from the live config by
            // someone else: nothing firmware-owned to carry over.
            continue;
        }
        if (record.pending_maintenance_action == base_record->pending_maintenance_action) {
            record.pending_maintenance_action = live_record->pending_maintenance_action;
        }
    }
}

SensorConfigStore::SensorConfigStore()
    : live_(makeDefaultSensorConfigList()) {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}

void SensorConfigStore::lock() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void SensorConfigStore::unlock() const {
    xSemaphoreGive(mutex_);
}

void SensorConfigStore::reset(const SensorConfigList& loaded, PersistFn persist) {
    lock();
    live_ = loaded;
    persist_ = std::move(persist);
    generation_ = 1U;
    unlock();
}

void SensorConfigStore::copyTo(SensorConfigList& out, std::uint32_t* out_generation) const {
    lock();
    out = live_;
    if (out_generation != nullptr) {
        *out_generation = generation_;
    }
    unlock();
}

std::uint32_t SensorConfigStore::generation() const {
    lock();
    const std::uint32_t generation = generation_;
    unlock();
    return generation;
}

esp_err_t SensorConfigStore::replace(
    SensorConfigList& next,
    const SensorConfigList& base,
    std::uint32_t& out_generation) {
    lock();
    mergeFirmwareOwnedSensorFields(next, base, live_);
    const esp_err_t err = persist_ ? persist_(next) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        live_ = next;
        ++generation_;
    }
    out_generation = generation_;
    unlock();
    return err;
}

esp_err_t SensorConfigStore::clearMaintenanceAction(
    std::uint32_t sensor_id,
    std::uint32_t generation,
    bool& out_cleared) {
    out_cleared = false;
    lock();
    SensorRecord* live_record = findSensorRecordById(live_, sensor_id);
    if (generation != generation_ || live_record == nullptr ||
        live_record->pending_maintenance_action ==
            static_cast<std::uint8_t>(MaintenanceActionKind::kNone)) {
        unlock();
        return ESP_OK;
    }

    // Modify in place (no second config copy on the sensor task stack) and
    // roll back if persisting fails, so RAM never runs ahead of NVS.
    const std::uint8_t previous_action = live_record->pending_maintenance_action;
    live_record->pending_maintenance_action =
        static_cast<std::uint8_t>(MaintenanceActionKind::kNone);
    const esp_err_t err = persist_ ? persist_(live_) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        out_cleared = true;
    } else {
        live_record->pending_maintenance_action = previous_action;
    }
    unlock();
    return err;
}

}  // namespace air360
