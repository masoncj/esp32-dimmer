set (IDF_PATH "/usr/local/src/esp-idf/")

cmake_minimum_required(VERSION 3.6)
project(esp_dimmer)

set(CMAKE_SYSTEM_NAME xtensa-esp32-elf)
set(CMAKE_C_COMPILER xtensa-esp32-elf-gcc)
set(CMAKE_FIND_ROOT_PATH /usr/local/xtensa-esp32-elf/)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
    main/main.c)

include_directories ("build/include")
include_directories ("${IDF_PATH}/components/freertos/include")
include_directories ("${IDF_PATH}/components/driver/include")
include_directories ("${IDF_PATH}/components/esp32/include")
include_directories ("${IDF_PATH}/components/nvs_flash/include")
include_directories ("${IDF_PATH}/components/log/include")

add_executable(esp_dimmer ${SOURCE_FILES})