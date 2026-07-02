/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gnss.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bike_gnss, LOG_LEVEL_INF);

static struct bike_gnss_fix latest_fix;
static struct k_mutex latest_lock;

void bike_gnss_store_fix(const struct bike_gnss_fix *fix)
{
	if (fix == NULL) {
		return;
	}

	k_mutex_lock(&latest_lock, K_FOREVER);
	latest_fix = *fix;
	k_mutex_unlock(&latest_lock);
}

bool bike_gnss_get_latest(struct bike_gnss_fix *fix)
{
	if (fix == NULL) {
		return false;
	}

	k_mutex_lock(&latest_lock, K_FOREVER);
	*fix = latest_fix;
	k_mutex_unlock(&latest_lock);

	return fix->valid;
}

#if defined(CONFIG_BIKE_GNSS) && defined(CONFIG_NRF_MODEM_LIB)

#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>

static struct k_work start_work;
static struct nrf_modem_gnss_pvt_data_frame last_pvt;
static struct nrf_modem_gnss_pvt_data_frame current_pvt;
static int gnss_event_count;
static uint8_t period_satellites_tracked;

static int32_t degrees_to_microdegrees(double degrees)
{
	return (int32_t)(degrees * 1000000.0);
}

static int32_t meters_to_millimeters(float meters)
{
	return (int32_t)(meters * 1000.0f);
}

static uint8_t count_satellites_used(const struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	uint8_t count = 0;

	for (size_t i = 0; i < NRF_MODEM_GNSS_MAX_SATELLITES; i++) {
		if (pvt->sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_USED_IN_FIX) {
			count++;
		}
	}

	return count;
}

static uint8_t count_satellites_tracked(const struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	uint8_t count = 0;

	for (size_t i = 0; i < NRF_MODEM_GNSS_MAX_SATELLITES; i++) {
		if (pvt->sv[i].sv > 0) {
			count++;
		}
	}

	return count;
}

static void store_no_fix(int error)
{
	struct bike_gnss_fix fix = {
		.supported = true,
		.running = error == 0,
		.valid = false,
		.uptime_ms = k_uptime_get(),
		.last_error = error,
	};

	bike_gnss_store_fix(&fix);
}

static void store_valid_fix(const struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	int32_t speed_cm_s = (int32_t)(pvt->speed * 100.0f);
	struct bike_gnss_fix fix = {
		.supported = true,
		.running = true,
		.valid = true,
		.uptime_ms = k_uptime_get(),
		.latitude_microdegrees = degrees_to_microdegrees(pvt->latitude),
		.longitude_microdegrees = degrees_to_microdegrees(pvt->longitude),
		.altitude_mm = meters_to_millimeters(pvt->altitude),
		.accuracy_mm = (uint32_t)meters_to_millimeters(pvt->accuracy),
		.satellites_used = count_satellites_used(pvt),
		.last_error = 0,
	};

	bike_gnss_store_fix(&fix);
	LOG_INF("GNSS fix updated: lat=%d lon=%d speed=%d.%02d m/s",
		fix.latitude_microdegrees, fix.longitude_microdegrees, speed_cm_s / 100,
		speed_cm_s % 100);
}

static void read_and_store_pvt(bool require_fix)
{
	int ret = nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt), NRF_MODEM_GNSS_DATA_PVT);

	if (ret != 0) {
		store_no_fix(ret);
		LOG_WRN("GNSS PVT read failed: %d", ret);
		return;
	}

	if ((last_pvt.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) == 0) {
		if (require_fix) {
			LOG_DBG("GNSS PVT data did not contain a valid fix");
		}
		store_no_fix(0);
		return;
	}

	current_pvt = last_pvt;
	store_valid_fix(&current_pvt);
}

static void handle_pvt_event(void)
{
	uint8_t satellites_tracked;

	read_and_store_pvt(false);

	gnss_event_count++;
	satellites_tracked = count_satellites_tracked(&last_pvt);
	if (satellites_tracked > period_satellites_tracked) {
		period_satellites_tracked = satellites_tracked;
	}

	if (gnss_event_count == 1) {
		LOG_INF("Searching for GNSS satellites");
		LOG_INF("Number of satellites: %u", period_satellites_tracked);
	} else if (gnss_event_count >= 30) {
		gnss_event_count = 0;
		period_satellites_tracked = 0;
	}
}

static void reset_search_period(void)
{
	gnss_event_count = 0;
	period_satellites_tracked = 0;
}

static void gnss_event_handler(int event)
{
	switch (event) {
	case NRF_MODEM_GNSS_EVT_PVT:
		handle_pvt_event();
		break;
	case NRF_MODEM_GNSS_EVT_FIX:
		LOG_INF("GNSS fix event");
		read_and_store_pvt(true);
		reset_search_period();
		break;
	case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
		LOG_INF("GNSS woke up in periodic mode");
		break;
	case NRF_MODEM_GNSS_EVT_BLOCKED:
		LOG_INF("GNSS is blocked by LTE");
		break;
	case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX:
		LOG_INF("GNSS sleeping after fix");
		reset_search_period();
		break;
	case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_TIMEOUT:
		LOG_INF("GNSS sleeping after fix retry timeout");
		store_no_fix(0);
		break;
	default:
		break;
	}
}

static void start_work_handler(struct k_work *work)
{
	uint8_t use_case = NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START;
	int ret;

	ARG_UNUSED(work);

	ret = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);
	if (ret != 0) {
		store_no_fix(ret);
		LOG_WRN("Failed to activate GNSS functional mode: %d", ret);
		return;
	}

	ret = nrf_modem_gnss_event_handler_set(gnss_event_handler);
	if (ret != 0) {
		store_no_fix(ret);
		LOG_WRN("Failed to set GNSS event handler: %d", ret);
		return;
	}

	if (IS_ENABLED(CONFIG_BIKE_GNSS_LOW_ACCURACY)) {
		use_case |= NRF_MODEM_GNSS_USE_CASE_LOW_ACCURACY;
	}

	ret = nrf_modem_gnss_use_case_set(use_case);
	if (ret != 0) {
		LOG_WRN("Failed to set GNSS use case: %d", ret);
	}

	ret = nrf_modem_gnss_fix_retry_set(CONFIG_BIKE_GNSS_FIX_RETRY_SECONDS);
	if (ret != 0) {
		store_no_fix(ret);
		LOG_WRN("Failed to set GNSS fix retry: %d", ret);
		return;
	}

	ret = nrf_modem_gnss_fix_interval_set(CONFIG_BIKE_GNSS_FIX_INTERVAL_SECONDS);
	if (ret != 0) {
		store_no_fix(ret);
		LOG_WRN("Failed to set GNSS fix interval: %d", ret);
		return;
	}

	ret = nrf_modem_gnss_start();
	if (ret != 0) {
		store_no_fix(ret);
		LOG_WRN("Failed to start GNSS: %d", ret);
		return;
	}

	if (IS_ENABLED(CONFIG_BIKE_GNSS_PRIORITY_MODE)) {
		ret = nrf_modem_gnss_prio_mode_enable();
		if (ret != 0) {
			LOG_WRN("Failed to enable GNSS priority mode: %d", ret);
		}
	}

	store_no_fix(0);
	LOG_INF("GNSS background sampling started");
}

void bike_gnss_init(void)
{
	struct bike_gnss_fix initial = {
		.supported = true,
		.uptime_ms = k_uptime_get(),
	};

	k_mutex_init(&latest_lock);
	bike_gnss_store_fix(&initial);
	k_work_init(&start_work, start_work_handler);
	(void)k_work_submit(&start_work);
}

#else

void bike_gnss_init(void)
{
	struct bike_gnss_fix initial = {
		.supported = false,
		.running = false,
		.valid = false,
		.uptime_ms = k_uptime_get(),
		.last_error = -ENOTSUP,
	};

	k_mutex_init(&latest_lock);
	bike_gnss_store_fix(&initial);
	LOG_INF("GNSS disabled on this target");
}

#endif
