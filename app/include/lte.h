/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LTE modem bring-up and diagnostics.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

enum bike_lte_registration {
	BIKE_LTE_REG_NOT_REGISTERED,
	BIKE_LTE_REG_SEARCHING,
	BIKE_LTE_REG_REGISTERED_HOME,
	BIKE_LTE_REG_REGISTERED_ROAMING,
	BIKE_LTE_REG_DENIED,
	BIKE_LTE_REG_UICC_FAIL,
	BIKE_LTE_REG_UNKNOWN,
};

enum bike_lte_mode {
	BIKE_LTE_MODE_NONE,
	BIKE_LTE_MODE_LTEM,
	BIKE_LTE_MODE_NBIOT,
	BIKE_LTE_MODE_UNKNOWN,
};

struct bike_lte_status {
	bool supported;
	bool initialized;
	bool connecting;
	bool connected;
	enum bike_lte_registration registration;
	enum bike_lte_mode mode;
	char apn[BIKE_APN_MAX_LEN];
	int last_error;
	uint32_t cell_id;
	uint32_t tac;
};

int bike_lte_init(void);
int bike_lte_connect(void);
int bike_lte_disconnect(void);
void bike_lte_status_get(struct bike_lte_status *status);

const char *bike_lte_registration_name(enum bike_lte_registration registration);
const char *bike_lte_mode_name(enum bike_lte_mode mode);
