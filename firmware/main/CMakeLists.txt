idf_component_register(
    SRCS
        "src/app_main.cpp"
        "src/app.cpp"
        "src/ble_advertiser.cpp"
        "src/data/data_layer.cpp"
        "src/network/network_layer.cpp"
        "src/platform/platform_layer.cpp"
        "src/build_info.cpp"
        "src/cellular_config_repository.cpp"
        "src/cellular_manager.cpp"
        "src/modem_gpio.cpp"
        "src/config_repository.cpp"
        "src/config_transaction.cpp"
        "src/connectivity_checker.cpp"
        "src/crypto_utils.cpp"
        "src/network_manager.cpp"
        "src/ota_service.cpp"
        "src/web_assets.cpp"
        "src/web_ui.cpp"
        "src/web/web_request_body.cpp"
        "src/uploads/air360_api_credentials.cpp"
        "src/uploads/backend_config_repository.cpp"
        "src/uploads/backend_registry.cpp"
        "src/uploads/adapters/air360_json_payload.cpp"
        "src/uploads/adapters/air360_api_uploader.cpp"
        "src/uploads/adapters/custom_upload_uploader.cpp"
        "src/uploads/adapters/influxdb_uploader.cpp"
        "src/uploads/adapters/sensor_community_uploader.cpp"
        "src/uploads/measurement_store.cpp"
        "src/uploads/upload_log_endpoint.cpp"
        "src/uploads/upload_prune_policy.cpp"
        "src/uploads/upload_manager.cpp"
        "src/uploads/upload_manager_config.cpp"
        "src/uploads/upload_manager_status.cpp"
        "src/uploads/upload_transport.cpp"
        "src/sensors/sensor_config_repository.cpp"
        "src/sensors/drivers/aht30_sensor.cpp"
        "src/sensors/drivers/bme280_sensor.cpp"
        "src/sensors/drivers/bme680_sensor.cpp"
        "src/sensors/drivers/dht_sensor.cpp"
        "src/sensors/drivers/ds18b20_sensor.cpp"
        "src/sensors/drivers/gps_nmea_sensor.cpp"
        "src/sensors/drivers/htu2x_sensor.cpp"
        "src/sensors/drivers/ina219_sensor.cpp"
        "src/sensors/drivers/mhz19b_sensor.cpp"
        "src/sensors/drivers/me3_no2_sensor.cpp"
        "src/sensors/drivers/opt3001_sensor.cpp"
        "src/sensors/drivers/pmsx003_sensor.cpp"
        "src/sensors/drivers/ppd42ns_sensor.cpp"
        "src/sensors/drivers/scd30_sensor.cpp"
        "src/sensors/drivers/sds011_sensor.cpp"
        "src/sensors/drivers/sht3x_sensor.cpp"
        "src/sensors/drivers/sht4x_sensor.cpp"
        "src/sensors/drivers/sensirion_i2c_hal.cpp"
        "src/sensors/drivers/sps30_sensor.cpp"
        "src/sensors/drivers/veml7700_sensor.cpp"
        "src/sensors/sensor_driver.cpp"
        "src/sensors/sensor_manager.cpp"
        "src/sensors/sensor_registry.cpp"
        "src/sensors/transport_binding.cpp"
        "third_party/sps30/sensirion_common.c"
        "third_party/sps30/sensirion_i2c.c"
        "third_party/sps30/sps30_i2c.c"
        "src/status_service.cpp"
        "src/log_buffer.cpp"
        "src/web_server.cpp"
        "src/web/web_form.cpp"
        "src/web/web_runtime_routes.cpp"
        "src/web/web_mutating_routes.cpp"
        "src/web/web_ota_routes.cpp"
        "src/web/web_server_helpers.cpp"
    INCLUDE_DIRS
        "include"
        "third_party/sps30"
    REQUIRES
        app_update
        esp_app_format
        esp_common
        esp_event
        esp_driver_gpio
        espressif__led_strip
        esp_http_client
        esp_http_server
        esp_driver_i2c
        esp_driver_uart
        esp-idf-lib__bme680
        esp-idf-lib__dht
        esp-idf-lib__scd30
        esp-idf-lib__sht3x
        esp-idf-lib__sht4x
        esp-idf-lib__ina219
        esp-idf-lib__mhz19b
        petrovgp__esp-pms
        esp-idf-lib__si7021
        esp-idf-lib__veml7700
        espressif__aht30
        espressif__bme280
        espressif__i2c_bus
        espressif__ds18b20
        espressif__onewire_bus
        cinderblocks__esp_tinygpsplusplus
        esp_adc
        efuse
        esp_psram
        esp_rom
        mbedtls
        esp_netif
        esp_system
        lwip
        esp_timer
        esp_wifi
        bt
        espressif__mdns
        nordesems__esp-captive-portal
        esp_modem
        freertos
        nvs_flash
    EMBED_FILES
        "webui/favicon.ico"
    EMBED_TXTFILES
        "webui/air360.css"
        "webui/air360.js"
        "webui/vendor/maplibre/maplibre-gl.css"
        "webui/vendor/maplibre/maplibre-gl.js"
        "webui/page_root.html"
        "webui/page_config.html"
        "webui/page_sensors.html"
        "webui/page_backends.html"
        "webui/page_diagnostics.html"
        "webui/card_backend.html"
        "webui/card_sensor.html"
        "webui/item_overview_backend.html"
        "webui/item_overview_sensor.html"
        "webui/page_shell.html"
        "webui/item_reading.html"
        "webui/item_section_row.html"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE
    # Surface real bugs.  -Wall and -Wextra are already on by ESP-IDF; listed
    # here for clarity.
    -Wall
    -Wextra
    -Wshadow
    -Wmissing-declarations
    -Wcast-align
    -Wdouble-promotion
    -Wformat=2

    # ESP-IDF applies a global -Werror; without these overrides every new
    # warning above would block the build.  PR 1 (this commit) keeps the
    # noisier categories as warnings so they show up in compile output as
    # cleanup work; subsequent PRs are expected to tighten them one
    # subsystem at a time.
    -Wno-error=shadow
    -Wno-error=cast-align
    -Wno-error=double-promotion
    -Wno-error=format

    # Promote near-certain bugs to errors.
    -Werror=return-type
    -Werror=switch
    -Werror=missing-declarations
    -Werror=unused-result

    # FreeRTOS task-entry signatures take a void* arg that is often unused;
    # silencing this avoids a sea of (void)arg casts across drivers.
    -Wno-unused-parameter

    # C++-only diagnostics — guarded so they do not reach the third_party
    # Sensirion C sources also compiled into this component.
    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
    $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=overloaded-virtual>
    $<$<COMPILE_LANGUAGE:CXX>:-Werror=non-virtual-dtor>
)

# petrovgp/esp-pms 1.0.0 still declares the legacy `driver` dependency, which
# does not expose split ESP-IDF 6 GPIO/UART headers to the component itself.
idf_component_get_property(PMS_COMPONENT_LIB petrovgp__esp-pms COMPONENT_LIB)
target_link_libraries(${PMS_COMPONENT_LIB} PUBLIC idf::esp_driver_gpio idf::esp_driver_uart)
