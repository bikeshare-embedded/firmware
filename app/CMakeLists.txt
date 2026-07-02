cmake_minimum_required(VERSION 3.20.0)

if(DEFINED WEST_PYTHON)
  set(Python3_EXECUTABLE ${WEST_PYTHON} CACHE FILEPATH "Python used by west" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bikeshare_firmware)

target_sources(app PRIVATE
	src/main.c
	src/button.c
	src/channels.c
	src/config.c
	src/shell.c
	src/state.c
	src/led.c
	src/lte.c
	src/sensor.c
)

target_include_directories(app PRIVATE include)
