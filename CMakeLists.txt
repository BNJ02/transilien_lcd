cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(transilien_lcd C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(transilien_lcd main.c)

target_include_directories(transilien_lcd PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(transilien_lcd
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mbedtls
    pico_mbedtls
)

pico_add_extra_outputs(transilien_lcd)
pico_enable_stdio_uart(transilien_lcd 1)
pico_enable_stdio_usb(transilien_lcd 0)
