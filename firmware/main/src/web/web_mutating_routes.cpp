#include "air360/web_server.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "air360/sensors/sensor_registry.hpp"
#include "air360/string_utils.hpp"
#include "air360/uploads/backend_registry.hpp"
#include "air360/web_server_internal.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.web";

using web::FormFields;
using web::defaultBoardGpioPin;
using web::findFormValue;
using web::formHasKey;
using web::inferredTransportKind;
using web::parseFormBody;
using web::parseI2cAddress;
using web::parseSignedLong;
using web::parseUnsignedLong;
using web::readRequestBody;
using web::renderBackendsPage;
using web::renderConfigPage;
using web::renderSensorsPage;
using web::sendHtmlResponse;
using web::sendRequestBodyTooLarge;
using web::validateConfigForm;
using web::validateSensorCategorySelection;

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

esp_err_t WebServer::handleSensors(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    if (server->status_service_->networkState().mode == NetworkMode::kSetupAp) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", "/config");
        return httpd_resp_send(request, nullptr, 0);
    }

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
            server->has_pending_sensor_changes_,
            "",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    std::string body;
    const esp_err_t body_err = readRequestBody(request, body);
    if (body_err == ESP_ERR_INVALID_SIZE) {
        return sendRequestBodyTooLarge(request);
    }
    if (body_err != ESP_OK) {
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
            server->has_pending_sensor_changes_,
            "Failed to read form body.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    const FormFields fields = parseFormBody(body);
    const std::string action = findFormValue(fields, "action");
    SensorConfigList updated = server->staged_sensor_config_;
    SensorRegistry registry;

    if (action == "apply") {
        const esp_err_t save_err = server->sensor_config_repository_->save(server->staged_sensor_config_);
        if (save_err != ESP_OK) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                std::string("Failed to save sensor configuration: ") + esp_err_to_name(save_err),
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        *server->sensor_config_list_ = server->staged_sensor_config_;
        const esp_err_t apply_err =
            server->sensor_manager_->applyConfig(*server->sensor_config_list_);
        server->has_pending_sensor_changes_ = false;
        if (apply_err != ESP_OK) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                std::string("Sensor configuration saved, but runtime apply failed: ") +
                    esp_err_to_name(apply_err) + ". Reboot to apply it.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
            server->has_pending_sensor_changes_,
            "Sensor configuration saved and applied.",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    } else if (action == "discard") {
        server->staged_sensor_config_ = *server->sensor_config_list_;
        server->has_pending_sensor_changes_ = false;
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
            server->has_pending_sensor_changes_,
            "Pending sensor changes discarded.",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    } else if (action == "delete") {
        unsigned long sensor_id = 0UL;
        if (!parseUnsignedLong(findFormValue(fields, "sensor_id"), sensor_id) ||
            !eraseSensorRecordById(updated, static_cast<std::uint32_t>(sensor_id))) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "Failed to delete sensor: invalid sensor id.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
    } else if (action == "add" || action == "update") {
        const std::string sensor_type_value = findFormValue(fields, "sensor_type");
        const SensorDescriptor* descriptor = registry.findByTypeKey(sensor_type_value);
        if (descriptor == nullptr) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "Unsupported sensor type.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        unsigned long poll_interval_ms = 0UL;
        if (!parseUnsignedLong(findFormValue(fields, "poll_interval_ms"), poll_interval_ms)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "Invalid numeric sensor fields.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
        if (poll_interval_ms < web::kMinSensorPollIntervalMs) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "Poll interval must be at least 5000 ms.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        const std::string analog_gpio_pin_value = findFormValue(fields, "analog_gpio_pin");
        std::int16_t analog_pin = -1;
        long parsed_signed = -1;
        if (!analog_gpio_pin_value.empty() &&
            !parseSignedLong(analog_gpio_pin_value, parsed_signed)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "Sensor pin must be a valid integer.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
        analog_pin = static_cast<std::int16_t>(parsed_signed);

        const std::string i2c_address_value = findFormValue(fields, "i2c_address");
        std::uint8_t parsed_i2c_address = 0U;
        if (!i2c_address_value.empty() &&
            !parseI2cAddress(i2c_address_value, parsed_i2c_address)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "I2C address must be a valid value like 0x76.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        SensorRecord record{};
        const SensorRecord* existing = nullptr;
        if (action == "update") {
            unsigned long sensor_id = 0UL;
            if (!parseUnsignedLong(findFormValue(fields, "sensor_id"), sensor_id)) {
                const std::string html = renderSensorsPage(
                    server->staged_sensor_config_,
                    *server->sensor_manager_,
                    *server->measurement_store_,
                    server->has_pending_sensor_changes_,
                    "Invalid sensor id.",
                    true);
                return httpd_resp_send(request, html.c_str(), html.size());
            }

            existing = findSensorRecordById(updated, static_cast<std::uint32_t>(sensor_id));
            if (existing == nullptr) {
                const std::string html = renderSensorsPage(
                    server->staged_sensor_config_,
                    *server->sensor_manager_,
                    *server->measurement_store_,
                    server->has_pending_sensor_changes_,
                    "Sensor not found.",
                    true);
                return httpd_resp_send(request, html.c_str(), html.size());
            }

            record = *existing;
            record.id = static_cast<std::uint32_t>(sensor_id);
        }

        const bool type_changed = existing == nullptr || existing->sensor_type != descriptor->type;
        if (type_changed) {
            const std::uint32_t preserved_id = record.id;
            const std::uint8_t preserved_enabled = formHasKey(fields, "enabled") ? 1U : 0U;
            SensorRecord rebuilt{};
            rebuilt.id = preserved_id;
            rebuilt.enabled = preserved_enabled;
            rebuilt.analog_gpio_pin = defaultBoardGpioPin();
            record = rebuilt;
        }

        record.enabled = formHasKey(fields, "enabled") ? 1U : 0U;
        record.sensor_type = descriptor->type;
        record.poll_interval_ms = static_cast<std::uint32_t>(poll_interval_ms);

        record.transport_kind = inferredTransportKind(*descriptor);
        switch (record.transport_kind) {
            case TransportKind::kI2c:
                if (type_changed) {
                    record.i2c_bus_id = descriptor->default_i2c_bus_id;
                    record.i2c_address = descriptor->default_i2c_address;
                }
                if (!i2c_address_value.empty()) {
                    record.i2c_address = parsed_i2c_address;
                }
                break;
            case TransportKind::kUart:
                record.uart_port_id = descriptor->default_uart_port_id;
                record.uart_rx_gpio_pin = descriptor->default_uart_rx_gpio_pin;
                record.uart_tx_gpio_pin = descriptor->default_uart_tx_gpio_pin;
                if (type_changed || record.uart_baud_rate == 0U) {
                    record.uart_baud_rate = descriptor->default_uart_baud_rate;
                }
                break;
            case TransportKind::kGpio:
            case TransportKind::kAnalog:
                if (!analog_gpio_pin_value.empty()) {
                    record.analog_gpio_pin = analog_pin;
                } else if (type_changed || record.analog_gpio_pin < 0) {
                    record.analog_gpio_pin = defaultBoardGpioPin();
                }
                break;
            case TransportKind::kUnknown:
            default:
                break;
        }

        if (action == "add") {
            if (updated.sensor_count >= kMaxConfiguredSensors) {
                const std::string html = renderSensorsPage(
                    server->staged_sensor_config_,
                    *server->sensor_manager_,
                    *server->measurement_store_,
                    server->has_pending_sensor_changes_,
                    "Sensor list is full.",
                    true);
                return httpd_resp_send(request, html.c_str(), html.size());
            }
            record.id = updated.next_sensor_id++;
            updated.sensors[updated.sensor_count++] = record;
        } else {
            *findSensorRecordById(updated, record.id) = record;
        }

        std::string validation_error;
        if (!registry.validateRecord(record, validation_error)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                validation_error,
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        if (!validateSensorCategorySelection(updated, record, validation_error)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                validation_error,
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
    } else {
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
            server->has_pending_sensor_changes_,
            "Unsupported sensor action.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    server->staged_sensor_config_ = updated;
    server->has_pending_sensor_changes_ = true;
    const std::string html = renderSensorsPage(
        server->staged_sensor_config_,
        *server->sensor_manager_,
        *server->measurement_store_,
        server->has_pending_sensor_changes_,
        action == "delete" ? "Sensor deletion staged." : "Sensor changes staged in memory.",
        false);
    return httpd_resp_send(request, html.c_str(), html.size());
}

esp_err_t WebServer::handleBackends(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    if (server->status_service_->networkState().mode == NetworkMode::kSetupAp) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", "/config");
        return httpd_resp_send(request, nullptr, 0);
    }

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            server->status_service_->buildInfo(),
            "",
            false);
        return sendHtmlResponse(request, html);
    }

    std::string body;
    const esp_err_t body_err = readRequestBody(request, body);
    if (body_err == ESP_ERR_INVALID_SIZE) {
        return sendRequestBodyTooLarge(request);
    }
    if (body_err != ESP_OK) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            server->status_service_->buildInfo(),
            "Failed to read form body.",
            true);
        return sendHtmlResponse(request, html);
    }

    const FormFields fields = parseFormBody(body);
    BackendRegistry registry;
    BackendConfigList updated = *server->backend_config_list_;

    const std::string upload_interval_value = findFormValue(fields, "upload_interval_ms");
    unsigned long upload_interval_ms = 0UL;
    if (!parseUnsignedLong(upload_interval_value, upload_interval_ms) ||
        upload_interval_ms < 10000UL ||
        upload_interval_ms > 300000UL) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            server->status_service_->buildInfo(),
            "Upload interval must be between 10000 ms and 300000 ms.",
            true);
        return sendHtmlResponse(request, html);
    }
    updated.upload_interval_ms = static_cast<std::uint32_t>(upload_interval_ms);

    for (std::size_t index = 0; index < registry.descriptorCount(); ++index) {
        const BackendDescriptor& descriptor = registry.descriptors()[index];
        BackendRecord* record = findBackendRecordByType(updated, descriptor.type);
        if (record == nullptr) {
            const std::string html = renderBackendsPage(
                *server->backend_config_list_,
                *server->upload_manager_,
                server->status_service_->buildInfo(),
                "Backend configuration is incomplete.",
                true);
            return sendHtmlResponse(request, html);
        }

        const std::string checkbox_name = std::string("enabled_") + descriptor.backend_key;
        record->enabled = formHasKey(fields, checkbox_name.c_str()) ? 1U : 0U;
        if (record->enabled == 0U) {
            continue;
        }

        const std::string key = descriptor.backend_key;
        record->protocol =
            formHasKey(fields, (std::string("use_https_") + key).c_str())
                ? BackendProtocol::kHttps
                : BackendProtocol::kHttp;

        switch (descriptor.type) {
            case BackendType::kSensorCommunity:
                copyString(
                    record->sensor_community_device_id,
                    sizeof(record->sensor_community_device_id),
                    findFormValue(fields, (std::string("device_id_") + key).c_str()));
                break;

            case BackendType::kAir360Api:
                break;

            case BackendType::kCustomUpload: {
                const std::string port_value =
                    findFormValue(fields, (std::string("port_") + key).c_str());
                unsigned long port = 0UL;
                if (!port_value.empty() &&
                    (!parseUnsignedLong(port_value, port) || port == 0UL || port > 65535UL)) {
                    const std::string html = renderBackendsPage(
                        *server->backend_config_list_,
                        *server->upload_manager_,
                        server->status_service_->buildInfo(),
                        "Port must be between 1 and 65535.",
                        true);
                    return sendHtmlResponse(request, html);
                }
                record->port = static_cast<std::uint16_t>(port);
                copyString(record->host, sizeof(record->host),
                    findFormValue(fields, (std::string("host_") + key).c_str()));
                copyString(record->path, sizeof(record->path),
                    findFormValue(fields, (std::string("path_") + key).c_str()));
                break;
            }

            case BackendType::kInfluxDb: {
                const std::string port_value =
                    findFormValue(fields, (std::string("port_") + key).c_str());
                unsigned long port = 0UL;
                if (!port_value.empty() &&
                    (!parseUnsignedLong(port_value, port) || port == 0UL || port > 65535UL)) {
                    const std::string html = renderBackendsPage(
                        *server->backend_config_list_,
                        *server->upload_manager_,
                        server->status_service_->buildInfo(),
                        "Port must be between 1 and 65535.",
                        true);
                    return sendHtmlResponse(request, html);
                }
                record->port = static_cast<std::uint16_t>(port);
                copyString(record->host, sizeof(record->host),
                    findFormValue(fields, (std::string("host_") + key).c_str()));
                copyString(record->path, sizeof(record->path),
                    findFormValue(fields, (std::string("path_") + key).c_str()));
                const std::string username =
                    findFormValue(fields, (std::string("user_") + key).c_str());
                const std::string password =
                    findFormValue(fields, (std::string("password_") + key).c_str());
                record->auth.auth_type = (!username.empty() || !password.empty())
                    ? BackendAuthType::kBasic : BackendAuthType::kNone;
                copyString(record->auth.basic_username,
                    sizeof(record->auth.basic_username), username);
                copyString(record->auth.basic_password,
                    sizeof(record->auth.basic_password), password);
                copyString(record->influxdb_measurement, sizeof(record->influxdb_measurement),
                    findFormValue(fields, (std::string("measurement_") + key).c_str()));
                break;
            }

            default:
                break;
        }
    }

    const esp_err_t save_err = server->backend_config_repository_->save(updated);
    if (save_err != ESP_OK) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            server->status_service_->buildInfo(),
            std::string("Failed to save backend configuration: ") + esp_err_to_name(save_err),
            true);
        return sendHtmlResponse(request, html);
    }

    *server->backend_config_list_ = updated;
    const esp_err_t apply_err = server->upload_manager_->applyConfig(updated);
    server->status_service_->setUploads(*server->upload_manager_);

    if (apply_err != ESP_OK) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            server->status_service_->buildInfo(),
            std::string("Backend configuration saved, but runtime apply failed: ") +
                esp_err_to_name(apply_err) + ". Reboot to apply it.",
            true);
        return sendHtmlResponse(request, html);
    }

    const std::string html = renderBackendsPage(
        *server->backend_config_list_,
        *server->upload_manager_,
        server->status_service_->buildInfo(),
        "Backend selection saved.",
        false);
    return sendHtmlResponse(request, html);
}

esp_err_t WebServer::handleConfig(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderConfigPage(
            *server->config_,
            *server->cellular_config_,
            server->status_service_->networkState(),
            *server->network_manager_,
            "",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    std::string body;
    const esp_err_t body_err = readRequestBody(request, body);
    if (body_err == ESP_ERR_INVALID_SIZE) {
        return sendRequestBodyTooLarge(request);
    }
    if (body_err != ESP_OK) {
        const std::string html = renderConfigPage(
            *server->config_,
            *server->cellular_config_,
            server->status_service_->networkState(),
            *server->network_manager_,
            "Failed to read form body.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    const FormFields fields = parseFormBody(body);

    const std::string device_name = findFormValue(fields, "device_name");
    const std::string wifi_ssid = findFormValue(fields, "wifi_ssid");
    const std::string wifi_password = findFormValue(fields, "wifi_password");
    const std::string sntp_server = findFormValue(fields, "sntp_server");
    const bool wifi_power_save_enabled = (findFormValue(fields, "wifi_power_save") == "1");
    const bool sta_use_static_ip = (findFormValue(fields, "sta_use_static_ip") == "1");
    const std::string sta_ip = sta_use_static_ip ? findFormValue(fields, "sta_ip") : "";
    const std::string sta_netmask = sta_use_static_ip ? findFormValue(fields, "sta_netmask") : "";
    const std::string sta_gateway = sta_use_static_ip ? findFormValue(fields, "sta_gateway") : "";
    const std::string sta_dns = sta_use_static_ip ? findFormValue(fields, "sta_dns") : "";

    const bool ble_advertise_enabled = (findFormValue(fields, "ble_advertise_enabled") == "1");
    unsigned long ble_adv_interval_index = kBleAdvIntervalDefaultIndex;
    parseUnsignedLong(findFormValue(fields, "ble_adv_interval_index"), ble_adv_interval_index);
    if (ble_adv_interval_index >= kBleAdvIntervalCount) {
        ble_adv_interval_index = kBleAdvIntervalDefaultIndex;
    }

    const bool cellular_enabled = (findFormValue(fields, "cellular_enabled") == "1");
    const std::string cellular_apn =
        cellular_enabled ? findFormValue(fields, "cellular_apn") : "";
    const std::string cellular_username = findFormValue(fields, "cellular_username");
    const std::string cellular_password = findFormValue(fields, "cellular_password");
    const std::string cellular_sim_pin = findFormValue(fields, "cellular_sim_pin");
    const std::string cellular_connectivity_check_host =
        findFormValue(fields, "cellular_connectivity_check_host");

    unsigned long cellular_wifi_debug_window_s = server->cellular_config_->wifi_debug_window_s;
    parseUnsignedLong(findFormValue(fields, "cellular_wifi_debug_window_s"),
                      cellular_wifi_debug_window_s);

    std::string validation_error;
    if (!validateConfigForm(
            device_name,
            wifi_ssid,
            wifi_password,
            sntp_server,
            sta_use_static_ip,
            sta_ip,
            sta_netmask,
            sta_gateway,
            sta_dns,
            cellular_enabled,
            cellular_apn,
            cellular_username,
            cellular_password,
            cellular_sim_pin,
            cellular_connectivity_check_host,
            cellular_wifi_debug_window_s,
            validation_error)) {
        DeviceConfig preview = *server->config_;
        copyString(preview.device_name, sizeof(preview.device_name), device_name);
        copyString(preview.wifi_sta_ssid, sizeof(preview.wifi_sta_ssid), wifi_ssid);
        copyString(preview.wifi_sta_password, sizeof(preview.wifi_sta_password), wifi_password);
        copyString(preview.sntp_server, sizeof(preview.sntp_server), sntp_server);
        preview.wifi_power_save_enabled = wifi_power_save_enabled ? 1U : 0U;
        preview.ble_advertise_enabled = ble_advertise_enabled ? 1U : 0U;
        preview.ble_adv_interval_index = static_cast<std::uint8_t>(ble_adv_interval_index);
        preview.sta_use_static_ip = sta_use_static_ip ? 1U : 0U;
        copyString(preview.sta_ip, sizeof(preview.sta_ip), sta_ip);
        copyString(preview.sta_netmask, sizeof(preview.sta_netmask), sta_netmask);
        copyString(preview.sta_gateway, sizeof(preview.sta_gateway), sta_gateway);
        copyString(preview.sta_dns, sizeof(preview.sta_dns), sta_dns);

        CellularConfig preview_cellular = *server->cellular_config_;
        preview_cellular.enabled = cellular_enabled ? 1U : 0U;
        copyString(preview_cellular.apn, sizeof(preview_cellular.apn), cellular_apn);
        copyString(preview_cellular.username, sizeof(preview_cellular.username), cellular_username);
        copyString(preview_cellular.password, sizeof(preview_cellular.password), cellular_password);
        copyString(preview_cellular.sim_pin, sizeof(preview_cellular.sim_pin), cellular_sim_pin);
        copyString(preview_cellular.connectivity_check_host,
                   sizeof(preview_cellular.connectivity_check_host),
                   cellular_connectivity_check_host);
        preview_cellular.wifi_debug_window_s =
            static_cast<std::uint16_t>(cellular_wifi_debug_window_s);

        const std::string html = renderConfigPage(
            preview,
            preview_cellular,
            server->status_service_->networkState(),
            *server->network_manager_,
            validation_error,
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    DeviceConfig updated = *server->config_;
    updated.magic = kDeviceConfigMagic;
    updated.schema_version = kDeviceConfigSchemaVersion;
    updated.record_size = static_cast<std::uint16_t>(sizeof(DeviceConfig));
    copyString(updated.device_name, sizeof(updated.device_name), device_name);
    copyString(updated.wifi_sta_ssid, sizeof(updated.wifi_sta_ssid), wifi_ssid);
    copyString(updated.wifi_sta_password, sizeof(updated.wifi_sta_password), wifi_password);
    copyString(updated.sntp_server, sizeof(updated.sntp_server), sntp_server);
    updated.wifi_power_save_enabled = wifi_power_save_enabled ? 1U : 0U;
    updated.ble_advertise_enabled = ble_advertise_enabled ? 1U : 0U;
    updated.ble_adv_interval_index = static_cast<std::uint8_t>(ble_adv_interval_index);
    updated.sta_use_static_ip = sta_use_static_ip ? 1U : 0U;
    copyString(updated.sta_ip, sizeof(updated.sta_ip), sta_ip);
    copyString(updated.sta_netmask, sizeof(updated.sta_netmask), sta_netmask);
    copyString(updated.sta_gateway, sizeof(updated.sta_gateway), sta_gateway);
    copyString(updated.sta_dns, sizeof(updated.sta_dns), sta_dns);

    const esp_err_t save_err = server->config_repository_->save(updated);
    if (save_err != ESP_OK) {
        const std::string html = renderConfigPage(
            updated,
            *server->cellular_config_,
            server->status_service_->networkState(),
            *server->network_manager_,
            std::string("Failed to save device configuration: ") + esp_err_to_name(save_err),
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    CellularConfig updated_cellular = *server->cellular_config_;
    updated_cellular.magic = kCellularConfigMagic;
    updated_cellular.schema_version = kCellularConfigSchemaVersion;
    updated_cellular.record_size = static_cast<std::uint16_t>(sizeof(CellularConfig));
    updated_cellular.enabled = cellular_enabled ? 1U : 0U;
    copyString(updated_cellular.apn, sizeof(updated_cellular.apn), cellular_apn);
    copyString(updated_cellular.username, sizeof(updated_cellular.username), cellular_username);
    copyString(updated_cellular.password, sizeof(updated_cellular.password), cellular_password);
    copyString(updated_cellular.sim_pin, sizeof(updated_cellular.sim_pin), cellular_sim_pin);
    copyString(updated_cellular.connectivity_check_host,
               sizeof(updated_cellular.connectivity_check_host),
               cellular_connectivity_check_host);
    updated_cellular.wifi_debug_window_s =
        static_cast<std::uint16_t>(cellular_wifi_debug_window_s);

    const esp_err_t cellular_save_err =
        server->cellular_config_repository_->save(updated_cellular);
    if (cellular_save_err != ESP_OK) {
        const std::string html = renderConfigPage(
            updated,
            updated_cellular,
            server->status_service_->networkState(),
            *server->network_manager_,
            std::string("Failed to save cellular configuration: ") +
                esp_err_to_name(cellular_save_err),
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    *server->config_ = updated;
    *server->cellular_config_ = updated_cellular;
    server->status_service_->setConfig(updated, true, false);
    const std::string html = renderConfigPage(
        updated,
        updated_cellular,
        server->status_service_->networkState(),
        *server->network_manager_,
        "Configuration saved. Device is rebooting now.",
        false);
    esp_err_t response_err = httpd_resp_send(request, html.c_str(), html.size());
    if (response_err == ESP_OK) {
        scheduleRestart();
    }
    return response_err;
}

}  // namespace air360
