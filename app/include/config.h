#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BIKE_ID_MAX_LEN             32
#define BIKE_DEVICE_TOKEN_MAX_LEN   128
#define BIKE_MQTT_HOST_MAX_LEN      128
#define BIKE_MQTT_PORT_MAX_LEN      6
#define BIKE_APN_MAX_LEN            64

struct bike_config {
	char id[BIKE_ID_MAX_LEN];
	char device_token[BIKE_DEVICE_TOKEN_MAX_LEN];
	char mqtt_host[BIKE_MQTT_HOST_MAX_LEN];
	uint16_t mqtt_port;
	char apn[BIKE_APN_MAX_LEN];
};

int bike_config_init(void);

const struct bike_config *bike_config_get(void);
const char *bike_config_get_id(void);
const char *bike_config_get_device_token(void);
const char *bike_config_get_mqtt_host(void);
uint16_t bike_config_get_mqtt_port(void);
const char *bike_config_get_apn(void);

bool bike_config_is_valid(const struct bike_config *config);
int bike_config_parse_mqtt_port(const char *value, uint16_t *port);

int bike_config_set_id(const char *id);
int bike_config_set_device_token(const char *token);
int bike_config_set_mqtt_host(const char *host);
int bike_config_set_mqtt_port(uint16_t port);
int bike_config_set_apn(const char *apn);
