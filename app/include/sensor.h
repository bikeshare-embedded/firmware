/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Motion sensor (MPU6050) sampling.
 *
 * Periodically samples the MPU6050 accelerometer/gyroscope, when present
 * in the devicetree, and caches the latest reading for retrieval.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Latest cached reading from the motion sensor. */
struct motion_sensor_sample {
	/** True if the sample was ever successfully populated. */
	bool valid;
	/** True if the sample indicates the bike is in motion. */
	bool moving;
	/** Timestamp of the sample, in milliseconds since boot. */
	int64_t uptime_ms;
	/** Acceleration on each axis, in milli-m/s^2. */
	int32_t accel_milli_ms2[3];
	/** Angular velocity on each axis, in milli-rad/s. */
	int32_t gyro_milli_rad_s[3];
	/** Die temperature, in milli-degrees Celsius. */
	int32_t die_temp_milli_c;
};

/**
 * @brief Initialize the motion sensor module.
 *
 * Starts periodic sampling if the MPU6050 is present and ready in the
 * devicetree; otherwise the module stays idle and
 * motion_sensor_get_latest() always reports an invalid sample.
 */
void motion_sensor_init(void);

/**
 * @brief Retrieve the latest cached motion sensor sample.
 *
 * @param sample Destination for the latest sample. Must not be NULL.
 *
 * @return true if @p sample was populated with a valid reading, false
 *         otherwise.
 */
bool motion_sensor_get_latest(struct motion_sensor_sample *sample);
