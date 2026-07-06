/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "config.h"
#include "lte.h"

LOG_MODULE_REGISTER(bike_lte, LOG_LEVEL_INF);

static struct bike_lte_status current_status = {
#if defined(CONFIG_LTE_LINK_CONTROL) && defined(CONFIG_NRF_MODEM_LIB)
	.supported = true,
#else
	.supported = false,
#endif
	.registration = BIKE_LTE_REG_NOT_REGISTERED,
	.mode = BIKE_LTE_MODE_NONE,
};

const char *bike_lte_registration_name(enum bike_lte_registration registration)
{
	switch (registration) {
	case BIKE_LTE_REG_NOT_REGISTERED:
		return "not_registered";
	case BIKE_LTE_REG_SEARCHING:
		return "searching";
	case BIKE_LTE_REG_REGISTERED_HOME:
		return "registered_home";
	case BIKE_LTE_REG_REGISTERED_ROAMING:
		return "registered_roaming";
	case BIKE_LTE_REG_DENIED:
		return "denied";
	case BIKE_LTE_REG_UICC_FAIL:
		return "uicc_fail";
	case BIKE_LTE_REG_UNKNOWN:
	default:
		return "unknown";
	}
}

const char *bike_lte_mode_name(enum bike_lte_mode mode)
{
	switch (mode) {
	case BIKE_LTE_MODE_NONE:
		return "none";
	case BIKE_LTE_MODE_LTEM:
		return "lte-m";
	case BIKE_LTE_MODE_NBIOT:
		return "nb-iot";
	case BIKE_LTE_MODE_UNKNOWN:
	default:
		return "unknown";
	}
}

void bike_lte_status_get(struct bike_lte_status *status)
{
	if (!status) {
		return;
	}

	*status = current_status;
}

#if defined(CONFIG_LTE_LINK_CONTROL) && defined(CONFIG_NRF_MODEM_LIB)

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <modem/pdn.h>

static bool default_pdn_active;

static enum bike_lte_registration map_registration(enum lte_lc_nw_reg_status status)
{
	switch (status) {
	case LTE_LC_NW_REG_NOT_REGISTERED:
		return BIKE_LTE_REG_NOT_REGISTERED;
	case LTE_LC_NW_REG_SEARCHING:
		return BIKE_LTE_REG_SEARCHING;
	case LTE_LC_NW_REG_REGISTERED_HOME:
		return BIKE_LTE_REG_REGISTERED_HOME;
	case LTE_LC_NW_REG_REGISTERED_ROAMING:
		return BIKE_LTE_REG_REGISTERED_ROAMING;
	case LTE_LC_NW_REG_REGISTRATION_DENIED:
		return BIKE_LTE_REG_DENIED;
	case LTE_LC_NW_REG_UICC_FAIL:
		return BIKE_LTE_REG_UICC_FAIL;
	case LTE_LC_NW_REG_UNKNOWN:
	default:
		return BIKE_LTE_REG_UNKNOWN;
	}
}

static enum bike_lte_mode map_lte_mode(enum lte_lc_lte_mode mode)
{
	switch (mode) {
	case LTE_LC_LTE_MODE_NONE:
		return BIKE_LTE_MODE_NONE;
	case LTE_LC_LTE_MODE_LTEM:
		return BIKE_LTE_MODE_LTEM;
	case LTE_LC_LTE_MODE_NBIOT:
		return BIKE_LTE_MODE_NBIOT;
	default:
		return BIKE_LTE_MODE_UNKNOWN;
	}
}

static bool is_registered(enum bike_lte_registration registration)
{
	return registration == BIKE_LTE_REG_REGISTERED_HOME ||
	       registration == BIKE_LTE_REG_REGISTERED_ROAMING;
}

static void update_connection_state(void)
{
	current_status.pdn_active = default_pdn_active;
	current_status.connected = is_registered(current_status.registration) &&
				   default_pdn_active;
	current_status.connecting = !current_status.connected &&
				    (current_status.registration == BIKE_LTE_REG_SEARCHING ||
				     is_registered(current_status.registration));
}

static void pdn_event_handler(uint8_t cid, enum pdn_event event, int reason)
{
	ARG_UNUSED(cid);

	switch (event) {
	case PDN_EVENT_ACTIVATED:
		default_pdn_active = true;
		current_status.last_error = 0;
		LOG_INF("Default PDN activated");
		break;
	case PDN_EVENT_DEACTIVATED:
	case PDN_EVENT_NETWORK_DETACH:
		default_pdn_active = false;
		LOG_WRN("Default PDN deactivated");
		break;
	case PDN_EVENT_CNEC_ESM:
		default_pdn_active = false;
		current_status.last_error = -ENETUNREACH;
#if defined(CONFIG_PDN_ESM_STRERROR)
		LOG_ERR("Default PDN ESM error: %d (%s)", reason,
			pdn_esm_strerror(reason));
#else
		LOG_ERR("Default PDN ESM error: %d", reason);
#endif
		break;
	default:
		break;
	}

	update_connection_state();
}

static void lte_event_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		current_status.registration = map_registration(evt->nw_reg_status);
		update_connection_state();
		LOG_INF("LTE registration: %s",
			bike_lte_registration_name(current_status.registration));
		break;
	case LTE_LC_EVT_LTE_MODE_UPDATE:
		current_status.mode = map_lte_mode(evt->lte_mode);
		LOG_INF("LTE mode: %s", bike_lte_mode_name(current_status.mode));
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		current_status.cell_id = evt->cell.id;
		current_status.tac = evt->cell.tac;
		LOG_INF("LTE cell updated: id=%u tac=%u",
			current_status.cell_id, current_status.tac);
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_INF("LTE RRC mode: %s",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
				"connected" : "idle");
		break;
	default:
		break;
	}
}

int bike_lte_init(void)
{
	int rc;

	if (current_status.initialized) {
		return 0;
	}

	rc = nrf_modem_lib_init();
	if (rc) {
		current_status.last_error = rc;
		LOG_ERR("Failed to initialize modem library: %d", rc);
		return rc;
	}

	lte_lc_register_handler(lte_event_handler);
	rc = pdn_default_ctx_cb_reg(pdn_event_handler);
	if (rc) {
		current_status.last_error = rc;
		LOG_ERR("Failed to register default PDN callback: %d", rc);
		return rc;
	}

	current_status.initialized = true;
	current_status.last_error = 0;
	LOG_INF("LTE modem initialized");
	return 0;
}

int bike_lte_connect(void)
{
	const char *apn = bike_config_get_apn();
	int rc;

	if (!current_status.initialized) {
		rc = bike_lte_init();
		if (rc) {
			return rc;
		}
	}

	if (current_status.connected || current_status.connecting) {
		current_status.last_error = 0;
		return 0;
	}

	if (apn[0]) {
		rc = pdn_ctx_configure(0, apn, PDN_FAM_IPV4, NULL);
		if (rc) {
			current_status.last_error = rc;
			LOG_ERR("Failed to configure APN '%s': %d", apn, rc);
			return rc;
		}

		strncpy(current_status.apn, apn, sizeof(current_status.apn) - 1);
		current_status.apn[sizeof(current_status.apn) - 1] = '\0';
		LOG_INF("Default PDN APN configured for IPv4: %s", apn);
	}

	rc = lte_lc_connect_async(NULL);
	if (rc == -EINPROGRESS) {
		current_status.connecting = true;
		current_status.last_error = 0;
		return 0;
	}
	if (rc) {
		current_status.last_error = rc;
		LOG_ERR("Failed to start LTE attach: %d", rc);
		return rc;
	}

	current_status.connecting = true;
	current_status.registration = BIKE_LTE_REG_SEARCHING;
	current_status.last_error = 0;
	LOG_INF("LTE attach started");
	return 0;
}

int bike_lte_disconnect(void)
{
	int rc;

	if (!current_status.initialized) {
		return 0;
	}

	rc = lte_lc_offline();
	if (rc) {
		current_status.last_error = rc;
		LOG_ERR("Failed to set LTE offline: %d", rc);
		return rc;
	}

	current_status.connecting = false;
	current_status.connected = false;
	default_pdn_active = false;
	current_status.pdn_active = false;
	current_status.registration = BIKE_LTE_REG_NOT_REGISTERED;
	current_status.last_error = 0;
	LOG_INF("LTE disconnected");
	return 0;
}

#else

int bike_lte_init(void)
{
	current_status.last_error = -ENOTSUP;
	return -ENOTSUP;
}

int bike_lte_connect(void)
{
	current_status.last_error = -ENOTSUP;
	return -ENOTSUP;
}

int bike_lte_disconnect(void)
{
	current_status.last_error = -ENOTSUP;
	return -ENOTSUP;
}

#endif
