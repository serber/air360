#include "air360/status_service.hpp"

#include <cinttypes>
#include <cstdio>
#include <string>
#include <utility>

#include "esp_timer.h"

namespace air360 {

namespace {

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

std::string jsonEscape(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size());

    for (const char ch : input) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

const char* boolString(bool value) {
    return value ? "true" : "false";
}

std::uint64_t uptimeMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
}

}  // namespace

StatusService::StatusService(BuildInfo build_info) : build_info_(std::move(build_info)) {}

void StatusService::markNvsReady(bool ready) {
    nvs_ready_ = ready;
}

void StatusService::markWatchdogArmed(bool armed) {
    watchdog_armed_ = armed;
}

void StatusService::setConfig(
    const DeviceConfig& config,
    bool loaded_from_storage,
    bool wrote_defaults) {
    config_ = config;
    config_loaded_from_storage_ = loaded_from_storage;
    wrote_default_config_ = wrote_defaults;
}

void StatusService::setBootCount(std::uint32_t boot_count) {
    boot_count_ = boot_count;
}

void StatusService::setNetworkState(const NetworkState& state) {
    network_state_ = state;
}

void StatusService::setWebServerStarted(bool started) {
    web_server_started_ = started;
}

std::string StatusService::renderRootHtml() const {
    std::string html;
    html.reserve(1200);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>air360 phase 1</title>";
    html += "<style>body{font-family:system-ui,sans-serif;margin:2rem;max-width:52rem;line-height:1.5}";
    html += "code{background:#f3f4f6;padding:.1rem .35rem;border-radius:.25rem}";
    html += "pre{background:#111827;color:#f9fafb;padding:1rem;border-radius:.5rem;overflow:auto}";
    html += "a{color:#0f766e}</style></head><body>";
    html += "<h1>air360 Phase 1 Runtime</h1>";
    html += "<p>Board: <code>" + htmlEscape(build_info_.board_name) + "</code></p>";
    html += "<p>Device: <code>" + htmlEscape(config_.device_name) + "</code></p>";
    html += "<p>Status endpoint: <a href='/status'>/status</a></p>";
    html += "<pre>";
    html += "project: " + htmlEscape(build_info_.project_name) + "\n";
    html += "version: " + htmlEscape(build_info_.project_version) + "\n";
    html += "idf: " + htmlEscape(build_info_.idf_version) + "\n";
    html += "boot_count: " + std::to_string(boot_count_) + "\n";
    html += "config_source: ";
    html += (config_loaded_from_storage_ ? "stored" : "defaults");
    html += "\nlab_ap_active: ";
    html += (network_state_.lab_ap_active ? "true" : "false");
    html += "\nlab_ap_ip: ";
    html += htmlEscape(network_state_.ip_address);
    html += "\n";
    html += "</pre></body></html>";
    return html;
}

std::string StatusService::renderStatusJson() const {
    const std::string project_name = jsonEscape(build_info_.project_name);
    const std::string project_version = jsonEscape(build_info_.project_version);
    const std::string idf_version = jsonEscape(build_info_.idf_version);
    const std::string board_name = jsonEscape(build_info_.board_name);
    const std::string compile_date = jsonEscape(build_info_.compile_date);
    const std::string compile_time = jsonEscape(build_info_.compile_time);
    const std::string device_name = jsonEscape(config_.device_name);
    const std::string lab_ap_ip = jsonEscape(network_state_.ip_address);

    char buffer[1400];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "{"
        "\"project_name\":\"%s\","
        "\"project_version\":\"%s\","
        "\"idf_version\":\"%s\","
        "\"board_name\":\"%s\","
        "\"compile_date\":\"%s\","
        "\"compile_time\":\"%s\","
        "\"device_name\":\"%s\","
        "\"boot_count\":%" PRIu32 ","
        "\"uptime_ms\":%" PRIu64 ","
        "\"reset_reason\":%d,"
        "\"nvs_ready\":%s,"
        "\"watchdog_armed\":%s,"
        "\"config_loaded_from_storage\":%s,"
        "\"wrote_default_config\":%s,"
        "\"web_server_started\":%s,"
        "\"http_port\":%" PRIu16 ","
        "\"lab_ap_enabled\":%s,"
        "\"lab_ap_active\":%s,"
        "\"lab_ap_ip\":\"%s\""
        "}",
        project_name.c_str(),
        project_version.c_str(),
        idf_version.c_str(),
        board_name.c_str(),
        compile_date.c_str(),
        compile_time.c_str(),
        device_name.c_str(),
        boot_count_,
        uptimeMilliseconds(),
        static_cast<int>(reset_reason_),
        boolString(nvs_ready_),
        boolString(watchdog_armed_),
        boolString(config_loaded_from_storage_),
        boolString(wrote_default_config_),
        boolString(web_server_started_),
        config_.http_port,
        boolString(config_.lab_ap_enabled != 0U),
        boolString(network_state_.lab_ap_active),
        lab_ap_ip.c_str());
    return buffer;
}

}  // namespace air360
