#include "air360/web_server.hpp"

#include <cinttypes>
#include <string>

#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.web";

}  // namespace

esp_err_t WebServer::start(StatusService& status_service, std::uint16_t port) {
    if (handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    status_service_ = &status_service;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;

    esp_err_t err = httpd_start(&handle_, &config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t root_uri{};
    root_uri.uri = "/";
    root_uri.method = HTTP_GET;
    root_uri.handler = &WebServer::handleRoot;
    root_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &root_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t status_uri{};
    status_uri.uri = "/status";
    status_uri.method = HTTP_GET;
    status_uri.handler = &WebServer::handleStatus;
    status_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &status_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    ESP_LOGI(kTag, "HTTP server listening on port %" PRIu16, port);
    return ESP_OK;
}

void WebServer::stop() {
    if (handle_ != nullptr) {
        httpd_stop(handle_);
        handle_ = nullptr;
    }
}

esp_err_t WebServer::handleRoot(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const std::string html = server->status_service_->renderRootHtml();
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, html.c_str(), html.size());
}

esp_err_t WebServer::handleStatus(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const std::string json = server->status_service_->renderStatusJson();
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json.c_str(), json.size());
}

}  // namespace air360
