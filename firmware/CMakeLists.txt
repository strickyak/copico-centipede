set(PROJECT centipede)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD solderparty_rp2350_stamp_xl)
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_RP2350A 0)
set(PICO_RP2350B 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()


add_executable(${PROJECT} centipede.cpp)
# pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/watch.pio)
target_sources(${PROJECT} PRIVATE
    centipede.cpp
    disk11_rom.c
    cross-core.h
)
target_link_libraries(${PROJECT} PRIVATE
    hardware_pio
    pico_stdlib
    pico_multicore
    cmsis_core
)
# pico_rand hardware_pio hardware_i2c

pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

add_compile_definitions(OS_LEVEL=${OS_LEVEL})
add_compile_definitions(TRACKING=${TRACKING})

# The Pi Pico can be overclocked if the binary gets copied to ram.
pico_set_binary_type(${PROJECT} copy_to_ram)
