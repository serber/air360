#pragma once

#include <atomic>
#include <memory>

#include "air360/uploads/backend_uploader.hpp"

namespace air360 {

class Air360ApiUploader : public IBackendUploader {
  public:
    BackendType type() const override;
    bool validateConfig(const BackendRecord& record, std::string& error) const override;
    UploadAttemptResult deliver(
        const BackendRecord& record,
        const MeasurementBatch& batch,
        const BackendDeliveryContext& context) override;

  private:
    UploadAttemptResult prepareSync(
        const BackendRecord& record,
        const MeasurementBatch& batch,
        const BackendDeliveryContext& context,
        const std::string& upload_secret_hash);
    bool buildRequests(
        const BackendRecord& record,
        const MeasurementBatch& batch,
        const std::string& upload_secret,
        std::vector<UploadRequestSpec>& out_requests,
        std::string& error) const;
    UploadResultClass classifyResponse(
        const UploadTransportResponse& response) const;
    std::atomic<bool> registered_{false};
};

std::unique_ptr<IBackendUploader> createAir360ApiUploader();

}  // namespace air360
