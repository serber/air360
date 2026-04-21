#include "air360/connectivity_checker.hpp"

#include <algorithm>
#include <cstdint>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.connectivity";
constexpr std::uint32_t kWaitSliceMs = 1000U;

struct PingCtx {
    EventGroupHandle_t done_event = nullptr;
    std::uint32_t recv_count = 0U;
    static constexpr EventBits_t kDoneBit = BIT0;
};

void onPingSuccess(esp_ping_handle_t /*hdl*/, void* args) {
    static_cast<PingCtx*>(args)->recv_count++;
}

void onPingEnd(esp_ping_handle_t /*hdl*/, void* args) {
    auto* ctx = static_cast<PingCtx*>(args);
    xEventGroupSetBits(ctx->done_event, PingCtx::kDoneBit);
}

}  // namespace

ConnectivityCheckResult runConnectivityCheck(
    const char* host,
    std::uint32_t timeout_ms,
    std::uint32_t retries) {
    if (host == nullptr || host[0] == '\0') {
        return ConnectivityCheckResult::kSkipped;
    }

    ip_addr_t target{};
    if (ipaddr_aton(host, &target) != 1) {
        ESP_LOGW(kTag, "Cannot parse \"%s\" as IP address, skipping check", host);
        return ConnectivityCheckResult::kFailed;
    }

    PingCtx ctx;
    ctx.done_event = xEventGroupCreate();
    if (ctx.done_event == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate ping event group");
        return ConnectivityCheckResult::kFailed;
    }

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target;
    ping_config.count = retries;
    ping_config.timeout_ms = timeout_ms;
    ping_config.interval_ms = 1000U;

    esp_ping_callbacks_t cbs{};
    cbs.cb_args = &ctx;
    cbs.on_ping_success = onPingSuccess;
    cbs.on_ping_end = onPingEnd;

    esp_ping_handle_t ping = nullptr;
    const esp_err_t err = esp_ping_new_session(&ping_config, &cbs, &ping);
    if (err != ESP_OK || ping == nullptr) {
        ESP_LOGE(kTag, "esp_ping_new_session failed: %s", esp_err_to_name(err));
        vEventGroupDelete(ctx.done_event);
        return ConnectivityCheckResult::kFailed;
    }

    esp_ping_start(ping);

    // Budget: retries × (timeout + interval) + 1 s margin. Wait in bounded
    // slices so cellular task callers can stay subscribed to the TWDT safely.
    const std::uint32_t wait_budget_ms =
        retries * (timeout_ms + ping_config.interval_ms) + 1000U;
    std::uint32_t waited_ms = 0U;
    while (waited_ms < wait_budget_ms) {
        const std::uint32_t remaining_ms = wait_budget_ms - waited_ms;
        const std::uint32_t slice_ms = std::min(remaining_ms, kWaitSliceMs);
        const EventBits_t bits = xEventGroupWaitBits(
            ctx.done_event, PingCtx::kDoneBit, pdFALSE, pdTRUE,
            pdMS_TO_TICKS(slice_ms));
        (void)esp_task_wdt_reset();
        if ((bits & PingCtx::kDoneBit) != 0U) {
            break;
        }
        waited_ms += slice_ms;
    }

    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
    vEventGroupDelete(ctx.done_event);

    return (ctx.recv_count > 0U)
               ? ConnectivityCheckResult::kOk
               : ConnectivityCheckResult::kFailed;
}

}  // namespace air360
