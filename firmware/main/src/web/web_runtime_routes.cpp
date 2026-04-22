#include "air360/web_server.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#include "air360/log_buffer.hpp"
#include "air360/string_utils.hpp"
#include "air360/web_assets.hpp"
#include "air360/web_server_internal.hpp"

namespace air360 {

namespace {

std::string_view assetPathFromUri(const char* uri) {
    if (uri == nullptr) {
        return {};
    }

    std::string_view path(uri);
    constexpr std::string_view kPrefix = "/assets/";
    if (path.size() < kPrefix.size() || path.substr(0, kPrefix.size()) != kPrefix) {
        return {};
    }

    path.remove_prefix(kPrefix.size());
    const std::size_t query = path.find('?');
    if (query != std::string_view::npos) {
        path = path.substr(0, query);
    }
    return path;
}

esp_err_t sendAssetResponse(httpd_req_t* request, std::string_view asset_path) {
    const WebAssetView* asset = findEmbeddedWebAsset(asset_path);
    if (asset == nullptr || asset->data == nullptr) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Asset not found");
        return ESP_ERR_NOT_FOUND;
    }

    httpd_resp_set_type(request, asset->content_type);
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_send(request, asset->data, asset->size);
}

}  // namespace

esp_err_t WebServer::handleAsset(httpd_req_t* request) {
    return sendAssetResponse(request, assetPathFromUri(request->uri));
}

esp_err_t WebServer::handleRoot(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    if (server->status_service_->networkState().mode == NetworkMode::kSetupAp) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", "/config");
        return httpd_resp_send(request, nullptr, 0);
    }

    const std::string html = server->status_service_->renderRootHtml();
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, html.c_str(), html.size());
}

esp_err_t WebServer::handleDiagnostics(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const std::string html = server->status_service_->renderDiagnosticsHtml(logBufferGetContents());
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, html.c_str(), html.size());
}

esp_err_t WebServer::handleLogsData(httpd_req_t* request) {
    const std::string contents = logBufferGetContents();
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, contents.c_str(), contents.size());
}

esp_err_t WebServer::handleWifiScan(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    WifiScanSnapshot scan_snapshot = server->network_manager_->wifiScanSnapshot();
    if (scan_snapshot.last_scan_uptime_ms == 0U) {
        server->network_manager_->scanAvailableNetworks();
        scan_snapshot = server->network_manager_->wifiScanSnapshot();
    }

    std::string json;
    json.reserve(1024U);
    json += "{";
    json += "\"networks\":[";
    for (std::size_t index = 0; index < scan_snapshot.networks.size(); ++index) {
        if (index > 0U) {
            json += ",";
        }

        json += "{";
        json += "\"ssid\":\"";
        json += jsonEscape(scan_snapshot.networks[index].ssid);
        json += "\",\"rssi\":";
        json += std::to_string(scan_snapshot.networks[index].rssi);
        json += "}";
    }
    json += "],\"last_scan_uptime_ms\":";
    json += std::to_string(scan_snapshot.last_scan_uptime_ms);
    json += ",\"last_scan_error\":\"";
    json += jsonEscape(scan_snapshot.last_scan_error);
    json += "\"}";
    return httpd_resp_send(request, json.c_str(), json.size());
}

esp_err_t WebServer::handleCheckSntp(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    std::string body;
    const esp_err_t body_err = web::readRequestBody(request, body);
    if (body_err == ESP_ERR_INVALID_SIZE) {
        return web::sendRequestBodyTooLarge(request);
    }
    if (body_err != ESP_OK) {
        return httpd_resp_sendstr(request, "{\"success\":false,\"error\":\"sync_failed\"}");
    }

    const web::FormFields fields = web::parseFormBody(body);
    const std::string sntp_server = web::findFormValue(fields, "server");

    const SntpCheckResult result = server->network_manager_->checkSntp(sntp_server);

    if (result.success) {
        return httpd_resp_sendstr(request, "{\"success\":true}");
    }

    std::string response;
    response.reserve(256U);
    response += "{\"success\":false,\"error\":\"";
    response += jsonEscape(result.error);
    response += "\"}";
    return httpd_resp_sendstr(request, response.c_str());
}

}  // namespace air360
