#include "air360/uploads/opensensemap_mapping_repository.hpp"

#include <cstring>

#include "nvs.h"

namespace air360 {

namespace {

constexpr char kNamespace[] = "air360";
constexpr char kMappingKey[] = "osem_map";

// A stored blob is only trusted when its header matches the current schema and
// the entry count fits the fixed array; anything else falls back to empty.
[[nodiscard]] bool isValidTable(const OpenSenseMapMappingTable& table) {
    return table.magic == kOpenSenseMapMappingMagic &&
           table.schema_version == kOpenSenseMapMappingSchemaVersion &&
           table.entry_count <= kMaxOpenSenseMapMappings;
}

}  // namespace

OpenSenseMapMappingRepository::OpenSenseMapMappingRepository() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}

esp_err_t OpenSenseMapMappingRepository::load(OpenSenseMapMappingTable& out_table) const {
    out_table = OpenSenseMapMappingTable{};

    const esp_err_t err = ensureCacheLoaded();
    if (err != ESP_OK) {
        return err;
    }

    if (mutex_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    out_table = cached_table_;
    xSemaphoreGive(mutex_);
    return ESP_OK;
}

esp_err_t OpenSenseMapMappingRepository::save(const OpenSenseMapMappingTable& table) const {
    if (!isValidTable(table)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        xSemaphoreGive(mutex_);
        return err;
    }

    err = nvs_set_blob(handle, kMappingKey, &table, sizeof(table));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        cached_table_ = table;
        cache_loaded_ = true;
    }
    xSemaphoreGive(mutex_);
    return err;
}

esp_err_t OpenSenseMapMappingRepository::ensureCacheLoaded() const {
    if (mutex_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (cache_loaded_) {
        xSemaphoreGive(mutex_);
        return ESP_OK;
    }

    OpenSenseMapMappingTable table{};
    const esp_err_t err = loadFromNvs(table);
    if (err == ESP_OK) {
        cached_table_ = table;
        cache_loaded_ = true;
    }
    xSemaphoreGive(mutex_);
    return err;
}

esp_err_t OpenSenseMapMappingRepository::loadFromNvs(
    OpenSenseMapMappingTable& out_table) const {
    out_table = OpenSenseMapMappingTable{};

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    std::size_t blob_size = 0U;
    err = nvs_get_blob(handle, kMappingKey, nullptr, &blob_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    if (blob_size != sizeof(OpenSenseMapMappingTable)) {
        // Incompatible layout from an older/newer schema: ignore and start empty.
        nvs_close(handle);
        return ESP_OK;
    }

    OpenSenseMapMappingTable stored{};
    err = nvs_get_blob(handle, kMappingKey, &stored, &blob_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    if (isValidTable(stored)) {
        out_table = stored;
    }
    return ESP_OK;
}

}  // namespace air360
