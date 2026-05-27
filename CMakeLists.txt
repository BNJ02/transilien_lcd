cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(transilien_lcd C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Sources TLS 1.3 + PSA absentes du pico_mbedtls par défaut
set(MBEDTLS_LIB ${PICO_SDK_PATH}/lib/mbedtls/library)

add_executable(transilien_lcd
    main.c
    ${MBEDTLS_LIB}/ssl_tls13_client.c
    ${MBEDTLS_LIB}/ssl_tls13_generic.c
    ${MBEDTLS_LIB}/psa_util.c
    ${MBEDTLS_LIB}/psa_crypto_driver_wrappers_no_static.c
)

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
