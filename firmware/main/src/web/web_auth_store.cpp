#include "air360/web_auth.hpp"

#include "nvs.h"

namespace air360::web {
namespace {
constexpr char kCredentialKey[] = "web_auth";
}  // namespace

esp_err_t WebAuth::load() {
    sessions.clear();
    nvs_handle_t handle;
    auto err = nvs_open("air360", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        std::size_t size = sizeof(credential_);
        err = nvs_get_blob(handle, kCredentialKey, &credential_, &size);
        if (err == ESP_OK) {
            if (size != sizeof(credential_)) {
                err = ESP_ERR_INVALID_SIZE;
            } else if (credential_.iterations != kPasswordIterations) {
                err = ESP_ERR_INVALID_ARG;
            }
        }
        nvs_close(handle);
    }
    readable_ = err == ESP_OK;
    // The single committed blob is authoritative, even after a power cut.
    // A corrupt/unreadable record locks the UI until physical recovery.
    enabled_ = err != ESP_ERR_NVS_NOT_FOUND;
    return enabled_ ? err : ESP_OK;
}

esp_err_t WebAuth::store(const WebCredential* credential) {
    nvs_handle_t handle;
    auto err = nvs_open("air360", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    if (credential != nullptr) {
        err = nvs_set_blob(handle, kCredentialKey, credential, sizeof(*credential));
    } else {
        err = nvs_erase_key(handle, kCredentialKey);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        credential_ = credential ? *credential : WebCredential{};
        readable_ = credential != nullptr;
        enabled_ = readable_;
        sessions.clear();
    } else {
        // NVS writes may already be visible after a failed commit.
        load();
    }
    return err;
}

bool WebAuth::verify(std::string_view password) const {
    return readable_ && matchesPassword(credential_, password);
}

Access WebAuth::access(std::string_view cookies, std::int64_t now, bool page_request) {
    if (!enabled_ || sessions.touch(sessionCookie(cookies), now)) {
        return Access::allow;
    }
    return page_request ? Access::redirectToLogin : Access::unauthorized;
}

}  // namespace air360::web
