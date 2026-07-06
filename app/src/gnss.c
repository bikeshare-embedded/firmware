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

#define GNSS_FIX_MAX_AGE_MS \
	((int64_t)CONFIG_BIKE_GNSS_FIX_MAX_AGE_SECONDS * MSEC_PER_SEC)

static struct bike_gnss_fix latest_fix;
static struct bike_gnss_fix last_valid_fix;
static struct k_mutex latest_lock;

void bike_gnss_store_fix(const struct bike_gnss_fix *fix)
{
	if (fix == NULL) {
		return;
	}

	k_mutex_lock(&latest_lock, K_FOREVER);
	latest_fix = *fix;
	if (fix->valid) {
		last_valid_fix = *fix;
	}
	k_mutex_unlock(&latest_lock);
}

static bool fix_is_fresh(const struct bike_gnss_fix *fix)
{
	return fix->valid &&
	       (k_uptime_get() - fix->uptime_ms) < GNSS_FIX_MAX_AGE_MS;
}

bool bike_gnss_get_latest(struct bike_gnss_fix *fix)
{
	if (fix == NULL) {
		return false;
	}

	/*
	 * LTE activity briefly blocks GNSS and produces no-fix samples right
	 * after good ones. Keep reporting the last valid position until it
	 * goes stale instead of flapping between a fix and none.
	 */
	k_mutex_lock(&latest_lock, K_FOREVER);
	if (latest_fix.valid || !fix_is_fresh(&last_valid_fix)) {
		*fix = latest_fix;
	} else {
		*fix = last_valid_fix;
		fix->retained = true;
	}
	k_mutex_unlock(&latest_lock);

	return fix->valid;
}

#if defined(CONFIG_BIKE_GNSS) && defined(CONFIG_NRF_MODEM_LIB)

#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>

/* The modem drops GNSS priority on its own after a fix or after 40 s. */
#define GNSS_PRIO_WINDOW_SECONDS 40
/* Signals a failed PVT read; must stay above every NRF_MODEM_GNSS_EVT_*. */
#define GNSS_EVT_READ_FAILED_BIT 31

static struct k_work start_work;
static struct k_work event_work;
static struct k_work_delayable duty_work;
static struct nrf_modem_gnss_pvt_data_frame isr_pvt;
static atomic_t pending_events;
static int gnss_event_count;
static uint8_t period_satellites_tracked;
static bool prio_logged;

K_MSGQ_DEFINE(pvt_msgq, sizeof(struct nrf_modem_gnss_pvt_data_frame), 4, 4);

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
	int32_t speed_milli_m_s = (int32_t)(pvt->speed * 1000.0f);
	struct bike_gnss_fix fix = {
		.supported = true,
		.running = true,
		.valid = true,
		.uptime_ms = k_uptime_get(),
		.latitude_microdegrees = degrees_to_microdegrees(pvt->latitude),
		.longitude_microdegrees = degrees_to_microdegrees(pvt->longitude),
		.altitude_mm = meters_to_millimeters(pvt->altitude),
		.accuracy_mm = (uint32_t)meters_to_millimeters(pvt->accuracy),
		.speed_milli_m_s = speed_milli_m_s,
		.satellites_used = count_satellites_used(pvt),
		.last_error = 0,
	};

	bike_gnss_store_fix(&fix);
	LOG_INF("GNSS fix updated: lat=%d lon=%d speed=%d.%02d m/s",
		fix.latitude_microdegrees, fix.longitude_microdegrees,
		speed_milli_m_s / 1000, (speed_milli_m_s % 1000) / 10);
	LOG_INF("GNSS satellites used: %u, accuracy: %u mm", fix.satellites_used, fix.accuracy_mm);
}

static void process_pvt(const struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	uint8_t satellites_tracked;

	if (pvt->flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {
		store_valid_fix(pvt);
	} else {
		store_no_fix(0);
	}

	gnss_event_count++;
	satellites_tracked = count_satellites_tracked(pvt);
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

/*
 * Runs in interrupt context: only read modem data and signal the work.
 * Mutexes, logging, and cache updates all happen in event_work_handler.
 */
static void gnss_event_handler(int event)
{
	if (event == NRF_MODEM_GNSS_EVT_PVT) {
		if (nrf_modem_gnss_read(&isr_pvt, sizeof(isr_pvt),
					NRF_MODEM_GNSS_DATA_PVT) == 0) {
			(void)k_msgq_put(&pvt_msgq, &isr_pvt, K_NO_WAIT);
		} else {
			atomic_set_bit(&pending_events,
				       GNSS_EVT_READ_FAILED_BIT);
		}
	} else if (event > 0 && event < GNSS_EVT_READ_FAILED_BIT) {
		atomic_set_bit(&pending_events, event);
	}

	(void)k_work_submit(&event_work);
}

static void event_work_handler(struct k_work *work)
{
	struct nrf_modem_gnss_pvt_data_frame pvt;
	uint32_t events = (uint32_t)atomic_clear(&pending_events);

	ARG_UNUSED(work);

	while (k_msgq_get(&pvt_msgq, &pvt, K_NO_WAIT) == 0) {
		process_pvt(&pvt);
	}

	if (events & BIT(GNSS_EVT_READ_FAILED_BIT)) {
		store_no_fix(-EIO);
		LOG_WRN("GNSS PVT read failed");
	}
	if (events & BIT(NRF_MODEM_GNSS_EVT_FIX)) {
		LOG_INF("GNSS fix event");
		reset_search_period();
	}
	if (events & BIT(NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP)) {
		LOG_INF("GNSS woke up in periodic mode");
	}
	if (events & BIT(NRF_MODEM_GNSS_EVT_BLOCKED)) {
		LOG_INF("GNSS is blocked by LTE");
	}
	if (events & BIT(NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX)) {
		LOG_INF("GNSS sleeping after fix");
		reset_search_period();
	}
	if (events & BIT(NRF_MODEM_GNSS_EVT_SLEEP_AFTER_TIMEOUT)) {
		LOG_INF("GNSS sleeping after fix retry timeout");
		store_no_fix(0);
	}
}

static int64_t last_fix_age_ms(void)
{
	int64_t fix_uptime_ms;

	k_mutex_lock(&latest_lock, K_FOREVER);
	fix_uptime_ms = last_valid_fix.valid ? last_valid_fix.uptime_ms : 0;
	k_mutex_unlock(&latest_lock);

	return k_uptime_get() - fix_uptime_ms;
}

/*
 * "Soft duty cycle": whenever the retained fix goes stale, hand the radio
 * back to GNSS via priority mode. LTE stays registered and the MQTT
 * session stays up (downlink is just delayed). The modem drops priority
 * on its own after a fix or after 40 s, so keep re-arming it once per
 * window while the fix stays stale; while fixes are fresh, sleep until
 * the staleness deadline instead of polling.
 */
static void duty_work_handler(struct k_work *work)
{
	int64_t age_ms = last_fix_age_ms();
	int ret;

	ARG_UNUSED(work);

	if (age_ms < GNSS_FIX_MAX_AGE_MS) {
		prio_logged = false;
		k_work_schedule(&duty_work,
				K_MSEC(GNSS_FIX_MAX_AGE_MS - age_ms));
		return;
	}

	ret = nrf_modem_gnss_prio_mode_enable();
	if (ret != 0) {
		LOG_WRN("Failed to enable GNSS priority mode: %d", ret);
	} else if (!prio_logged) {
		prio_logged = true;
		LOG_INF("GNSS priority over LTE enabled (no fix for %lld s)",
			age_ms / 1000);
	}

	k_work_schedule(&duty_work, K_SECONDS(GNSS_PRIO_WINDOW_SECONDS));
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
		k_work_schedule(&duty_work, K_MSEC(GNSS_FIX_MAX_AGE_MS));
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
	last_valid_fix = (struct bike_gnss_fix){ 0 };
	bike_gnss_store_fix(&initial);
	k_work_init(&start_work, start_work_handler);
	k_work_init(&event_work, event_work_handler);
	k_work_init_delayable(&duty_work, duty_work_handler);
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
	last_valid_fix = (struct bike_gnss_fix){ 0 };
	bike_gnss_store_fix(&initial);
	LOG_INF("GNSS disabled on this target");
}

#endif
