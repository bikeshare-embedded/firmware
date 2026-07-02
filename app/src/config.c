/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#include "config.h"

LOG_MODULE_REGISTER(bike_config, LOG_LEVEL_INF);

static struct bike_config cfg;

static int read_string_setting(size_t len, settings_read_cb read_cb,
			       void *cb_arg, char *buf, size_t buf_len)
{

	if (len >= buf_len) {
		return -EINVAL;
	}

	int rc = read_cb(cb_arg, buf, len);

	if (rc < 0) {
		return rc;
	}
	if ((size_t)rc >= buf_len) {
		return -EINVAL;
	}

	buf[rc] = '\0';
	return 0;
}

static int settings_set_cb(const char *key, size_t len,
			   settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(key, "id", &next) && !next) {
		return read_string_setting(len, read_cb, cb_arg, cfg.id,
					   sizeof(cfg.id));
	}

	if (settings_name_steq(key, "device_token", &next) && !next) {
		return read_string_setting(len, read_cb, cb_arg, cfg.device_token,
					   sizeof(cfg.device_token));
	}

	if (settings_name_steq(key, "mqtt_host", &next) && !next) {
		return read_string_setting(len, read_cb, cb_arg, cfg.mqtt_host,
					   sizeof(cfg.mqtt_host));
	}

	if (settings_name_steq(key, "mqtt_port", &next) && !next) {
		char port_str[BIKE_MQTT_PORT_MAX_LEN];
		int rc = read_string_setting(len, read_cb, cb_arg, port_str,
					     sizeof(port_str));

		if (rc) {
			return rc;
		}

		uint16_t port;

		rc = bike_config_parse_mqtt_port(port_str, &port);
		if (rc) {
			return rc;
		}

		cfg.mqtt_port = port;
		return 0;
	}

	if (settings_name_steq(key, "apn", &next) && !next) {
		return read_string_setting(len, read_cb, cb_arg, cfg.apn,
					   sizeof(cfg.apn));
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(bike, "bike", NULL, settings_set_cb, NULL, NULL);

int bike_config_init(void)
{
	int rc = settings_subsys_init();

	if (rc) {
		LOG_ERR("Failed to initialize settings: %d", rc);
		return rc;
	}

	rc = settings_load();

	if (rc) {
		LOG_ERR("Failed to load settings: %d", rc);
	}

	return rc;
}

const struct bike_config *bike_config_get(void)
{
	return &cfg;
}

const char *bike_config_get_id(void)
{
	return cfg.id;
}

const char *bike_config_get_device_token(void)
{
	return cfg.device_token;
}

const char *bike_config_get_mqtt_host(void)
{
	return cfg.mqtt_host;
}

uint16_t bike_config_get_mqtt_port(void)
{
	return cfg.mqtt_port;
}

const char *bike_config_get_apn(void)
{
	return cfg.apn;
}

static int validate_required_string(const char *value, size_t buf_len)
{
	size_t len;

	if (!value) {
		return -EINVAL;
	}

	len = strlen(value);
	if (len == 0 || len >= buf_len) {
		return -EINVAL;
	}

	return 0;
}

int bike_config_parse_mqtt_port(const char *value, uint16_t *port)
{
	char *endptr;
	long parsed;

	if (!value || !value[0] || !port) {
		return -EINVAL;
	}

	parsed = strtol(value, &endptr, 10);
	if (*endptr != '\0' || parsed < 1 || parsed > UINT16_MAX) {
		return -EINVAL;
	}

	*port = (uint16_t)parsed;
	return 0;
}

bool bike_config_is_valid(const struct bike_config *config)
{
	return config &&
	       config->id[0] &&
	       config->device_token[0] &&
	       config->mqtt_host[0] &&
	       config->mqtt_port >= 1 &&
	       config->apn[0];
}

static int save_string_field(const char *settings_key, const char *value,
			     char *buf, size_t buf_len)
{
	size_t len;
	int rc;

	rc = validate_required_string(value, buf_len);
	if (rc) {
		return rc;
	}

	len = strlen(value);
	memcpy(buf, value, len + 1);

#if defined(CONFIG_SETTINGS_RUNTIME)
	return settings_runtime_set(settings_key, buf, len);
#else
	return settings_save_one(settings_key, buf, len);
#endif
}

int bike_config_set_id(const char *id)
{
	return save_string_field("bike/id", id, cfg.id, sizeof(cfg.id));
}

int bike_config_set_device_token(const char *token)
{
	return save_string_field("bike/device_token", token, cfg.device_token,
				 sizeof(cfg.device_token));
}

int bike_config_set_mqtt_host(const char *host)
{
	return save_string_field("bike/mqtt_host", host, cfg.mqtt_host,
				 sizeof(cfg.mqtt_host));
}

int bike_config_set_mqtt_port(uint16_t port)
{
	char port_str[BIKE_MQTT_PORT_MAX_LEN];
	int len;

	if (port < 1) {
		return -EINVAL;
	}

	cfg.mqtt_port = port;
	len = snprintk(port_str, sizeof(port_str), "%u", port);
	if (len <= 0 || len >= sizeof(port_str)) {
		return -EINVAL;
	}

#if defined(CONFIG_SETTINGS_RUNTIME)
	return settings_runtime_set("bike/mqtt_port", port_str, len);
#else
	return settings_save_one("bike/mqtt_port", port_str, len);
#endif
}

int bike_config_set_apn(const char *apn)
{
	return save_string_field("bike/apn", apn, cfg.apn, sizeof(cfg.apn));
}
