/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "channels.h"

ZBUS_CHAN_DEFINE(button_event_chan,
		 struct bike_button_event_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(backend_command_chan,
		 struct bike_backend_command_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(bike_state_chan,
		 struct bike_state_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = BIKE_STATE_UNREGISTERED));

ZBUS_CHAN_DEFINE(telemetry_sample_chan,
		 struct telemetry_sample_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
