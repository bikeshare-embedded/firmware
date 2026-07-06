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

/** Network registration state, mapped from the modem's registration status. */
enum bike_lte_registration {
	/** Not registered and not currently searching for a network. */
	BIKE_LTE_REG_NOT_REGISTERED,
	/** Actively searching for a network to attach to. */
	BIKE_LTE_REG_SEARCHING,
	/** Registered on the home network. */
	BIKE_LTE_REG_REGISTERED_HOME,
	/** Registered while roaming. */
	BIKE_LTE_REG_REGISTERED_ROAMING,
	/** Network refused the registration attempt. */
	BIKE_LTE_REG_DENIED,
	/** SIM/UICC failure prevented registration. */
	BIKE_LTE_REG_UICC_FAIL,
	/** Registration status could not be determined. */
	BIKE_LTE_REG_UNKNOWN,
};

/** Active radio access technology reported by the modem. */
enum bike_lte_mode {
	/** No access technology active (detached). */
	BIKE_LTE_MODE_NONE,
	/** LTE-M (LTE Cat-M1). */
	BIKE_LTE_MODE_LTEM,
	/** NB-IoT. */
	BIKE_LTE_MODE_NBIOT,
	/** Access technology could not be determined. */
	BIKE_LTE_MODE_UNKNOWN,
};

/** Latest cached LTE modem status. */
struct bike_lte_status {
	/** True when the target includes modem LTE support. */
	bool supported;
	/** True after bike_lte_init() has completed successfully. */
	bool initialized;
	/** True while attach is in progress but not yet fully connected. */
	bool connecting;
	/** True when registered (home or roaming) and the PDN is active. */
	bool connected;
	/** True when the default packet data network context is active. */
	bool pdn_active;
	/** Current network registration state. */
	enum bike_lte_registration registration;
	/** Current radio access technology. */
	enum bike_lte_mode mode;
	/** Configured access point name, empty when the default APN is used. */
	char apn[BIKE_APN_MAX_LEN];
	/** Last modem/API error, or 0 when the latest operation succeeded. */
	int last_error;
	/** Serving cell identifier from the most recent cell update. */
	uint32_t cell_id;
	/** Tracking area code from the most recent cell update. */
	uint32_t tac;
};

/**
 * @brief Initialize the LTE modem.
 *
 * Initializes the nRF modem library and registers the network and default
 * PDN event handlers. Safe to call more than once; later calls are no-ops.
 *
 * @return 0 on success, -ENOTSUP when the target lacks LTE support, or a
 *         negative errno on modem initialization failure.
 */
int bike_lte_init(void);

/**
 * @brief Start connecting to the LTE network.
 *
 * Initializes the modem if needed, applies the configured APN, and starts
 * an asynchronous network attach. Returns once the attach has been started;
 * completion is reported asynchronously via bike_lte_status_get(). A no-op
 * that returns success when already connected or connecting.
 *
 * @return 0 on success, -ENOTSUP when the target lacks LTE support, or a
 *         negative errno on failure.
 */
int bike_lte_connect(void);

/**
 * @brief Disconnect from the LTE network.
 *
 * Sets the modem to offline (functional mode) and clears the connection
 * state. Safe to call when not initialized.
 *
 * @return 0 on success, -ENOTSUP when the target lacks LTE support, or a
 *         negative errno on failure.
 */
int bike_lte_disconnect(void);

/**
 * @brief Retrieve the latest cached LTE status.
 *
 * @param status Destination for the current status. Ignored when NULL.
 */
void bike_lte_status_get(struct bike_lte_status *status);

/**
 * @brief Return a human-readable name for a registration state.
 *
 * @param registration Registration state to name.
 *
 * @return Static, null-terminated string; never NULL.
 */
const char *bike_lte_registration_name(enum bike_lte_registration registration);

/**
 * @brief Return a human-readable name for a radio access technology.
 *
 * @param mode Access technology to name.
 *
 * @return Static, null-terminated string; never NULL.
 */
const char *bike_lte_mode_name(enum bike_lte_mode mode);
