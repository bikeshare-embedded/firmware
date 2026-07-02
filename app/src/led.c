/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "channels.h"
#include "led.h"

#if defined(CONFIG_GPIO) && DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
#include <zephyr/drivers/gpio.h>
#define LED_STATUS_HAS_GPIO 1
#else
#define LED_STATUS_HAS_GPIO 0
#endif

LOG_MODULE_REGISTER(led_status, LOG_LEVEL_INF);

#if LED_STATUS_HAS_GPIO
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif

static enum led_status_pattern current_pattern = LED_STATUS_OFF;
static struct k_work_delayable blink_work;
static bool led_is_on;
static bool initialized;

enum led_status_pattern led_status_pattern_for_state(enum bike_state_value state)
{
	switch (state) {
	case BIKE_STATE_UNREGISTERED:
		return LED_STATUS_OFF;
	case BIKE_STATE_AVAILABLE:
		return LED_STATUS_BLINK_SLOW;
	case BIKE_STATE_RESERVED:
		return LED_STATUS_BLINK_FAST;
	case BIKE_STATE_IN_USE:
		return LED_STATUS_SOLID_ON;
	case BIKE_STATE_ERROR:
		return LED_STATUS_ERROR;
	default:
		return LED_STATUS_ERROR;
	}
}

enum led_status_pattern led_status_get_pattern(void)
{
	return current_pattern;
}

bool led_status_is_on(void)
{
	return led_is_on;
}

const char *led_status_pattern_name(enum led_status_pattern pattern)
{
	switch (pattern) {
	case LED_STATUS_OFF:
		return "OFF";
	case LED_STATUS_SOLID_ON:
		return "SOLID_ON";
	case LED_STATUS_BLINK_SLOW:
		return "BLINK_SLOW";
	case LED_STATUS_BLINK_FAST:
		return "BLINK_FAST";
	case LED_STATUS_ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}

static void write_led(bool on)
{
	led_is_on = on;

#if LED_STATUS_HAS_GPIO
	if (initialized) {
		(void)gpio_pin_set_dt(&led, on ? 1 : 0);
	}
#endif
}

static k_timeout_t blink_interval(enum led_status_pattern pattern)
{
	switch (pattern) {
	case LED_STATUS_BLINK_SLOW:
		return K_MSEC(1000);
	case LED_STATUS_BLINK_FAST:
		return K_MSEC(250);
	case LED_STATUS_ERROR:
		return K_MSEC(150);
	default:
		return K_FOREVER;
	}
}

static bool pattern_blinks(enum led_status_pattern pattern)
{
	return pattern == LED_STATUS_BLINK_SLOW ||
	       pattern == LED_STATUS_BLINK_FAST ||
	       pattern == LED_STATUS_ERROR;
}

static void blink_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!pattern_blinks(current_pattern)) {
		return;
	}

	write_led(!led_is_on);
	(void)k_work_reschedule(&blink_work, blink_interval(current_pattern));
}

static void apply_pattern(enum led_status_pattern pattern, bool force)
{
	if (!initialized) {
		current_pattern = pattern;
		return;
	}

	if (current_pattern == pattern && !force) {
		return;
	}

	current_pattern = pattern;
	(void)k_work_cancel_delayable(&blink_work);

	switch (pattern) {
	case LED_STATUS_OFF:
		write_led(false);
		break;
	case LED_STATUS_SOLID_ON:
		write_led(true);
		break;
	case LED_STATUS_BLINK_SLOW:
	case LED_STATUS_BLINK_FAST:
	case LED_STATUS_ERROR:
		write_led(true);
		(void)k_work_reschedule(&blink_work, blink_interval(pattern));
		break;
	default:
		write_led(false);
		break;
	}

	LOG_INF("LED pattern: %s", led_status_pattern_name(pattern));
}

int led_status_init(void)
{
	k_work_init_delayable(&blink_work, blink_handler);

#if LED_STATUS_HAS_GPIO
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED led0 is not ready");
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (rc) {
		LOG_ERR("Failed to configure LED led0: %d", rc);
		return rc;
	}
#else
	LOG_WRN("led0 alias unavailable; LED kept in logical-only mode");
#endif

	initialized = true;
	apply_pattern(current_pattern, true);
	return 0;
}

static void bike_state_listener(const struct zbus_channel *chan)
{
	const struct bike_state_msg *msg = zbus_chan_const_msg(chan);

	apply_pattern(led_status_pattern_for_state(msg->state), false);
}

ZBUS_LISTENER_DEFINE(led_status_state_listener, bike_state_listener);
ZBUS_CHAN_ADD_OBS(bike_state_chan, led_status_state_listener, 0);
