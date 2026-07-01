#pragma once

#include "state.h"

enum led_status_pattern {
	LED_STATUS_OFF,
	LED_STATUS_SOLID_ON,
	LED_STATUS_BLINK_SLOW,
	LED_STATUS_BLINK_FAST,
	LED_STATUS_ERROR,
};

int led_status_init(void);
enum led_status_pattern led_status_pattern_for_state(enum bike_state_value state);
enum led_status_pattern led_status_get_pattern(void);
const char *led_status_pattern_name(enum led_status_pattern pattern);
