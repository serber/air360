#pragma once

#include "air360/uploads/backend_uploader.hpp"

namespace air360 {

class SensorCommunityUploader : public IBackendUploader {
  public:
    BackendType type() const override;
    const char* backendKey() const override;
    bool validateConfig(const BackendRecord& record, std::string& error) const override;
    bool buildRequests(
        const BackendRecord& record,
        const MeasurementBatch& batch,
        std::vector<UploadRequestSpec>& out_requests,
        std::string& error) const override;
    UploadResultClass classifyResponse(const UploadTransportResponse& response) const override;
};

std::unique_ptr<IBackendUploader> createSensorCommunityUploader();

}  // namespace air360
