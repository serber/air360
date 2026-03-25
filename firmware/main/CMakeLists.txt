idf_component_register(
    SRCS
        "src/app_main.cpp"
        "src/app.cpp"
        "src/build_info.cpp"
        "src/config_repository.cpp"
        "src/network_manager.cpp"
        "src/sensors/sensor_config_repository.cpp"
        "src/sensors/sensor_manager.cpp"
        "src/sensors/sensor_registry.cpp"
        "src/status_service.cpp"
        "src/web_server.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_app_format
        esp_common
        esp_event
        esp_http_server
        esp_netif
        esp_system
        esp_timer
        esp_wifi
        freertos
        nvs_flash
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
