/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief zbus channel and message definitions shared across the app.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#include "config.h"
#include "state.h"

/** Maximum length, including the terminator, of a rental ID string. */
#define BIKE_RENTAL_ID_MAX_LEN 32

/** Backend commands carried by @ref backend_command_chan. */
enum bike_backend_command_type {
	BIKE_BACKEND_RENT_AUTHORIZE,
	BIKE_BACKEND_RENT_CANCEL,
};

/** Message published on @ref button_event_chan for each button press. */
struct bike_button_event_msg {
	/** Timestamp of the press, in milliseconds since boot. */
	int64_t uptime_ms;
};

/** Message published on @ref backend_command_chan. */
struct bike_backend_command_msg {
	/** Command requested by the backend. */
	enum bike_backend_command_type type;
	/** Rental ID the command applies to. */
	char rental_id[BIKE_RENTAL_ID_MAX_LEN];
};

/** Message published on @ref bike_state_chan whenever the state changes. */
struct bike_state_msg {
	/** New state value. */
	enum bike_state_value state;
	/** Rental ID associated with the state, empty if there is none. */
	char rental_id[BIKE_RENTAL_ID_MAX_LEN];
	/** Timestamp of the transition, in milliseconds since boot. */
	int64_t updated_at_ms;
};

/** Message published on @ref telemetry_sample_chan. */
struct telemetry_sample_msg {
	/** Bike identifier. */
	char bike_id[BIKE_ID_MAX_LEN];
	/** Bike state at the time of the sample. */
	enum bike_state_value state;
	/** Timestamp of the sample, in milliseconds since boot. */
	int64_t uptime_ms;
	/** True if the GNSS fix backing this sample is valid. */
	bool gnss_fix_valid;
	/** Latitude in microdegrees when @ref gnss_fix_valid is true. */
	int32_t gnss_latitude_microdegrees;
	/** Longitude in microdegrees when @ref gnss_fix_valid is true. */
	int32_t gnss_longitude_microdegrees;
	/** Altitude above WGS-84 ellipsoid in millimeters when valid. */
	int32_t gnss_altitude_mm;
	/** Horizontal accuracy in millimeters when valid, or 0 if unknown. */
	uint32_t gnss_accuracy_mm;
};

/** Published on each debounced physical button press. */
ZBUS_CHAN_DECLARE(button_event_chan);
/** Published by the backend to request rental authorization or cancellation. */
ZBUS_CHAN_DECLARE(backend_command_chan);
/** Published on every bike state transition. */
ZBUS_CHAN_DECLARE(bike_state_chan);
/** Published with periodic telemetry samples. */
ZBUS_CHAN_DECLARE(telemetry_sample_chan);
