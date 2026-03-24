#include "air360/web_server.hpp"

#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.web";

void copyString(char* destination, std::size_t destination_size, const std::string& source) {
    if (destination_size == 0U) {
        return;
    }

    std::strncpy(destination, source.c_str(), destination_size - 1U);
    destination[destination_size - 1U] = '\0';
}

std::string htmlEscape(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size());

    for (const char ch : input) {
        switch (ch) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

int decodeHex(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

std::string urlDecode(const std::string& input) {
    std::string decoded;
    decoded.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (ch == '%' && (i + 2U) < input.size()) {
            const int hi = decodeHex(input[i + 1U]);
            const int lo = decodeHex(input[i + 2U]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2U;
                continue;
            }
        }

        decoded.push_back(ch);
    }

    return decoded;
}

using FormFields = std::vector<std::pair<std::string, std::string>>;

FormFields parseFormBody(const std::string& body) {
    FormFields fields;
    std::size_t cursor = 0U;

    while (cursor <= body.size()) {
        const std::size_t delimiter = body.find('&', cursor);
        const std::size_t end = delimiter == std::string::npos ? body.size() : delimiter;
        const std::size_t equals = body.find('=', cursor);

        if (equals != std::string::npos && equals < end) {
            fields.emplace_back(
                urlDecode(body.substr(cursor, equals - cursor)),
                urlDecode(body.substr(equals + 1U, end - equals - 1U)));
        } else if (end > cursor) {
            fields.emplace_back(urlDecode(body.substr(cursor, end - cursor)), "");
        }

        if (delimiter == std::string::npos) {
            break;
        }
        cursor = delimiter + 1U;
    }

    return fields;
}

std::string findFormValue(const FormFields& fields, const char* key) {
    for (const auto& [name, value] : fields) {
        if (name == key) {
            return value;
        }
    }
    return "";
}

bool formHasKey(const FormFields& fields, const char* key) {
    for (const auto& [name, value] : fields) {
        if (name == key) {
            static_cast<void>(value);
            return true;
        }
    }
    return false;
}

esp_err_t readRequestBody(httpd_req_t* request, std::string& out_body) {
    out_body.clear();
    if (request->content_len <= 0) {
        return ESP_OK;
    }

    out_body.resize(static_cast<std::size_t>(request->content_len));
    int received_total = 0;
    while (received_total < request->content_len) {
        const int received = httpd_req_recv(
            request,
            out_body.data() + received_total,
            request->content_len - received_total);
        if (received <= 0) {
            return ESP_FAIL;
        }
        received_total += received;
    }

    return ESP_OK;
}

const char* networkModeLabel(NetworkMode mode) {
    switch (mode) {
        case NetworkMode::kSetupAp:
            return "setup_ap";
        case NetworkMode::kStation:
            return "station";
        case NetworkMode::kOffline:
        default:
            return "offline";
    }
}

std::string renderConfigPage(
    const DeviceConfig& config,
    const NetworkState& network_state,
    const std::string& notice,
    bool error_notice) {
    std::string html;
    html.reserve(5000);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>air360 config</title>";
    html += "<style>";
    html += "body{font-family:system-ui,sans-serif;margin:2rem;max-width:56rem;line-height:1.5}";
    html += "label{display:block;margin-top:1rem;font-weight:600}";
    html += "input{width:100%;max-width:30rem;padding:.6rem;border:1px solid #cbd5e1;border-radius:.4rem}";
    html += "button{margin-top:1.5rem;padding:.7rem 1.2rem;border:0;border-radius:.5rem;background:#0f766e;color:white;font-weight:700}";
    html += ".card{background:#f8fafc;border:1px solid #e2e8f0;border-radius:.75rem;padding:1rem 1.25rem}";
    html += ".notice{margin:1rem 0;padding:.8rem 1rem;border-radius:.5rem}";
    html += ".ok{background:#ecfdf5;color:#166534}.err{background:#fef2f2;color:#991b1b}";
    html += "code{background:#f1f5f9;padding:.1rem .35rem;border-radius:.25rem}";
    html += "</style></head><body>";
    html += "<h1>air360 Configuration</h1>";
    html += "<p>Mode: <code>";
    html += networkModeLabel(network_state.mode);
    html += "</code> · IP: <code>";
    html += htmlEscape(network_state.ip_address.empty() ? "unavailable" : network_state.ip_address);
    html += "</code></p>";
    if (!network_state.last_error.empty()) {
        html += "<p>Last network error: <code>";
        html += htmlEscape(network_state.last_error);
        html += "</code></p>";
    }

    if (!notice.empty()) {
        html += "<div class='notice ";
        html += error_notice ? "err" : "ok";
        html += "'>";
        html += htmlEscape(notice);
        html += "</div>";
    }

    html += "<div class='card'>";
    html += "<p>This Phase 2 form stores Wi-Fi credentials and basic local device settings. ";
    html += "After save, the firmware reboots and either joins Wi-Fi in station mode or falls back to setup AP mode.</p>";
    html += "<form method='POST' action='/config'>";
    html += "<label for='device_name'>Device name</label>";
    html += "<input id='device_name' name='device_name' maxlength='31' value='";
    html += htmlEscape(config.device_name);
    html += "'>";

    html += "<label for='wifi_ssid'>Wi-Fi SSID</label>";
    html += "<input id='wifi_ssid' name='wifi_ssid' maxlength='32' value='";
    html += htmlEscape(config.wifi_sta_ssid);
    html += "'>";

    html += "<label for='wifi_password'>Wi-Fi password</label>";
    html += "<input id='wifi_password' name='wifi_password' type='password' maxlength='63' value='";
    html += htmlEscape(config.wifi_sta_password);
    html += "'>";
    html += "<p>If Wi-Fi SSID is left empty, the device will reboot back into setup AP mode.</p>";

    html += "<label for='ap_ssid'>Setup AP name</label>";
    html += "<input id='ap_ssid' name='ap_ssid' maxlength='32' value='";
    html += htmlEscape(config.lab_ap_ssid);
    html += "'>";

    html += "<label for='ap_password'>Setup AP password</label>";
    html += "<input id='ap_password' name='ap_password' type='password' maxlength='63' value='";
    html += htmlEscape(config.lab_ap_password);
    html += "'>";

    html += "<label><input name='local_auth_enabled' type='checkbox' ";
    if (config.local_auth_enabled != 0U) {
        html += "checked ";
    }
    html += "style='width:auto;max-width:none;margin-right:.5rem'>Local auth placeholder (stored only, not enforced yet)</label>";
    html += "<button type='submit'>Save and reboot</button>";
    html += "</form></div>";
    html += "<p><a href='/'>Back to root</a> · <a href='/status'>JSON status</a></p>";
    html += "</body></html>";
    return html;
}

bool validateConfigForm(
    const std::string& device_name,
    const std::string& wifi_ssid,
    const std::string& wifi_password,
    const std::string& ap_ssid,
    const std::string& ap_password,
    std::string& error) {
    if (device_name.empty()) {
        error = "Device name must not be empty.";
        return false;
    }
    if (device_name.size() > 31U) {
        error = "Device name is too long.";
        return false;
    }
    if (wifi_ssid.size() > 32U) {
        error = "Wi-Fi SSID is too long.";
        return false;
    }
    if (wifi_password.size() > 63U) {
        error = "Wi-Fi password is too long.";
        return false;
    }
    if (!wifi_password.empty() && wifi_password.size() < 8U) {
        error = "Wi-Fi password must be empty or at least 8 characters.";
        return false;
    }
    if (ap_ssid.empty()) {
        error = "Setup AP name must not be empty.";
        return false;
    }
    if (ap_ssid.size() > 32U) {
        error = "Setup AP name is too long.";
        return false;
    }
    if (ap_password.size() > 63U) {
        error = "Setup AP password is too long.";
        return false;
    }
    if (!ap_password.empty() && ap_password.size() < 8U) {
        error = "Setup AP password must be empty or at least 8 characters.";
        return false;
    }

    error.clear();
    return true;
}

void restartCallback(void* arg) {
    static_cast<void>(arg);
    esp_restart();
}

void scheduleRestart() {
    static esp_timer_handle_t restart_timer = nullptr;
    if (restart_timer == nullptr) {
        esp_timer_create_args_t args{};
        args.callback = &restartCallback;
        args.name = "air360_reboot";
        ESP_ERROR_CHECK(esp_timer_create(&args, &restart_timer));
    }

    const esp_err_t stop_err = esp_timer_stop(restart_timer);
    if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "Failed to stop restart timer: %s", esp_err_to_name(stop_err));
    }

    ESP_ERROR_CHECK(esp_timer_start_once(restart_timer, 400000));
}

}  // namespace

esp_err_t WebServer::start(
    StatusService& status_service,
    ConfigRepository& config_repository,
    DeviceConfig& config,
    std::uint16_t port) {
    if (handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    status_service_ = &status_service;
    config_repository_ = &config_repository;
    config_ = &config;

    httpd_config_t config_httpd = HTTPD_DEFAULT_CONFIG();
    config_httpd.server_port = port;

    esp_err_t err = httpd_start(&handle_, &config_httpd);
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

    httpd_uri_t config_get_uri{};
    config_get_uri.uri = "/config";
    config_get_uri.method = HTTP_GET;
    config_get_uri.handler = &WebServer::handleConfig;
    config_get_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &config_get_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t config_post_uri{};
    config_post_uri.uri = "/config";
    config_post_uri.method = HTTP_POST;
    config_post_uri.handler = &WebServer::handleConfig;
    config_post_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &config_post_uri);
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

esp_err_t WebServer::handleStatus(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const std::string json = server->status_service_->renderStatusJson();
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json.c_str(), json.size());
}

esp_err_t WebServer::handleConfig(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderConfigPage(
            *server->config_,
            server->status_service_->networkState(),
            "",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    std::string body;
    if (readRequestBody(request, body) != ESP_OK) {
        const std::string html = renderConfigPage(
            *server->config_,
            server->status_service_->networkState(),
            "Failed to read form body.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    const FormFields fields = parseFormBody(body);
    const std::string device_name = findFormValue(fields, "device_name");
    const std::string wifi_ssid = findFormValue(fields, "wifi_ssid");
    const std::string wifi_password = findFormValue(fields, "wifi_password");
    const std::string ap_ssid = findFormValue(fields, "ap_ssid");
    const std::string ap_password = findFormValue(fields, "ap_password");

    std::string validation_error;
    if (!validateConfigForm(
            device_name,
            wifi_ssid,
            wifi_password,
            ap_ssid,
            ap_password,
            validation_error)) {
        DeviceConfig preview = *server->config_;
        copyString(preview.device_name, sizeof(preview.device_name), device_name);
        copyString(preview.wifi_sta_ssid, sizeof(preview.wifi_sta_ssid), wifi_ssid);
        copyString(preview.wifi_sta_password, sizeof(preview.wifi_sta_password), wifi_password);
        copyString(preview.lab_ap_ssid, sizeof(preview.lab_ap_ssid), ap_ssid);
        copyString(preview.lab_ap_password, sizeof(preview.lab_ap_password), ap_password);
        preview.local_auth_enabled = formHasKey(fields, "local_auth_enabled") ? 1U : 0U;

        const std::string html = renderConfigPage(
            preview,
            server->status_service_->networkState(),
            validation_error,
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    DeviceConfig updated = *server->config_;
    updated.magic = kDeviceConfigMagic;
    updated.schema_version = kDeviceConfigSchemaVersion;
    updated.record_size = static_cast<std::uint16_t>(sizeof(DeviceConfig));
    updated.local_auth_enabled = formHasKey(fields, "local_auth_enabled") ? 1U : 0U;
    copyString(updated.device_name, sizeof(updated.device_name), device_name);
    copyString(updated.wifi_sta_ssid, sizeof(updated.wifi_sta_ssid), wifi_ssid);
    copyString(updated.wifi_sta_password, sizeof(updated.wifi_sta_password), wifi_password);
    copyString(updated.lab_ap_ssid, sizeof(updated.lab_ap_ssid), ap_ssid);
    copyString(updated.lab_ap_password, sizeof(updated.lab_ap_password), ap_password);

    const esp_err_t save_err = server->config_repository_->save(updated);
    if (save_err != ESP_OK) {
        const std::string html = renderConfigPage(
            updated,
            server->status_service_->networkState(),
            std::string("Failed to save configuration: ") + esp_err_to_name(save_err),
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    *server->config_ = updated;
    server->status_service_->setConfig(updated, true, false);
    const std::string html = renderConfigPage(
        updated,
        server->status_service_->networkState(),
        "Configuration saved. Device is rebooting now.",
        false);
    esp_err_t response_err = httpd_resp_send(request, html.c_str(), html.size());
    if (response_err == ESP_OK) {
        scheduleRestart();
    }
    return response_err;
}

}  // namespace air360
