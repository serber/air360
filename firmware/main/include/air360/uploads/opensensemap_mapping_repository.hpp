#pragma once

#include "air360/uploads/opensensemap_mapping.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace air360 {

// Persists the openSenseMap reading -> sensor-ID mapping table in its own NVS
// blob. Shared between the web task (save) and the upload task (load), so it
// caches the table behind a mutex and writes through on save. Mirrors the
// caching contract of Air360ApiCredentialRepository.
class OpenSenseMapMappingRepository {
  public:
    OpenSenseMapMappingRepository();

    // Loads the mapping table, falling back to an empty table when nothing is
    // stored or the stored blob is incompatible.
    [[nodiscard]] esp_err_t load(OpenSenseMapMappingTable& out_table) const;
    [[nodiscard]] esp_err_t save(const OpenSenseMapMappingTable& table) const;

  private:
    [[nodiscard]] esp_err_t ensureCacheLoaded() const;
    [[nodiscard]] esp_err_t loadFromNvs(OpenSenseMapMappingTable& out_table) const;

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    mutable bool cache_loaded_ = false;
    mutable OpenSenseMapMappingTable cached_table_{};
};

}  // namespace air360
