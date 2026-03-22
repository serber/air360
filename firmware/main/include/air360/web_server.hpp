#pragma once

#include <cstdint>

#include "air360/status_service.hpp"
#include "esp_err.h"
#include "esp_http_server.h"

namespace air360 {

class WebServer {
  public:
    esp_err_t start(StatusService& status_service, std::uint16_t port);
    void stop();

  private:
    static esp_err_t handleRoot(httpd_req_t* request);
    static esp_err_t handleStatus(httpd_req_t* request);

    httpd_handle_t handle_ = nullptr;
    StatusService* status_service_ = nullptr;
};

}  // namespace air360
