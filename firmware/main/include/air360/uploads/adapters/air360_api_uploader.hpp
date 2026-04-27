#pragma once

#include <atomic>
#include <memory>

#include "air360/uploads/backend_uploader.hpp"

namespace air360 {

class Air360ApiUploader : public IBackendUploader {
  public:
    BackendType type() const override;
    bool validateConfig(const BackendRecord& record, std::string& error) const override;
    bool prepareSync(
        const BackendRecord& record,
        const MeasurementBatch& batch,
        const UploadTransport& transport,
        std::string& error) override;
    bool buildRequests(
        const BackendRecord& record,
        const MeasurementBatch& batch,
        std::vector<UploadRequestSpec>& out_requests,
        std::string& error) const override;
    UploadResultClass classifyResponse(
        const UploadTransportResponse& response) const override;

  private:
    std::atomic<bool> registered_{false};
};

std::unique_ptr<IBackendUploader> createAir360ApiUploader();

}  // namespace air360
