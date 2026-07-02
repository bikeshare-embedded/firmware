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
#include "button.h"

#if defined(CONFIG_GPIO) && DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
#include <zephyr/drivers/gpio.h>
#define BUTTON_INPUT_HAS_GPIO 1
#else
#define BUTTON_INPUT_HAS_GPIO 0
#endif

LOG_MODULE_REGISTER(button_input, LOG_LEVEL_INF);

#if BUTTON_INPUT_HAS_GPIO
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;
#endif

static struct k_work button_work;
static int64_t last_published_press_ms = -BUTTON_INPUT_DEBOUNCE_MS;

int button_input_publish_press(int64_t uptime_ms)
{
	struct bike_button_event_msg msg = {
		.uptime_ms = uptime_ms,
	};

	return zbus_chan_pub(&button_event_chan, &msg, K_MSEC(100));
}

int button_input_publish_press_debounced(int64_t uptime_ms)
{
	if ((uptime_ms - last_published_press_ms) < BUTTON_INPUT_DEBOUNCE_MS) {
		LOG_WRN("Button ignored due to debounce");
		return -EALREADY;
	}

	int rc = button_input_publish_press(uptime_ms);

	if (!rc) {
		last_published_press_ms = uptime_ms;
	}

	return rc;
}

void button_input_reset_debounce(void)
{
	last_published_press_ms = -BUTTON_INPUT_DEBOUNCE_MS;
}

static void button_work_handler(struct k_work *work)
{
	int rc;

	ARG_UNUSED(work);

	rc = button_input_publish_press_debounced(k_uptime_get());
	if (rc) {
		LOG_WRN("Failed to publish button press: %d", rc);
	}
}

#if BUTTON_INPUT_HAS_GPIO
static void button_gpio_callback(const struct device *dev,
				 struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit(&button_work);
}
#endif

int button_input_init(void)
{
	k_work_init(&button_work, button_work_handler);

#if BUTTON_INPUT_HAS_GPIO
	int rc;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Button sw0 is not ready");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (rc) {
		LOG_ERR("Failed to configure button sw0: %d", rc);
		return rc;
	}

	gpio_init_callback(&button_cb, button_gpio_callback, BIT(button.pin));
	rc = gpio_add_callback(button.port, &button_cb);
	if (rc) {
		LOG_ERR("Failed to register callback for sw0: %d", rc);
		return rc;
	}

	rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) {
		LOG_ERR("Failed to configure interrupt for sw0: %d", rc);
		return rc;
	}

	LOG_INF("Button sw0 initialized");
#else
	LOG_WRN("sw0 alias unavailable; physical button disabled");
#endif

	return 0;
}
