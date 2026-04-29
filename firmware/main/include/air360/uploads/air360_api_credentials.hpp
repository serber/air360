#pragma once

#include <cstddef>
#include <string>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace air360 {

inline constexpr char kAir360UploadSecretPrefix[] = "air360_us_v1_";
constexpr std::size_t kAir360UploadSecretPrefixLength =
    sizeof(kAir360UploadSecretPrefix) - 1U;
constexpr std::size_t kAir360UploadSecretRandomBytes = 32U;
constexpr std::size_t kAir360UploadSecretEncodedChars = 43U;
constexpr std::size_t kAir360UploadSecretLength =
    kAir360UploadSecretPrefixLength + kAir360UploadSecretEncodedChars;

[[nodiscard]] bool isValidAir360UploadSecret(const std::string& secret);
[[nodiscard]] std::string generateAir360UploadSecret();
[[nodiscard]] std::string hashAir360UploadSecret(const std::string& secret);

class Air360ApiCredentialRepository {
  public:
    Air360ApiCredentialRepository();

    [[nodiscard]] esp_err_t loadUploadSecret(std::string& out_secret, bool& found) const;
    [[nodiscard]] esp_err_t saveUploadSecret(const std::string& secret) const;
    [[nodiscard]] bool hasUploadSecret() const;

  private:
    [[nodiscard]] esp_err_t ensureCacheLoaded() const;
    [[nodiscard]] esp_err_t loadUploadSecretFromNvs(
        std::string& out_secret,
        bool& found) const;

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    mutable bool cache_loaded_ = false;
    mutable bool cached_upload_secret_found_ = false;
    mutable std::string cached_upload_secret_;
};

}  // namespace air360
