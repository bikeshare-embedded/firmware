/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief MQTT client bring-up and diagnostics.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define BIKE_MQTT_TOPIC_MAX_LEN 96

struct bike_mqtt_status {
	bool supported;
	bool initialized;
	bool connecting;
	bool connected;
	bool subscribed;
	char broker_host[BIKE_MQTT_HOST_MAX_LEN];
	uint16_t broker_port;
	char command_topic[BIKE_MQTT_TOPIC_MAX_LEN];
	int last_error;
	int disconnect_reason;
	uint32_t rx_count;
};

int bike_mqtt_init(void);
int bike_mqtt_connect(void);
int bike_mqtt_disconnect(void);
void bike_mqtt_status_get(struct bike_mqtt_status *status);
