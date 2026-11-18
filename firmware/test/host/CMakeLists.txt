cmake_minimum_required(VERSION 3.16)

project(air360_host_tests LANGUAGES CXX)

enable_testing()

add_executable(test_upload_transport
    test_upload_transport.cpp
    ../../main/src/uploads/upload_transport.cpp
    ../../main/src/uploads/upload_log_endpoint.cpp
)
target_include_directories(test_upload_transport PRIVATE stubs ../../main/include)
target_compile_features(test_upload_transport PRIVATE cxx_std_17)
add_test(NAME upload_transport COMMAND test_upload_transport)

add_executable(test_config_transaction
    test_config_transaction.cpp
    ../../main/src/config_transaction.cpp
    ../../main/src/config_repository.cpp
    ../../main/src/cellular_config_repository.cpp
)
target_include_directories(test_config_transaction PRIVATE stubs ../../main/include)
target_compile_options(test_config_transaction PRIVATE -include sdkconfig.h)
target_compile_features(test_config_transaction PRIVATE cxx_std_17)
add_test(NAME config_transaction COMMAND test_config_transaction)

add_executable(test_network_uplink test_network_uplink.cpp)
target_include_directories(test_network_uplink PRIVATE stubs ../../main/include)
target_compile_features(test_network_uplink PRIVATE cxx_std_17)
add_test(NAME network_uplink COMMAND test_network_uplink)

add_executable(test_web_form
    ../../main/src/web/web_form.cpp
    test_web_form.cpp
)

target_include_directories(test_web_form PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_web_form PRIVATE cxx_std_17)

add_test(NAME web_form COMMAND test_web_form)

add_executable(test_web_request_body
    ../../main/src/web/web_request_body.cpp
    test_web_request_body.cpp
)

target_include_directories(test_web_request_body PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_web_request_body PRIVATE cxx_std_17)

add_test(NAME web_request_body COMMAND test_web_request_body)

add_executable(test_backend_config
    test_backend_config.cpp
)

target_include_directories(test_backend_config PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_backend_config PRIVATE cxx_std_17)

add_test(NAME backend_config COMMAND test_backend_config)

add_executable(test_upload_log_endpoint
    ../../main/src/uploads/upload_log_endpoint.cpp
    test_upload_log_endpoint.cpp
)

target_include_directories(test_upload_log_endpoint PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_upload_log_endpoint PRIVATE cxx_std_17)

add_test(NAME upload_log_endpoint COMMAND test_upload_log_endpoint)

add_executable(test_upload_prune_policy
    ../../main/src/uploads/upload_prune_policy.cpp
    test_upload_prune_policy.cpp
)

target_include_directories(test_upload_prune_policy PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_upload_prune_policy PRIVATE cxx_std_17)

add_test(NAME upload_prune_policy COMMAND test_upload_prune_policy)

add_executable(test_sensor_community_uploader
    ../../main/src/uploads/adapters/sensor_community_uploader.cpp
    test_sensor_community_uploader.cpp
)

target_include_directories(test_sensor_community_uploader PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_sensor_community_uploader PRIVATE cxx_std_17)

add_test(NAME sensor_community_uploader COMMAND test_sensor_community_uploader)

add_executable(test_measurement_store
    ../../main/src/uploads/measurement_store.cpp
    ../../main/src/uploads/upload_prune_policy.cpp
    test_measurement_store.cpp
)

target_include_directories(test_measurement_store PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_measurement_store PRIVATE cxx_std_17)

add_test(NAME measurement_store COMMAND test_measurement_store)

add_executable(test_ble_encoding
    test_ble_encoding.cpp
)

target_include_directories(test_ble_encoding PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_ble_encoding PRIVATE cxx_std_17)

add_test(NAME ble_encoding COMMAND test_ble_encoding)

add_executable(test_sensor_sample_freshness
    test_sensor_sample_freshness.cpp
)

target_include_directories(test_sensor_sample_freshness PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_sensor_sample_freshness PRIVATE cxx_std_17)

add_test(NAME sensor_sample_freshness COMMAND test_sensor_sample_freshness)

add_executable(test_sensor_config_store
    ../../main/src/sensors/sensor_config_store.cpp
    test_sensor_config_store.cpp
)

target_include_directories(test_sensor_config_store PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_sensor_config_store PRIVATE cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(test_sensor_config_store PRIVATE Threads::Threads)

add_test(NAME sensor_config_store COMMAND test_sensor_config_store)

add_executable(test_backend_config_repository
    ../../main/src/uploads/backend_config_repository.cpp
    ../../main/src/uploads/backend_registry.cpp
    ../../main/src/uploads/adapters/air360_api_uploader.cpp
    ../../main/src/uploads/adapters/air360_json_payload.cpp
    ../../main/src/uploads/adapters/custom_upload_uploader.cpp
    ../../main/src/uploads/adapters/influxdb_uploader.cpp
    ../../main/src/uploads/adapters/opensensemap_uploader.cpp
    ../../main/src/uploads/adapters/sensor_community_uploader.cpp
    ../../main/src/uploads/air360_api_credentials.cpp
    ../../main/src/uploads/opensensemap_mapping_repository.cpp
    ../../main/src/uploads/upload_transport.cpp
    ../../main/src/uploads/upload_log_endpoint.cpp
    fakes/crypto_utils_fake.cpp
    test_backend_config_repository.cpp
)

target_include_directories(test_backend_config_repository PRIVATE
    stubs
    ../../main/include
)

target_compile_features(test_backend_config_repository PRIVATE cxx_std_17)

add_test(NAME backend_config_repository COMMAND test_backend_config_repository)

add_executable(test_sensor_manager_lifecycle
    ../../main/src/sensors/sensor_manager.cpp
    ../../main/src/sensors/sensor_driver.cpp
    ../../main/src/uploads/measurement_store.cpp
    ../../main/src/uploads/upload_prune_policy.cpp
    fakes/sensor_registry_fake.cpp
    fakes/transport_binding_fake.cpp
    test_sensor_manager_lifecycle.cpp
)

target_include_directories(test_sensor_manager_lifecycle PRIVATE
    stubs
    fakes
    ../../main/include
)

target_compile_features(test_sensor_manager_lifecycle PRIVATE cxx_std_20)
target_link_libraries(test_sensor_manager_lifecycle PRIVATE Threads::Threads)

add_test(NAME sensor_manager_lifecycle COMMAND test_sensor_manager_lifecycle)

add_executable(test_sensor_config_repository
    ../../main/src/sensors/sensor_config_repository.cpp
    fakes/sensor_registry_fake.cpp
    ../../main/src/sensors/sensor_driver.cpp
    test_sensor_config_repository.cpp
)

target_include_directories(test_sensor_config_repository PRIVATE
    stubs
    fakes
    ../../main/include
)

target_compile_features(test_sensor_config_repository PRIVATE cxx_std_20)

add_test(NAME sensor_config_repository COMMAND test_sensor_config_repository)

add_executable(test_upload_manager_lifecycle
    ../../main/src/uploads/upload_manager.cpp
    ../../main/src/uploads/upload_manager_config.cpp
    ../../main/src/uploads/upload_manager_status.cpp
    ../../main/src/uploads/measurement_store.cpp
    ../../main/src/uploads/upload_prune_policy.cpp
    ../../main/src/uploads/backend_config_repository.cpp
    ../../main/src/uploads/backend_registry.cpp
    ../../main/src/uploads/adapters/air360_api_uploader.cpp
    ../../main/src/uploads/adapters/air360_json_payload.cpp
    ../../main/src/uploads/adapters/custom_upload_uploader.cpp
    ../../main/src/uploads/adapters/influxdb_uploader.cpp
    ../../main/src/uploads/adapters/opensensemap_uploader.cpp
    ../../main/src/uploads/adapters/sensor_community_uploader.cpp
    ../../main/src/uploads/air360_api_credentials.cpp
    ../../main/src/uploads/opensensemap_mapping_repository.cpp
    ../../main/src/uploads/upload_transport.cpp
    ../../main/src/uploads/upload_log_endpoint.cpp
    ../../main/src/sensors/sensor_manager.cpp
    ../../main/src/sensors/sensor_driver.cpp
    fakes/sensor_registry_fake.cpp
    fakes/transport_binding_fake.cpp
    fakes/network_manager_fake.cpp
    fakes/crypto_utils_fake.cpp
    test_upload_manager_lifecycle.cpp
)

target_include_directories(test_upload_manager_lifecycle PRIVATE
    stubs
    fakes
    ../../main/include
)

target_compile_features(test_upload_manager_lifecycle PRIVATE cxx_std_20)
target_link_libraries(test_upload_manager_lifecycle PRIVATE Threads::Threads)

add_test(NAME upload_manager_lifecycle COMMAND test_upload_manager_lifecycle)

add_executable(test_uart_frames
    ../../main/src/sensors/drivers/sds011_sensor.cpp
    ../../main/src/sensors/drivers/pmsx003_sensor.cpp
    ../../main/src/sensors/sensor_driver.cpp
    fakes/transport_binding_fake.cpp
    test_uart_frames.cpp
)

target_include_directories(test_uart_frames PRIVATE
    stubs
    fakes
    ../../main/include
)

target_compile_features(test_uart_frames PRIVATE cxx_std_20)

add_test(NAME uart_frames COMMAND test_uart_frames)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
add_executable(test_web_auth
    test_web_auth.cpp
    ../../main/src/web/web_auth.cpp
    ../../main/src/web/web_auth_store.cpp
)
target_include_directories(test_web_auth PRIVATE stubs ../../main/include)
target_link_libraries(test_web_auth PRIVATE OpenSSL::Crypto)
target_compile_features(test_web_auth PRIVATE cxx_std_17)
add_test(NAME web_auth COMMAND test_web_auth)
