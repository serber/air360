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
    std::string body_snippet;
};

class IBackendUploader {
  public:
    virtual ~IBackendUploader() = default;

    virtual BackendType type() const = 0;
    virtual bool validateConfig(const BackendRecord& record, std::string& error) const = 0;
    virtual bool buildRequests(
        const BackendRecord& record,
        const MeasurementBatch& batch,
        std::vector<UploadRequestSpec>& out_requests,
        std::string& error) const = 0;
    virtual UploadResultClass classifyResponse(
        const UploadTransportResponse& response) const = 0;
};

}  // namespace air360
