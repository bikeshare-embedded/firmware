/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Periodic telemetry sample publication.
 */

#pragma once

#include <stdint.h>

#include "channels.h"
#include "gnss.h"
#include "sensor.h"

/** Initialize and start periodic telemetry publication. */
void bike_telemetry_init(void);

/**
 * @brief Fill a telemetry sample from explicit inputs.
 *
 * This isolates the testable formatting/copying logic from the periodic work
 * item and from GNSS hardware availability.
 */
void bike_telemetry_fill_sample(struct telemetry_sample_msg *sample,
				const char *bike_id,
				enum bike_state_value state,
				const char *rental_id,
				int64_t uptime_ms,
				const struct bike_gnss_fix *gnss,
				const struct motion_sensor_sample *motion);
