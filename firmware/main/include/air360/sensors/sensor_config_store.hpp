#pragma once

#include <cstdint>
#include <functional>

#include "air360/sensors/sensor_config.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace air360 {

// Folds firmware-owned fields of the live config into an operator edit that
// was staged from `base`. Today the only such field is
// pending_maintenance_action: the sensor task clears it once a one-shot action
// finishes. If the operator left the field as it was in `base`, the live value
// wins, so a stale staged copy cannot re-arm an action that already ran. An
// explicit operator change (different from `base`) is kept.
void mergeFirmwareOwnedSensorFields(
    SensorConfigList& next,
    const SensorConfigList& base,
    const SensorConfigList& live);

// Single owner of the live sensor configuration and its persistence.
//
// The HTTP task (Apply) and the sensor task (maintenance-action completion)
// both read-modify-write the sensor config and its NVS blob. Every such
// sequence runs under one mutex here, so RAM and NVS never diverge and no
// update is lost. Each accepted replacement bumps a generation; SensorManager
// tags maintenance completions with the generation its task was started for,
// and completions from a superseded configuration are ignored.
class SensorConfigStore {
  public:
    using PersistFn = std::function<esp_err_t(const SensorConfigList&)>;

    SensorConfigStore();
    SensorConfigStore(const SensorConfigStore&) = delete;
    SensorConfigStore& operator=(const SensorConfigStore&) = delete;
    SensorConfigStore(SensorConfigStore&&) = delete;
    SensorConfigStore& operator=(SensorConfigStore&&) = delete;

    // Boot-time setup: installs the loaded config (already persisted or
    // defaults) and the persistence callback. Starts generation 1.
    void reset(const SensorConfigList& loaded, PersistFn persist);

    // Copies the live config (and its generation) under the lock.
    void copyTo(SensorConfigList& out, std::uint32_t* out_generation = nullptr) const;
    std::uint32_t generation() const;

    // Merges firmware-owned fields from the live config into `next` (see
    // mergeFirmwareOwnedSensorFields), persists it, and only on success makes
    // it the live config and bumps the generation. `next` holds the accepted
    // config on return.
    [[nodiscard]] esp_err_t replace(
        SensorConfigList& next,
        const SensorConfigList& base,
        std::uint32_t& out_generation);

    // Clears a completed one-shot maintenance action. No-op (out_cleared =
    // false) when `generation` is not the live generation, the sensor no
    // longer exists, or no action is pending. Persists before publishing.
    [[nodiscard]] esp_err_t clearMaintenanceAction(
        std::uint32_t sensor_id,
        std::uint32_t generation,
        bool& out_cleared);

  private:
    void lock() const;
    void unlock() const;

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    SensorConfigList live_{};
    std::uint32_t generation_ = 0U;
    PersistFn persist_;
};

}  // namespace air360
