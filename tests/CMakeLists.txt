cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bikeshare_firmware_tests)

target_include_directories(app PRIVATE ../app/include)

target_sources(app PRIVATE
	src/main.c
	../app/src/button.c
	../app/src/channels.c
	../app/src/config.c
	../app/src/gnss.c
	../app/src/led.c
	../app/src/state.c
	../app/src/mqtt_client.c
	../app/src/telemetry.c
)
