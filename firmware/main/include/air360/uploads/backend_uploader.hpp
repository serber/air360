#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/measurement_batch.hpp"
#include "air360/uploads/backend_types.hpp"
#include "esp_err.h"

namespace air360 {

class UploadTransport;

enum class UploadAttemptPhase : std::uint8_t {
    kUnknown = 0U,
    kPreflight = 1U,
    kRegistration = 2U,
    kDataUpload = 3U,
};

enum class UploadMethod : std::uint8_t {
    kPost = 1U,
    kPut = 2U,
};

struct UploadRequestHeader {
    std::string name;
    std::string value;
};

struct UploadRequestSpec {
    std::string request_key;
    UploadMethod method = UploadMethod::kPost;
    std::string url;
    std::vector<UploadRequestHeader> headers;
    std::string body;
    int timeout_ms = 15000;
};

struct UploadTransportResponse {
    esp_err_t transport_err = ESP_OK;
    int http_status = 0;
    int response_size = 0;
    std::uint32_t response_time_ms = 0U;
    std::uint32_t connect_time_ms = 0U;
    std::uint32_t request_send_time_ms = 0U;
    std::uint32_t first_response_time_ms = 0U;
    std::uint32_t retry_after_seconds = 0U;
    std::string body_snippet;
};

using BackendStopRequestedFn = bool (*)(void* arg);
using BackendWatchdogResetFn = void (*)(void* arg, const char* checkpoint);

struct BackendDeliveryContext {
    const UploadTransport* http_transport = nullptr;
    BackendStopRequestedFn stop_requested = nullptr;
    BackendWatchdogResetFn reset_watchdog = nullptr;
    void* callback_arg = nullptr;

    bool stopRequested() const {
        return stop_requested != nullptr && stop_requested(callback_arg);
    }

    void resetWatchdog(const char* checkpoint) const {
        if (reset_watchdog != nullptr) {
            reset_watchdog(callback_arg, checkpoint);
        }
    }
};

struct UploadAttemptResult {
    UploadResultClass result = UploadResultClass::kUnknown;
    UploadAttemptPhase phase = UploadAttemptPhase::kUnknown;
    esp_err_t transport_err = ESP_OK;
    int status_code = 0;
    std::uint32_t response_time_ms = 0U;
    std::uint32_t retry_after_seconds = 0U;
    std::string message;

    bool acknowledgesWindow() const {
        return result == UploadResultClass::kSuccess ||
               result == UploadResultClass::kNoData;
    }
};

class IBackendUploader {
  public:
    virtual ~IBackendUploader() = default;

    virtual BackendType type() const = 0;
    virtual bool validateConfig(const BackendRecord& record, std::string& error) const = 0;
    virtual UploadAttemptResult deliver(
        const BackendRecord& record,
        const MeasurementBatch& batch,
        const BackendDeliveryContext& context) = 0;
};

}  // namespace air360
