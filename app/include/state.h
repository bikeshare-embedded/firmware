/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Bike rental state machine.
 *
 * Tracks the bike's rental lifecycle (unregistered/available/reserved/
 * in-use/error) and reacts to backend commands and button presses
 * published on the zbus channels declared in channels.h.
 */

#pragma once

#include <stdbool.h>

/** Seconds a reservation is held before automatically expiring. */
#define BIKE_RESERVATION_TIMEOUT_SECONDS 60

/** Rental lifecycle states of the bike. */
enum bike_state_value {
	BIKE_STATE_UNREGISTERED,
	BIKE_STATE_AVAILABLE,
	BIKE_STATE_RESERVED,
	BIKE_STATE_IN_USE,
	BIKE_STATE_ERROR,
};

/**
 * @brief Initialize the bike state machine.
 *
 * Sets the initial state based on whether the current configuration is
 * valid, and publishes it on @ref bike_state_chan.
 *
 * @return 0 on success.
 */
int bike_state_init(void);

/**
 * @brief Get the current bike state.
 *
 * @return Current state value.
 */
enum bike_state_value bike_state_get(void);

/**
 * @brief Get the rental ID of the active reservation or trip.
 *
 * @return Pointer to the rental ID string, empty if there is none.
 */
const char *bike_state_get_rental_id(void);

/**
 * @brief Get the human-readable name of a state value.
 *
 * @param state State to describe.
 *
 * @return Static string naming @p state.
 */
const char *bike_state_name(enum bike_state_value state);

/**
 * @brief Re-evaluate the state after a configuration change.
 *
 * Moves between BIKE_STATE_UNREGISTERED and BIKE_STATE_AVAILABLE
 * depending on configuration validity. Has no effect while a rental is
 * reserved, in use, or in error.
 *
 * @return 0 on success.
 */
int bike_state_refresh_config(void);

/**
 * @brief Authorize a rental reservation.
 *
 * Only accepted while the bike is BIKE_STATE_AVAILABLE; transitions it
 * to BIKE_STATE_RESERVED and arms the reservation timeout.
 *
 * @param rental_id Rental identifier to associate with the reservation.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p rental_id is missing or too long.
 * @retval -EACCES if the bike is not currently available.
 */
int bike_state_authorize(const char *rental_id);

/**
 * @brief Cancel a reserved rental.
 *
 * Only accepted while the bike is BIKE_STATE_RESERVED and, if
 * @p rental_id is given, matches the active reservation.
 *
 * @param rental_id Rental identifier to cancel, or NULL/empty to cancel
 *                  the active reservation unconditionally.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p rental_id does not match the active reservation.
 * @retval -EACCES if the bike is not currently reserved.
 */
int bike_state_cancel(const char *rental_id);

/**
 * @brief Handle a physical button press.
 *
 * Starts a trip when reserved, or ends the active trip when in use.
 *
 * @retval 0 on success.
 * @retval -EACCES if the button press is not valid in the current state.
 */
int bike_state_button_press(void);

/**
 * @brief Check whether the current configuration is valid.
 *
 * @return true if the configuration is valid, false otherwise.
 */
bool bike_state_is_config_valid(void);
