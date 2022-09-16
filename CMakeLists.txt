cmake_minimum_required(VERSION 3.13)

include(/usr/local/raspberrypi-pico-sdk/pico_sdk_init.cmake)

project(morse)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(morse
        morse.cpp
	libmorse.cpp
        )

# pull in common dependencies
target_link_libraries(morse
	pico_stdlib
	pico_malloc
	)

pico_enable_stdio_usb(morse 1)
pico_enable_stdio_uart(morse 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(morse)
