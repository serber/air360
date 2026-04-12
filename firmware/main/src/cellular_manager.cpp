#include "air360/cellular_manager.hpp"

#include "air360/connectivity_checker.hpp"
#include "air360/network_manager.hpp"
#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.cellular";
constexpr std::uint32_t kCheckTimeoutMs = 5000U;
constexpr std::uint32_t kCheckRetries = 3U;

}  // namespace

void CellularManager::init(NetworkManager& network_manager) {
    network_manager_ = &network_manager;
}

const CellularState& CellularManager::state() const {
    return state_;
}

void CellularManager::onPppConnected(const char* ip_address, const char* check_host) {
    state_.ppp_connected = true;
    state_.ip_address = (ip_address != nullptr) ? ip_address : "";
    state_.last_error.clear();

    if (network_manager_ != nullptr) {
        network_manager_->setCellularStatus(true, ip_address);
    }

    ESP_LOGI(kTag, "PPP connected, IP: %s", state_.ip_address.c_str());

    const ConnectivityCheckResult result =
        runConnectivityCheck(check_host, kCheckTimeoutMs, kCheckRetries);

    switch (result) {
        case ConnectivityCheckResult::kSkipped:
            state_.connectivity_check_skipped = true;
            state_.connectivity_ok = false;
            ESP_LOGI(kTag, "Connectivity check skipped (no host configured)");
            break;
        case ConnectivityCheckResult::kOk:
            state_.connectivity_ok = true;
            state_.connectivity_check_skipped = false;
            ESP_LOGI(kTag, "Connectivity check OK (%s)", check_host);
            break;
        case ConnectivityCheckResult::kFailed:
            state_.connectivity_ok = false;
            state_.connectivity_check_skipped = false;
            ESP_LOGW(
                kTag,
                "Connectivity check failed (%s)",
                (check_host != nullptr) ? check_host : "");
            break;
    }
}

void CellularManager::onPppDisconnected(const char* reason) {
    state_.ppp_connected = false;
    state_.connectivity_ok = false;
    state_.ip_address.clear();

    if (network_manager_ != nullptr) {
        network_manager_->setCellularStatus(false, nullptr);
    }

    if (reason != nullptr && reason[0] != '\0') {
        state_.last_error = reason;
        ESP_LOGW(kTag, "PPP disconnected: %s", reason);
    } else {
        ESP_LOGI(kTag, "PPP disconnected");
    }
}

}  // namespace air360
