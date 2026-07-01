#pragma once

#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#include "config.h"
#include "state.h"

#define BIKE_RENTAL_ID_MAX_LEN 32

enum bike_backend_command_type {
	BIKE_BACKEND_RENT_AUTHORIZE,
	BIKE_BACKEND_RENT_CANCEL,
};

struct bike_button_event_msg {
	int64_t uptime_ms;
};

struct bike_backend_command_msg {
	enum bike_backend_command_type type;
	char rental_id[BIKE_RENTAL_ID_MAX_LEN];
};

struct bike_state_msg {
	enum bike_state_value state;
	char rental_id[BIKE_RENTAL_ID_MAX_LEN];
	int64_t updated_at_ms;
};

struct telemetry_sample_msg {
	char bike_id[BIKE_ID_MAX_LEN];
	enum bike_state_value state;
	int64_t uptime_ms;
	bool gnss_fix_valid;
};

ZBUS_CHAN_DECLARE(button_event_chan);
ZBUS_CHAN_DECLARE(backend_command_chan);
ZBUS_CHAN_DECLARE(bike_state_chan);
ZBUS_CHAN_DECLARE(telemetry_sample_chan);
