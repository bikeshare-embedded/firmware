/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "telemetry.h"
#include "config.h"
#include "state.h"

LOG_MODULE_REGISTER(bike_telemetry, LOG_LEVEL_INF);

#ifndef CONFIG_BIKE_TELEMETRY_PERIOD_SECONDS
#define CONFIG_BIKE_TELEMETRY_PERIOD_SECONDS 10
#endif

static struct k_work_delayable telemetry_work;

void bike_telemetry_fill_sample(struct telemetry_sample_msg *sample,
				const char *bike_id,
				enum bike_state_value state,
				int64_t uptime_ms,
				const struct bike_gnss_fix *gnss)
{
	if (sample == NULL) {
		return;
	}

	memset(sample, 0, sizeof(*sample));

	if (bike_id != NULL) {
		strncpy(sample->bike_id, bike_id, sizeof(sample->bike_id) - 1);
		sample->bike_id[sizeof(sample->bike_id) - 1] = '\0';
	}

	sample->state = state;
	sample->uptime_ms = uptime_ms;

	if (gnss == NULL || !gnss->valid) {
		sample->gnss_fix_valid = false;
		return;
	}

	sample->gnss_fix_valid = true;
	sample->gnss_latitude_microdegrees = gnss->latitude_microdegrees;
	sample->gnss_longitude_microdegrees = gnss->longitude_microdegrees;
	sample->gnss_altitude_mm = gnss->altitude_mm;
	sample->gnss_accuracy_mm = gnss->accuracy_mm;
}

static void telemetry_work_handler(struct k_work *work)
{
	struct telemetry_sample_msg sample;
	struct bike_gnss_fix gnss;
	int ret;

	ARG_UNUSED(work);

	(void)bike_gnss_get_latest(&gnss);
	bike_telemetry_fill_sample(&sample, bike_config_get_id(), bike_state_get(),
				   k_uptime_get(), &gnss);

	ret = zbus_chan_pub(&telemetry_sample_chan, &sample, K_NO_WAIT);
	if (ret != 0) {
		LOG_WRN("Telemetry publish failed: %d", ret);
	} else if (sample.gnss_fix_valid) {
		LOG_INF("Telemetry published with GNSS fix");
	} else {
		LOG_INF("Telemetry published with no GNSS fix");
	}

	k_work_schedule(&telemetry_work,
			K_SECONDS(CONFIG_BIKE_TELEMETRY_PERIOD_SECONDS));
}

void bike_telemetry_init(void)
{
	k_work_init_delayable(&telemetry_work, telemetry_work_handler);
	k_work_schedule(&telemetry_work,
			K_SECONDS(CONFIG_BIKE_TELEMETRY_PERIOD_SECONDS));
	LOG_INF("Telemetry publisher initialized");
}
