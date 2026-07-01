cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bikeshare_firmware_tests)

target_include_directories(app PRIVATE ../app/include)

target_sources(app PRIVATE
	src/main.c
	../app/src/app_channels.c
	../app/src/button_input.c
	../app/src/bike_config.c
	../app/src/bike_state.c
	../app/src/led_status.c
)
