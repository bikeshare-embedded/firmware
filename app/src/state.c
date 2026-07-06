/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "channels.h"
#include "config.h"
#include "state.h"

LOG_MODULE_REGISTER(bike_state, LOG_LEVEL_INF);

static enum bike_state_value current_state = BIKE_STATE_UNREGISTERED;
static char active_rental_id[BIKE_RENTAL_ID_MAX_LEN];
static int64_t trip_started_at_ms;
static struct k_work_delayable reservation_timeout_work;

static void publish_state_event(enum bike_state_event event)
{
	struct bike_state_msg msg = {
		.state = current_state,
		.event = event,
		.updated_at_ms = k_uptime_get(),
	};

	strncpy(msg.rental_id, active_rental_id, sizeof(msg.rental_id) - 1);
	msg.rental_id[sizeof(msg.rental_id) - 1] = '\0';

	(void)zbus_chan_pub(&bike_state_chan, &msg, K_NO_WAIT);
}

const char *bike_state_name(enum bike_state_value state)
{
	switch (state) {
	case BIKE_STATE_UNREGISTERED:
		return "UNREGISTERED";
	case BIKE_STATE_AVAILABLE:
		return "AVAILABLE";
	case BIKE_STATE_RESERVED:
		return "RESERVED";
	case BIKE_STATE_IN_USE:
		return "IN_USE";
	case BIKE_STATE_ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}

static void set_state_event(enum bike_state_value next_state,
			    enum bike_state_event event)
{
	if (current_state == next_state) {
		publish_state_event(event);
		return;
	}

	LOG_INF("State: %s -> %s", bike_state_name(current_state),
		bike_state_name(next_state));
	current_state = next_state;
	publish_state_event(event);
}

static void set_state(enum bike_state_value next_state)
{
	set_state_event(next_state, BIKE_STATE_EVENT_NONE);
}

static void clear_rental(void)
{
	active_rental_id[0] = '\0';
	trip_started_at_ms = 0;
	(void)k_work_cancel_delayable(&reservation_timeout_work);
}

bool bike_state_is_config_valid(void)
{
	return bike_config_is_valid(bike_config_get());
}

int bike_state_refresh_config(void)
{
	if (current_state == BIKE_STATE_ERROR ||
	    current_state == BIKE_STATE_RESERVED ||
	    current_state == BIKE_STATE_IN_USE) {
		return 0;
	}

	set_state(bike_state_is_config_valid() ? BIKE_STATE_AVAILABLE :
		  BIKE_STATE_UNREGISTERED);
	return 0;
}

static void reservation_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (current_state != BIKE_STATE_RESERVED) {
		return;
	}

	LOG_WRN("Reservation %s expired", active_rental_id);
	set_state_event(BIKE_STATE_AVAILABLE, BIKE_STATE_EVENT_RESERVATION_EXPIRED);
	clear_rental();
}

int bike_state_init(void)
{
	k_work_init_delayable(&reservation_timeout_work,
			      reservation_timeout_handler);
	current_state = bike_state_is_config_valid() ? BIKE_STATE_AVAILABLE :
			BIKE_STATE_UNREGISTERED;
	publish_state_event(current_state == BIKE_STATE_AVAILABLE ?
			    BIKE_STATE_EVENT_BICYCLE_ONLINE :
			    BIKE_STATE_EVENT_NONE);
	LOG_INF("Initial state: %s", bike_state_name(current_state));
	return 0;
}

enum bike_state_value bike_state_get(void)
{
	return current_state;
}

const char *bike_state_get_rental_id(void)
{
	return active_rental_id;
}

int bike_state_authorize(const char *rental_id)
{
	size_t len;

	if (!rental_id || !rental_id[0]) {
		LOG_WRN("RENT_AUTHORIZE rejected: rental_id missing");
		return -EINVAL;
	}

	len = strlen(rental_id);
	if (len >= sizeof(active_rental_id)) {
		LOG_WRN("RENT_AUTHORIZE rejected: rental_id too long");
		return -EINVAL;
	}

	if (current_state != BIKE_STATE_AVAILABLE) {
		LOG_WRN("RENT_AUTHORIZE rejected in state %s",
			bike_state_name(current_state));
		return -EACCES;
	}

	memcpy(active_rental_id, rental_id, len + 1);
	trip_started_at_ms = 0;
	(void)k_work_reschedule(&reservation_timeout_work,
				 K_SECONDS(BIKE_RESERVATION_TIMEOUT_SECONDS));
	set_state(BIKE_STATE_RESERVED);
	return 0;
}

int bike_state_cancel(const char *rental_id)
{
	if (current_state != BIKE_STATE_RESERVED && current_state != BIKE_STATE_IN_USE) {
		LOG_WRN("RENT_CANCEL rejected in state %s",
			bike_state_name(current_state));
		return -EACCES;
	}

	if (rental_id && rental_id[0] &&
	    strcmp(rental_id, active_rental_id) != 0) {
		LOG_WRN("RENT_CANCEL rejected: rental_id mismatch");
		return -EINVAL;
	}

	clear_rental();
	set_state(BIKE_STATE_AVAILABLE);
	return 0;
}

int bike_state_button_press(void)
{
	switch (current_state) {
	case BIKE_STATE_RESERVED:
		(void)k_work_cancel_delayable(&reservation_timeout_work);
		trip_started_at_ms = k_uptime_get();
		set_state_event(BIKE_STATE_IN_USE, BIKE_STATE_EVENT_RIDE_STARTED);
		return 0;
	case BIKE_STATE_IN_USE:
		LOG_INF("Trip finished after %lld ms",
			k_uptime_get() - trip_started_at_ms);
		set_state_event(BIKE_STATE_AVAILABLE, BIKE_STATE_EVENT_RIDE_ENDED);
		clear_rental();
		return 0;
	default:
		LOG_WRN("Button ignored in state %s",
			bike_state_name(current_state));
		return -EACCES;
	}
}

static void backend_command_listener(const struct zbus_channel *chan)
{
	const struct bike_backend_command_msg *msg = zbus_chan_const_msg(chan);

	switch (msg->type) {
	case BIKE_BACKEND_RENT_AUTHORIZE:
		(void)bike_state_authorize(msg->rental_id);
		break;
	case BIKE_BACKEND_RENT_CANCEL:
		(void)bike_state_cancel(msg->rental_id);
		break;
	default:
		LOG_WRN("Unknown backend command: %d", msg->type);
		break;
	}
}

static void button_event_listener(const struct zbus_channel *chan)
{
	ARG_UNUSED(chan);

	(void)bike_state_button_press();
}

ZBUS_LISTENER_DEFINE(backend_command_listener_node, backend_command_listener);
ZBUS_CHAN_ADD_OBS(backend_command_chan, backend_command_listener_node, 0);

ZBUS_LISTENER_DEFINE(button_event_listener_node, button_event_listener);
ZBUS_CHAN_ADD_OBS(button_event_chan, button_event_listener_node, 0);
