#pragma once

#include <stdbool.h>

#define BIKE_RESERVATION_TIMEOUT_SECONDS 60

enum bike_state_value {
	BIKE_STATE_UNREGISTERED,
	BIKE_STATE_AVAILABLE,
	BIKE_STATE_RESERVED,
	BIKE_STATE_IN_USE,
	BIKE_STATE_ERROR,
};

int bike_state_init(void);
enum bike_state_value bike_state_get(void);
const char *bike_state_get_rental_id(void);
const char *bike_state_name(enum bike_state_value state);

int bike_state_refresh_config(void);
int bike_state_authorize(const char *rental_id);
int bike_state_cancel(const char *rental_id);
int bike_state_button_press(void);
bool bike_state_is_config_valid(void);
