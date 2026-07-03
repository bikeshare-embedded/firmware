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
#include <stddef.h>

#include "channels.h"
#include "config.h"

#define BIKE_MQTT_TOPIC_MAX_LEN 96

struct bike_mqtt_status {
	bool supported;
	bool initialized;
	bool enabled;
	bool connecting;
	bool connected;
	bool subscribed;
	bool secure;
	char broker_host[BIKE_MQTT_HOST_MAX_LEN];
	uint16_t broker_port;
	char command_topic[BIKE_MQTT_TOPIC_MAX_LEN];
	char telemetry_topic[BIKE_MQTT_TOPIC_MAX_LEN];
	char event_topic[BIKE_MQTT_TOPIC_MAX_LEN];
	int last_error;
	int disconnect_reason;
	uint32_t rx_count;
	uint32_t parse_error_count;
	uint32_t publish_count;
	uint32_t publish_error_count;
	uint32_t reconnect_count;
};

/**
 * @brief Initialize MQTT diagnostics state.
 *
 * This does not open a socket or start an automatic broker connection.
 *
 * @retval 0 on success.
 * @retval -ENOTSUP if MQTT socket support is not built in.
 */
int bike_mqtt_init(void);

/**
 * @brief Enable MQTT and request a broker connection.
 *
 * The worker connects only after the bike configuration is valid and LTE is
 * ready, when LTE support is present. A later broker disconnect is retried
 * with bounded backoff while MQTT remains enabled.
 *
 * @retval 0 on success or if a connection attempt is already active.
 * @retval -EINVAL if the bike configuration is incomplete.
 * @retval -ENOTSUP if MQTT socket support is not built in.
 */
int bike_mqtt_connect(void);

/**
 * @brief Disable MQTT and disconnect the active session.
 *
 * This is the manual-debug counterpart to @ref bike_mqtt_connect. It stops
 * reconnect attempts until MQTT is enabled again.
 *
 * @retval 0 on success or when no session is active.
 * @retval -ENOTSUP if MQTT socket support is not built in.
 * @return Negative errno from the MQTT stack on disconnect failure.
 */
int bike_mqtt_disconnect(void);

/**
 * @brief Probe plain TCP reachability to the configured MQTT broker.
 *
 * This intentionally skips TLS and MQTT. A successful probe means the modem
 * can open a TCP connection to the configured host and port.
 *
 * @retval 0 on successful TCP connect.
 * @retval -EINVAL if the broker host or port is not configured.
 * @retval -ENETUNREACH if LTE is not ready for sockets.
 * @retval -ENOTSUP if MQTT socket support is not built in.
 * @return Negative errno from DNS, socket creation, or TCP connect failure.
 */
int bike_mqtt_tcp_probe(void);

/**
 * @brief Copy the current MQTT status snapshot.
 *
 * @param status Destination for the copied status. NULL is ignored.
 */
void bike_mqtt_status_get(struct bike_mqtt_status *status);

/**
 * @brief Convert a stored MQTT error or errno value into a shell label.
 *
 * @param error Value from @ref bike_mqtt_status::last_error or
 *              @ref bike_mqtt_status::disconnect_reason.
 *
 * @return Static string naming the reason.
 */
const char *bike_mqtt_error_name(int error);

/**
 * @brief Build the backend command subscription topic for a bike.
 *
 * @param bike_id Configured bike identifier.
 * @param topic Destination buffer.
 * @param topic_len Size of @p topic in bytes.
 *
 * @retval 0 on success.
 * @retval -EINVAL if an argument is missing.
 * @retval -ENAMETOOLONG if @p topic is too small.
 */
int bike_mqtt_build_command_topic(const char *bike_id, char *topic,
				  size_t topic_len);

/**
 * @brief Build the telemetry publication topic for a bike.
 *
 * @param bike_id Configured bike identifier.
 * @param topic Destination buffer.
 * @param topic_len Size of @p topic in bytes.
 *
 * @retval 0 on success.
 * @retval -EINVAL if an argument is missing.
 * @retval -ENAMETOOLONG if @p topic is too small.
 */
int bike_mqtt_build_telemetry_topic(const char *bike_id, char *topic,
				    size_t topic_len);

/**
 * @brief Build the event publication topic for a bike.
 *
 * @param bike_id Configured bike identifier.
 * @param topic Destination buffer.
 * @param topic_len Size of @p topic in bytes.
 *
 * @retval 0 on success.
 * @retval -EINVAL if an argument is missing.
 * @retval -ENAMETOOLONG if @p topic is too small.
 */
int bike_mqtt_build_event_topic(const char *bike_id, char *topic,
				size_t topic_len);

/**
 * @brief Decode a backend command JSON payload.
 *
 * Supported payloads are compact JSON objects with `protocolVersion` set to
 * 1, `type` set to `rent_authorize` or `rent_cancel`, and a non-empty
 * `rental_id`.
 *
 * @param payload Mutable JSON buffer. Zephyr's JSON parser stores pointers
 *                into this buffer.
 * @param payload_len Number of bytes in @p payload.
 * @param msg Destination backend command message.
 *
 * @retval 0 on success.
 * @retval -EINVAL if required fields are missing.
 * @retval -ENAMETOOLONG if `rental_id` does not fit the zbus message.
 * @retval -ENOTSUP if the command type is unknown.
 * @return Negative parser error from the Zephyr JSON parser.
 */
int bike_mqtt_parse_command(char *payload, size_t payload_len,
			    struct bike_backend_command_msg *msg);

/**
 * @brief Parse a command payload and publish it to backend_command_chan.
 *
 * Invalid payloads update MQTT parse diagnostics and are not forwarded to the
 * state machine.
 *
 * @param payload Mutable JSON buffer.
 * @param payload_len Number of bytes in @p payload.
 *
 * @retval 0 on success.
 * @return Negative error from parsing or zbus publication.
 */
int bike_mqtt_handle_command_payload(char *payload, size_t payload_len);

/**
 * @brief Format a compact JSON telemetry publication.
 *
 * @param sample Telemetry sample to encode.
 * @param payload Destination buffer.
 * @param payload_len Size of @p payload in bytes.
 *
 * @return Number of bytes written, excluding the terminator.
 * @retval -EINVAL if an argument is missing.
 * @retval -ENAMETOOLONG if @p payload is too small.
 */
int bike_mqtt_format_telemetry_json(const struct telemetry_sample_msg *sample,
				    char *payload, size_t payload_len);

/**
 * @brief Format a compact JSON state event publication.
 *
 * @param msg State message to encode.
 * @param payload Destination buffer.
 * @param payload_len Size of @p payload in bytes.
 *
 * @return Number of bytes written, excluding the terminator.
 * @retval -EINVAL if an argument is missing.
 * @retval -ENAMETOOLONG if @p payload is too small.
 */
int bike_mqtt_format_state_event_json(const struct bike_state_msg *msg,
				      char *payload, size_t payload_len);

/**
 * @brief Format a compact JSON command rejection event.
 *
 * @param command Rejected command type string.
 * @param reason Protocol reason string.
 * @param payload Destination buffer.
 * @param payload_len Size of @p payload in bytes.
 *
 * @return Number of bytes written, excluding the terminator.
 * @retval -EINVAL if an argument is missing.
 * @retval -ENAMETOOLONG if @p payload is too small.
 */
int bike_mqtt_format_command_rejected_json(const char *command,
					   const char *reason,
					   char *payload,
					   size_t payload_len);
