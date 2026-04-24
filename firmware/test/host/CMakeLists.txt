cmake_minimum_required(VERSION 3.16)

project(air360_host_tests LANGUAGES CXX)

enable_testing()

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
