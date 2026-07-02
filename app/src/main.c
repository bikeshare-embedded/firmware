/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Application entry point.
 *
 * @authors ruantmelo@gmail.com & vcn0510@gmail.com
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "button.h"
#include "config.h"
#include "led.h"
#include "lte.h"
#include "sensor.h"
#include "state.h"

LOG_MODULE_REGISTER(bikeshare, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Bikeshare. Version: %s", CONFIG_APP_VERSION);

	/* Initialize modules */
	bike_config_init();
	bike_lte_init();
	led_status_init();
	button_input_init();
	bike_state_init();
	// motion_sensor_init();

	if (bike_config_is_valid(bike_config_get())) {
		(void)bike_lte_connect();
	}

	return 0;
}
