/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Physical button input handling.
 *
 * Debounces the physical button (devicetree alias `sw0`) and publishes
 * press events on @ref button_event_chan.
 */

#pragma once

#include <stdint.h>

/** Minimum time between two accepted button presses, in milliseconds. */
#define BUTTON_INPUT_DEBOUNCE_MS 200

/**
 * @brief Initialize the button input module.
 *
 * Configures the GPIO interrupt for the physical button, if available in
 * the devicetree. If no button is available, the module still initializes
 * successfully but never publishes press events on its own.
 *
 * @retval 0 on success.
 * @retval -ENODEV if the configured GPIO device is not ready.
 * @return Negative errno code on other failures.
 */
int button_input_init(void);

/**
 * @brief Publish a button press event, bypassing debounce.
 *
 * @param uptime_ms Timestamp of the press, in milliseconds since boot.
 *
 * @return 0 on success, negative errno code on failure.
 */
int button_input_publish_press(int64_t uptime_ms);

/**
 * @brief Publish a button press event, applying debounce.
 *
 * Presses that occur less than @ref BUTTON_INPUT_DEBOUNCE_MS after the
 * last accepted press are rejected.
 *
 * @param uptime_ms Timestamp of the press, in milliseconds since boot.
 *
 * @retval 0 on success.
 * @retval -EALREADY if the press was rejected due to debounce.
 * @return Negative errno code on other failures.
 */
int button_input_publish_press_debounced(int64_t uptime_ms);

/**
 * @brief Reset the debounce timer.
 *
 * The next call to button_input_publish_press_debounced() is guaranteed
 * to be accepted regardless of timing.
 */
void button_input_reset_debounce(void);
