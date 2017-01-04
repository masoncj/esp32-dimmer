# NOTE: This file is only used for configuring CLion, use regular `make` for building.

set (IDF_PATH "/usr/local/src/esp-idf/")
set (DUK_PATH "components/duktape-esp32/")

cmake_minimum_required(VERSION 3.6)
project(esp_dimmer)

set(CMAKE_SYSTEM_NAME xtensa-esp32-elf)
set(CMAKE_C_COMPILER xtensa-esp32-elf-gcc)
set(CMAKE_FIND_ROOT_PATH /usr/local/xtensa-esp32-elf/)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
    main/main.c
    ${DUK_PATH}
    )

include_directories ("build/include")
include_directories ("${IDF_PATH}/components/freertos/include")
include_directories ("${IDF_PATH}/components/driver/include")
include_directories ("${IDF_PATH}/components/esp32/include")
include_directories ("${IDF_PATH}/components/nvs_flash/include")
include_directories ("${IDF_PATH}/components/tcpip_adapter/include")
include_directories ("${IDF_PATH}/components/log/include")
include_directories ("${DUK_PATH}/components/duktape/src")
include_directories ("components/duktape-esp32-main/include")

add_executable(esp_dimmer ${SOURCE_FILES})
