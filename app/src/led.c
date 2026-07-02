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

#if defined(CONFIG_GPIO)
#include <zephyr/drivers/gpio.h>
#define LED_STATUS_HAS_LED0 DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
#define LED_STATUS_HAS_LED1 DT_NODE_HAS_STATUS(DT_ALIAS(led1), okay)
#define LED_STATUS_HAS_LED2 DT_NODE_HAS_STATUS(DT_ALIAS(led2), okay)
#define LED_STATUS_HAS_LED3 DT_NODE_HAS_STATUS(DT_ALIAS(led3), okay)
#else
#define LED_STATUS_HAS_LED0 0
#define LED_STATUS_HAS_LED1 0
#define LED_STATUS_HAS_LED2 0
#define LED_STATUS_HAS_LED3 0
#endif

#define LED_STATUS_HAS_GPIO \
	(LED_STATUS_HAS_LED0 || LED_STATUS_HAS_LED1 || \
	 LED_STATUS_HAS_LED2 || LED_STATUS_HAS_LED3)

LOG_MODULE_REGISTER(led_status, LOG_LEVEL_INF);

#if LED_STATUS_HAS_LED0
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif
#if LED_STATUS_HAS_LED1
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#endif
#if LED_STATUS_HAS_LED2
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
#endif
#if LED_STATUS_HAS_LED3
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);
#endif

static const uint8_t chase_leds[] = { 0, 1, 3, 2 };
static const uint8_t chase_led_count = ARRAY_SIZE(chase_leds);
static enum led_status_pattern current_pattern = LED_STATUS_OFF;
static struct k_work_delayable blink_work;
static bool led_is_on;
static bool initialized;
static uint8_t active_chase_led;

enum led_status_pattern led_status_pattern_for_state(enum bike_state_value state)
{
	switch (state) {
	case BIKE_STATE_UNREGISTERED:
		return LED_STATUS_OFF;
	case BIKE_STATE_AVAILABLE:
		return LED_STATUS_BLINK_SLOW;
	case BIKE_STATE_RESERVED:
		return LED_STATUS_CHASE;
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
	case LED_STATUS_CHASE:
		return "CHASE";
	case LED_STATUS_ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}

#if LED_STATUS_HAS_GPIO
static int configure_led(const struct gpio_dt_spec *led, const char *name)
{
	if (!gpio_is_ready_dt(led)) {
		LOG_ERR("LED %s is not ready", name);
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);

	if (rc) {
		LOG_ERR("Failed to configure LED %s: %d", name, rc);
		return rc;
	}

	return 0;
}

static int configure_status_leds(void)
{
	int rc;

#if LED_STATUS_HAS_LED0
	rc = configure_led(&led0, "led0");
	if (rc) {
		return rc;
	}
#endif
#if LED_STATUS_HAS_LED1
	rc = configure_led(&led1, "led1");
	if (rc) {
		return rc;
	}
#endif
#if LED_STATUS_HAS_LED2
	rc = configure_led(&led2, "led2");
	if (rc) {
		return rc;
	}
#endif
#if LED_STATUS_HAS_LED3
	rc = configure_led(&led3, "led3");
	if (rc) {
		return rc;
	}
#endif

	return 0;
}

static void write_gpio_led(uint8_t index, bool on)
{
	switch (index) {
#if LED_STATUS_HAS_LED0
	case 0:
		(void)gpio_pin_set_dt(&led0, on ? 1 : 0);
		break;
#endif
#if LED_STATUS_HAS_LED1
	case 1:
		(void)gpio_pin_set_dt(&led1, on ? 1 : 0);
		break;
#endif
#if LED_STATUS_HAS_LED2
	case 2:
		(void)gpio_pin_set_dt(&led2, on ? 1 : 0);
		break;
#endif
#if LED_STATUS_HAS_LED3
	case 3:
		(void)gpio_pin_set_dt(&led3, on ? 1 : 0);
		break;
#endif
	default:
		break;
	}
}
#endif

static void write_led0(bool on)
{
	led_is_on = on;

#if LED_STATUS_HAS_GPIO
	if (initialized) {
		write_gpio_led(0, on);
	}
#endif
}

static void write_all_leds_off(void)
{
	led_is_on = false;

#if LED_STATUS_HAS_GPIO
	if (initialized) {
		for (uint8_t i = 0; i < chase_led_count; i++) {
			write_gpio_led(i, false);
		}
	}
#endif
}

static void write_chase_led(uint8_t index)
{
	active_chase_led = index % chase_led_count;
	led_is_on = true;

#if LED_STATUS_HAS_GPIO
	if (initialized) {
		for (uint8_t i = 0; i < chase_led_count; i++) {
			write_gpio_led(chase_leds[i], i == active_chase_led);
		}
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
	case LED_STATUS_CHASE:
		return K_MSEC(500);
	default:
		return K_FOREVER;
	}
}

static bool pattern_blinks(enum led_status_pattern pattern)
{
	return pattern == LED_STATUS_BLINK_SLOW ||
	       pattern == LED_STATUS_BLINK_FAST ||
	       pattern == LED_STATUS_ERROR ||
	       pattern == LED_STATUS_CHASE;
}

static void blink_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!pattern_blinks(current_pattern)) {
		return;
	}

	if (current_pattern == LED_STATUS_CHASE) {
		write_chase_led((active_chase_led + 1) % chase_led_count);
	} else {
		write_led0(!led_is_on);
	}

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
		write_all_leds_off();
		break;
	case LED_STATUS_SOLID_ON:
		write_all_leds_off();
		write_led0(true);
		break;
	case LED_STATUS_BLINK_SLOW:
	case LED_STATUS_BLINK_FAST:
	case LED_STATUS_ERROR:
		write_all_leds_off();
		write_led0(true);
		(void)k_work_reschedule(&blink_work, blink_interval(pattern));
		break;
	case LED_STATUS_CHASE:
		write_chase_led(0);
		(void)k_work_reschedule(&blink_work, blink_interval(pattern));
		break;
	default:
		write_all_leds_off();
		break;
	}

	LOG_INF("LED pattern: %s", led_status_pattern_name(pattern));
}

int led_status_init(void)
{
	k_work_init_delayable(&blink_work, blink_handler);

#if LED_STATUS_HAS_GPIO
	int rc = configure_status_leds();

	if (rc) {
		return rc;
	}
#else
	LOG_WRN("led0-led3 aliases unavailable; LED kept in logical-only mode");
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
