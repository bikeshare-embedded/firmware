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
#include <zephyr/sys/printk.h>

#include "button.h"
#include "config.h"
#include "gnss.h"
#include "led.h"
#include "lte.h"
#include "mqtt_client.h"
#include "sensor.h"
#include "state.h"
#include "telemetry.h"

LOG_MODULE_REGISTER(bikeshare, LOG_LEVEL_INF);

static void print_bike_banner(void);

int main(void)
{
	print_bike_banner();
	LOG_INF("Version: %s", CONFIG_APP_VERSION);

	/* Initialize modules */
	bike_config_init();
	bike_lte_init();
	bike_mqtt_init();
	bike_gnss_init();
	led_status_init();
	button_input_init();
	bike_state_init();
	bike_telemetry_init();
	motion_sensor_init();

	if (bike_config_is_valid(bike_config_get())) {
		(void)bike_lte_connect();
		(void)bike_mqtt_connect();
	}

	return 0;
}

static void print_bike_banner(void)
{
	printk("\n");
	printk("⠀⠀⠀⠀⠀⠀⠀⠀⠀⠛⠻⠗⢶⣶⣶⣦⣀⡀⠀⠀⠀⠀⠀⠀⠀⠀\n");
	printk("⠀⠀⠀⠀⣀⡀⣤⡄⠀⠀⠀⠀⢸⠟⡄⠀⠘⠉⠛⠀⠀⠀⠀⠀⠀⠀\n");
	printk("⠀⠀⠀⠀⠉⢻⠇⠀⠀⠀⡀⢴⠝⣄⣷⣀⠀⠀⠀ bikeshare.\n");
	printk("⠀⠀⠀⠀⠀⠈⣦⠀⡠⡪⢲⠃⡜⢉⣧⣿⣧⣤⣤⣤⣄⡀⠀⠀⠀⠀\n");
	printk("⠀⢀⣠⣴⣤⣴⣿⡕⠈⡠⣡⠌⠀⣼⢿⠿⢿⡏⠉⠛⠻⣿⣷⣤⠀⠀\n");
	printk("⢠⡾⠁⡴⠋⢿⣷⠃⡔⣱⠃⢀⣾⡟⢻⡆⠘⣷⠀⠀⠀⠀⠻⣿⣷⡀\n");
	printk("⣸⢁⣾⣅⣀⡜⢻⡜⡠⠁⠀⣼⣿⠀⠘⣿⣴⣻⡶⡄⠀⠀⠀⢹⣿⣧\n");
	printk("⢻⡘⢻⠀⠈⣿⣿⠗⠉⠀⠀⢿⣇⠀⠀⠈⣿⠛⢃⡇⠀⠀⠀⢸⣿⣿\n");
	printk("⠈⢷⣧⣨⣾⣯⡿⠀⠀⠀⠀⠸⣿⡄⠀⠀⠈⠉⠉⠀⠀⠀⢀⣿⣿⡏\n");
	printk("⠀⠀⠈⠉⠀⠀⠀⠀⠀⠀⠀⠀⠹⣿⣦⣀⠀⠀⠀⠀⢀⣠⣿⣿⠟⠀\n");
	printk("⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠙⠿⣿⣶⣶⣿⣿⠿⠛⠁⠀⠀\n");
	printk("\n");
}