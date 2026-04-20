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
