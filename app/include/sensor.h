#pragma once

#include <stdbool.h>
#include <stdint.h>

struct motion_sensor_sample {
	bool valid;
	bool moving;
	int64_t uptime_ms;
	int32_t accel_milli_ms2[3];
	int32_t gyro_milli_rad_s[3];
	int32_t die_temp_milli_c;
};

void motion_sensor_init(void);
bool motion_sensor_get_latest(struct motion_sensor_sample *sample);
