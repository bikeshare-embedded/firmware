/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Persistent bike configuration.
 *
 * Stores the bike identity, backend credentials and MQTT/APN settings
 * in the Zephyr settings subsystem, under the "bike" key.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Maximum length, including the terminator, of the bike ID string. */
#define BIKE_ID_MAX_LEN             32
/** Maximum length, including the terminator, of the device token string. */
#define BIKE_DEVICE_TOKEN_MAX_LEN   128
/** Maximum length, including the terminator, of the MQTT host string. */
#define BIKE_MQTT_HOST_MAX_LEN      128
/** Maximum length, including the terminator, of the MQTT port string form. */
#define BIKE_MQTT_PORT_MAX_LEN      6
/** Maximum length, including the terminator, of the APN string. */
#define BIKE_APN_MAX_LEN            64

/** Persistent bike configuration. */
struct bike_config {
	/** Bike identifier. */
	char id[BIKE_ID_MAX_LEN];
	/** Authentication token used with the backend. */
	char device_token[BIKE_DEVICE_TOKEN_MAX_LEN];
	/** Hostname or IP address of the MQTT broker. */
	char mqtt_host[BIKE_MQTT_HOST_MAX_LEN];
	/** TCP port of the MQTT broker. */
	uint16_t mqtt_port;
	/** APN used for the cellular connection. */
	char apn[BIKE_APN_MAX_LEN];
};

/**
 * @brief Initialize the configuration module.
 *
 * Initializes the settings subsystem and loads any previously saved
 * configuration values.
 *
 * @return 0 on success, negative errno code on failure.
 */
int bike_config_init(void);

/**
 * @brief Get a read-only pointer to the current configuration.
 *
 * @return Pointer to the current configuration.
 */
const struct bike_config *bike_config_get(void);

/**
 * @brief Get the configured bike ID.
 *
 * @return Pointer to the bike ID string, empty if unset.
 */
const char *bike_config_get_id(void);

/**
 * @brief Get the configured device token.
 *
 * @return Pointer to the device token string, empty if unset.
 */
const char *bike_config_get_device_token(void);

/**
 * @brief Get the configured MQTT broker host.
 *
 * @return Pointer to the MQTT host string, empty if unset.
 */
const char *bike_config_get_mqtt_host(void);

/**
 * @brief Get the configured MQTT broker port.
 *
 * @return MQTT port, 0 if unset.
 */
uint16_t bike_config_get_mqtt_port(void);

/**
 * @brief Get the configured APN.
 *
 * @return Pointer to the APN string, empty if unset.
 */
const char *bike_config_get_apn(void);

/**
 * @brief Check whether a configuration has every required field set.
 *
 * @param config Configuration to validate.
 *
 * @return true if @p config is non-NULL and fully populated, false
 *         otherwise.
 */
bool bike_config_is_valid(const struct bike_config *config);

/**
 * @brief Parse and validate an MQTT port string.
 *
 * @param value String to parse, must contain only decimal digits.
 * @param port  Destination for the parsed port. Must not be NULL.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p value is missing or not a valid port number.
 */
int bike_config_parse_mqtt_port(const char *value, uint16_t *port);

/**
 * @brief Set and persist the bike ID.
 *
 * @param id New bike ID. Must be non-empty and fit within
 *           @ref BIKE_ID_MAX_LEN.
 *
 * @return 0 on success, negative errno code on failure.
 */
int bike_config_set_id(const char *id);

/**
 * @brief Set and persist the device token.
 *
 * @param token New device token. Must be non-empty and fit within
 *              @ref BIKE_DEVICE_TOKEN_MAX_LEN.
 *
 * @return 0 on success, negative errno code on failure.
 */
int bike_config_set_device_token(const char *token);

/**
 * @brief Set and persist the MQTT broker host.
 *
 * @param host New MQTT host. Must be non-empty and fit within
 *             @ref BIKE_MQTT_HOST_MAX_LEN.
 *
 * @return 0 on success, negative errno code on failure.
 */
int bike_config_set_mqtt_host(const char *host);

/**
 * @brief Set and persist the MQTT broker port.
 *
 * @param port New MQTT port, must be non-zero.
 *
 * @return 0 on success, negative errno code on failure.
 */
int bike_config_set_mqtt_port(uint16_t port);

/**
 * @brief Set and persist the APN.
 *
 * @param apn New APN. Must be non-empty and fit within
 *            @ref BIKE_APN_MAX_LEN.
 *
 * @return 0 on success, negative errno code on failure.
 */
int bike_config_set_apn(const char *apn);
