
cmake_minimum_required(VERSION 3.22)

if("$ENV{IDF_PATH}" STREQUAL "")
    message(FATAL_ERROR
        "IDF_PATH is not set. Load the ESP-IDF environment first "
        "(for example: '. $HOME/.espressif/v6.0/esp-idf/export.sh') "
        "or open firmware/ through the ESP-IDF VS Code extension."
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(air360_firmware)
