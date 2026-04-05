#pragma once

#include "air360/uploads/backend_uploader.hpp"

namespace air360 {

class UploadTransport {
  public:
    UploadTransportResponse execute(const UploadRequestSpec& request) const;
};

}  // namespace air360
