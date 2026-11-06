#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "esp_err.h"

using nvs_handle_t = unsigned;
constexpr int NVS_READONLY = 0;
constexpr int NVS_READWRITE = 1;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH = 0x110c;

namespace nvs_test {
inline std::map<std::string, std::vector<std::uint8_t>> blobs;
inline unsigned writes = 0;
inline bool fail_write = false;
inline bool fail_commit = false;
inline bool fail_read = false;
}

inline esp_err_t nvs_open(const char*, int mode, nvs_handle_t* handle) {
    if (mode == NVS_READONLY && nvs_test::blobs.empty()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *handle = 1;
    return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_commit(nvs_handle_t) {
    return nvs_test::fail_commit ? ESP_FAIL : ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* data, std::size_t size) {
    ++nvs_test::writes;
    if (nvs_test::fail_write) {
        return ESP_FAIL;
    }
    // Match ESP-IDF: writes are visible immediately, not staged until commit.
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    nvs_test::blobs[key] = std::vector<std::uint8_t>(bytes, bytes + size);
    return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* out, std::size_t* size) {
    if (nvs_test::fail_read) {
        return ESP_FAIL;
    }
    const auto it = nvs_test::blobs.find(key);
    if (it == nvs_test::blobs.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (out != nullptr && *size < it->second.size()) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    *size = it->second.size();
    if (out != nullptr) {
        std::memcpy(out, it->second.data(), *size);
    }
    return ESP_OK;
}
inline esp_err_t nvs_get_u32(nvs_handle_t h, const char* key, std::uint32_t* value) {
    std::size_t size = sizeof(*value);
    return nvs_get_blob(h, key, value, &size);
}
inline esp_err_t nvs_set_u32(nvs_handle_t h, const char* key, std::uint32_t value) {
    return nvs_set_blob(h, key, &value, sizeof(value));
}
