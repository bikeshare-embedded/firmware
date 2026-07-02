/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Best-effort GNSS fix cache.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Latest cached GNSS status and fix. */
struct bike_gnss_fix {
	/** True when the target includes modem GNSS support. */
	bool supported;
	/** True after the background GNSS start path succeeds. */
	bool running;
	/** True when latitude, longitude, and altitude contain a valid fix. */
	bool valid;
	/** Timestamp of the cached status, in milliseconds since boot. */
	int64_t uptime_ms;
	/** Latitude in microdegrees when @ref valid is true. */
	int32_t latitude_microdegrees;
	/** Longitude in microdegrees when @ref valid is true. */
	int32_t longitude_microdegrees;
	/** Altitude above WGS-84 ellipsoid in millimeters when valid. */
	int32_t altitude_mm;
	/** Horizontal accuracy in millimeters when valid, or 0 if unknown. */
	uint32_t accuracy_mm;
	/** Number of satellites used in the latest valid fix. */
	uint8_t satellites_used;
	/** Last GNSS API error, or 0 when the latest operation succeeded. */
	int last_error;
};

/**
 * @brief Initialize GNSS sampling.
 *
 * Starts the modem GNSS path from background work when supported. Unsupported
 * targets remain initialized with an explicit no-fix status.
 */
void bike_gnss_init(void);

/**
 * @brief Retrieve the latest cached GNSS status.
 *
 * @param fix Destination for the latest cached status. Must not be NULL.
 *
 * @return true if @p fix contains a valid position fix, false otherwise.
 */
bool bike_gnss_get_latest(struct bike_gnss_fix *fix);

/**
 * @brief Store a GNSS status sample.
 *
 * This helper is public so tests can exercise telemetry transitions without
 * real modem hardware.
 */
void bike_gnss_store_fix(const struct bike_gnss_fix *fix);
