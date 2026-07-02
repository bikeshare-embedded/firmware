/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED status indicator.
 *
 * Drives the status LED (devicetree alias `led0`) with a pattern derived
 * from the current @ref bike_state_value.
 */

#pragma once

#include "state.h"

/** Blink/solid patterns the status LED can display. */
enum led_status_pattern {
	LED_STATUS_OFF,
	LED_STATUS_SOLID_ON,
	LED_STATUS_BLINK_SLOW,
	LED_STATUS_BLINK_FAST,
	LED_STATUS_ERROR,
};

/**
 * @brief Initialize the LED status module.
 *
 * Configures the status LED GPIO, if available in the devicetree, and
 * applies the pattern matching the current bike state.
 *
 * @retval 0 on success.
 * @retval -ENODEV if the configured GPIO device is not ready.
 * @return Negative errno code on other failures.
 */
int led_status_init(void);

/**
 * @brief Map a bike state to its corresponding LED pattern.
 *
 * @param state Bike state to map.
 *
 * @return LED pattern associated with @p state.
 */
enum led_status_pattern led_status_pattern_for_state(enum bike_state_value state);

/**
 * @brief Get the LED pattern currently being displayed.
 *
 * @return Current LED pattern.
 */
enum led_status_pattern led_status_get_pattern(void);

/**
 * @brief Check whether the status LED is currently lit.
 *
 * @return true if the LED is on, false otherwise.
 */
bool led_status_is_on(void);

/**
 * @brief Get the human-readable name of an LED pattern.
 *
 * @param pattern Pattern to describe.
 *
 * @return Static string naming @p pattern.
 */
const char *led_status_pattern_name(enum led_status_pattern pattern);
