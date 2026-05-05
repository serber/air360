#include "air360/uploads/air360_api_credentials.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "air360/crypto_utils.hpp"
#include "esp_random.h"
#include "nvs.h"

namespace air360 {

namespace {

constexpr char kNamespace[] = "air360_cred";
constexpr char kSecretKey[] = "air360_us";

bool isBase64UrlChar(char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' ||
           ch == '_';
}

}  // namespace

bool isValidAir360UploadSecret(const std::string& secret) {
    if (secret.size() != static_cast<std::size_t>(kAir360UploadSecretLength)) {
        return false;
    }

    if (secret.rfind(kAir360UploadSecretPrefix, 0U) != 0U) {
        return false;
    }

    for (std::size_t index = kAir360UploadSecretPrefixLength; index < secret.size();
         ++index) {
        if (!isBase64UrlChar(secret[index])) {
            return false;
        }
    }

    return true;
}

std::string generateAir360UploadSecret() {
    std::array<std::uint8_t, kAir360UploadSecretRandomBytes> random_bytes{};
    esp_fill_random(random_bytes.data(), random_bytes.size());

    std::string encoded;
    if (!encodeBase64UrlNoPadding(random_bytes.data(), random_bytes.size(), encoded)) {
        return "";
    }

    return std::string(kAir360UploadSecretPrefix) + encoded;
}

std::string hashAir360UploadSecret(const std::string& secret) {
    std::array<std::uint8_t, kSha256DigestBytes> digest{};
    if (!sha256Digest(secret, digest)) {
        return "";
    }

    std::string encoded;
    if (!encodeBase64UrlNoPadding(digest.data(), digest.size(), encoded)) {
        return "";
    }

    return std::string("sha256:") + encoded;
}

Air360ApiCredentialRepository::Air360ApiCredentialRepository() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}

esp_err_t Air360ApiCredentialRepository::loadUploadSecret(
    std::string& out_secret,
    bool& found) const {
    out_secret.clear();
    found = false;

    esp_err_t err = ensureCacheLoaded();
    if (err != ESP_OK) {
        return err;
    }

    if (mutex_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    found = cached_upload_secret_found_;
    if (found) {
        out_secret = cached_upload_secret_;
    }
    xSemaphoreGive(mutex_);
    return ESP_OK;
}

esp_err_t Air360ApiCredentialRepository::saveUploadSecret(const std::string& secret) const {
    if (!isValidAir360UploadSecret(secret)) {
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

    err = nvs_set_str(handle, kSecretKey, secret.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        cached_upload_secret_ = secret;
        cached_upload_secret_found_ = true;
        cache_loaded_ = true;
    }
    xSemaphoreGive(mutex_);
    return err;
}

bool Air360ApiCredentialRepository::hasUploadSecret() const {
    std::string secret;
    bool found = false;
    return loadUploadSecret(secret, found) == ESP_OK && found;
}

esp_err_t Air360ApiCredentialRepository::ensureCacheLoaded() const {
    if (mutex_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (cache_loaded_) {
        xSemaphoreGive(mutex_);
        return ESP_OK;
    }

    std::string secret;
    bool found = false;
    const esp_err_t err = loadUploadSecretFromNvs(secret, found);
    if (err == ESP_OK) {
        cached_upload_secret_ = std::move(secret);
        cached_upload_secret_found_ = found;
        cache_loaded_ = true;
    }
    xSemaphoreGive(mutex_);
    return err;
}

esp_err_t Air360ApiCredentialRepository::loadUploadSecretFromNvs(
    std::string& out_secret,
    bool& found) const {
    out_secret.clear();
    found = false;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    std::size_t required = 0U;
    err = nvs_get_str(handle, kSecretKey, nullptr, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    std::string value(required, '\0');
    err = nvs_get_str(handle, kSecretKey, value.data(), &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    if (!isValidAir360UploadSecret(value)) {
        return ESP_ERR_INVALID_STATE;
    }

    out_secret = std::move(value);
    found = true;
    return ESP_OK;
}

}  // namespace air360
