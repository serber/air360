cmake_minimum_required(VERSION 3.16)

project(air360_host_tests LANGUAGES CXX)

enable_testing()

add_executable(test_web_form
    ../../main/src/web/web_form.cpp
    test_web_form.cpp
)

target_include_directories(test_web_form PRIVATE
    ../../main/include
)

target_compile_features(test_web_form PRIVATE cxx_std_17)

add_test(NAME web_form COMMAND test_web_form)

add_executable(test_backend_config
    test_backend_config.cpp
)

target_include_directories(test_backend_config PRIVATE
    ../../main/include
)

target_compile_features(test_backend_config PRIVATE cxx_std_17)

add_test(NAME backend_config COMMAND test_backend_config)

add_executable(test_upload_prune_policy
    ../../main/src/uploads/upload_prune_policy.cpp
    test_upload_prune_policy.cpp
)

target_include_directories(test_upload_prune_policy PRIVATE
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
