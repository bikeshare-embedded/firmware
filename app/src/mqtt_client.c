/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "config.h"
#include "mqtt_client.h"

LOG_MODULE_REGISTER(bike_mqtt, LOG_LEVEL_INF);

static struct bike_mqtt_status current_status = {
#if defined(CONFIG_MQTT_LIB) && defined(CONFIG_NET_SOCKETS)
	.supported = true,
#else
	.supported = false,
#endif
};

K_MUTEX_DEFINE(status_lock);

static void status_copy(struct bike_mqtt_status *status)
{
	if (!status) {
		return;
	}

	k_mutex_lock(&status_lock, K_FOREVER);
	*status = current_status;
	k_mutex_unlock(&status_lock);
}

void bike_mqtt_status_get(struct bike_mqtt_status *status)
{
	status_copy(status);
}

static void status_set_error(int error)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.last_error = error;
	k_mutex_unlock(&status_lock);
}

#if defined(CONFIG_MQTT_LIB) && defined(CONFIG_NET_SOCKETS)

#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>

static uint8_t rx_buffer[CONFIG_BIKE_MQTT_RX_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_BIKE_MQTT_TX_BUFFER_SIZE];
static uint8_t payload_buffer[CONFIG_BIKE_MQTT_PAYLOAD_BUFFER_SIZE + 1];
static struct mqtt_client client_ctx;
static struct sockaddr_storage broker_addr;
static struct zsock_pollfd fds[1];
K_SEM_DEFINE(connect_sem, 0, 1);
static bool thread_running;
static bool stop_requested;
static char client_id[BIKE_ID_MAX_LEN];
static char command_topic[BIKE_MQTT_TOPIC_MAX_LEN];

static int build_command_topic(const char *bike_id, char *topic, size_t topic_len)
{
	int len;

	if (!bike_id || !bike_id[0]) {
		return -EINVAL;
	}

	len = snprintk(topic, topic_len, "bikes/%s/commands", bike_id);
	if (len < 0 || len >= topic_len) {
		return -ENAMETOOLONG;
	}

	return 0;
}

static int resolve_broker(const char *host, uint16_t port)
{
	struct zsock_addrinfo hints = {
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	struct zsock_addrinfo *result;
	char port_str[BIKE_MQTT_PORT_MAX_LEN];
	int rc;

	if (!host || !host[0] || !port) {
		return -EINVAL;
	}

	snprintk(port_str, sizeof(port_str), "%u", port);

	rc = zsock_getaddrinfo(host, port_str, &hints, &result);
	if (rc) {
		return -EIO;
	}

	memcpy(&broker_addr, result->ai_addr, result->ai_addrlen);
	zsock_freeaddrinfo(result);
	return 0;
}

static void prepare_fds(struct mqtt_client *client)
{
	fds[0].fd = client->transport.tcp.sock;
	fds[0].events = ZSOCK_POLLIN;
}

static int subscribe_commands(struct mqtt_client *client)
{
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = (uint8_t *)command_topic,
			.size = strlen(command_topic),
		},
		.qos = MQTT_QOS_0_AT_MOST_ONCE,
	};
	struct mqtt_subscription_list subscriptions = {
		.list = &topic,
		.list_count = 1,
		.message_id = sys_rand16_get(),
	};

	return mqtt_subscribe(client, &subscriptions);
}

static void read_publish_payload(struct mqtt_client *client,
				 const struct mqtt_publish_param *publish)
{
	size_t to_read = MIN(publish->message.payload.len,
			    CONFIG_BIKE_MQTT_PAYLOAD_BUFFER_SIZE);
	int rc;

	rc = mqtt_read_publish_payload_blocking(client, payload_buffer, to_read);
	if (rc < 0) {
		LOG_ERR("Failed to read MQTT payload: %d", rc);
		status_set_error(rc);
		return;
	}

	payload_buffer[rc] = '\0';

	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.rx_count++;
	k_mutex_unlock(&status_lock);

	LOG_INF("MQTT command topic: %.*s",
		publish->message.topic.topic.size,
		publish->message.topic.topic.utf8);
	LOG_INF("MQTT command payload: %s", payload_buffer);

	if (publish->message.payload.len > to_read) {
		size_t remaining = publish->message.payload.len - to_read;

		while (remaining > 0) {
			size_t chunk = MIN(remaining,
					   CONFIG_BIKE_MQTT_PAYLOAD_BUFFER_SIZE);

			rc = mqtt_read_publish_payload_blocking(client,
								payload_buffer,
								chunk);
			if (rc < 0) {
				LOG_ERR("Failed to drain MQTT payload: %d", rc);
				status_set_error(rc);
				return;
			}
			if (rc == 0) {
				status_set_error(-EIO);
				return;
			}
			remaining -= rc;
		}

		LOG_WRN("MQTT payload truncated to %u bytes",
			CONFIG_BIKE_MQTT_PAYLOAD_BUFFER_SIZE);
	}
}

static void mqtt_evt_handler(struct mqtt_client *client,
			     const struct mqtt_evt *evt)
{
	int rc;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result) {
			LOG_ERR("MQTT connect failed: %d", evt->result);
			k_mutex_lock(&status_lock, K_FOREVER);
			current_status.connecting = false;
			current_status.connected = false;
			current_status.last_error = evt->result;
			k_mutex_unlock(&status_lock);
			stop_requested = true;
			break;
		}

		k_mutex_lock(&status_lock, K_FOREVER);
		current_status.connecting = false;
		current_status.connected = true;
		current_status.last_error = 0;
		k_mutex_unlock(&status_lock);
		LOG_INF("MQTT connected");

		rc = subscribe_commands(client);
		if (rc) {
			LOG_ERR("Failed to subscribe to commands: %d", rc);
			status_set_error(rc);
		}
		break;
	case MQTT_EVT_SUBACK:
		if (evt->result) {
			LOG_ERR("MQTT subscribe failed: %d", evt->result);
			status_set_error(evt->result);
			break;
		}

		k_mutex_lock(&status_lock, K_FOREVER);
		current_status.subscribed = true;
		current_status.last_error = 0;
		k_mutex_unlock(&status_lock);
		LOG_INF("MQTT subscribed: %s", command_topic);
		break;
	case MQTT_EVT_PUBLISH:
		read_publish_payload(client, &evt->param.publish);
		break;
	case MQTT_EVT_DISCONNECT:
		k_mutex_lock(&status_lock, K_FOREVER);
		current_status.connecting = false;
		current_status.connected = false;
		current_status.subscribed = false;
		current_status.disconnect_reason = evt->result;
		current_status.last_error = evt->result;
		k_mutex_unlock(&status_lock);
		LOG_INF("MQTT disconnected: %d", evt->result);
		stop_requested = true;
		break;
	default:
		break;
	}
}

static void client_init(void)
{
	mqtt_client_init(&client_ctx);

	client_ctx.broker = &broker_addr;
	client_ctx.evt_cb = mqtt_evt_handler;
	client_ctx.client_id.utf8 = (uint8_t *)client_id;
	client_ctx.client_id.size = strlen(client_id);
	client_ctx.password = NULL;
	client_ctx.user_name = NULL;
	client_ctx.protocol_version = MQTT_VERSION_3_1_1;
	client_ctx.rx_buf = rx_buffer;
	client_ctx.rx_buf_size = sizeof(rx_buffer);
	client_ctx.tx_buf = tx_buffer;
	client_ctx.tx_buf_size = sizeof(tx_buffer);
	client_ctx.transport.type = MQTT_TRANSPORT_NON_SECURE;
}

static void mqtt_worker(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	for (;;) {
		k_sem_take(&connect_sem, K_FOREVER);

		while (thread_running && !stop_requested) {
			int timeout = mqtt_keepalive_time_left(&client_ctx);
			int rc;

			if (timeout < 0) {
				timeout = 1000;
			}

			rc = zsock_poll(fds, ARRAY_SIZE(fds), timeout);
			if (rc < 0) {
				rc = -errno;
				LOG_ERR("MQTT poll failed: %d", rc);
				status_set_error(rc);
				break;
			}

			if (rc > 0 && (fds[0].revents & ZSOCK_POLLIN)) {
				rc = mqtt_input(&client_ctx);
				if (rc) {
					LOG_ERR("MQTT input failed: %d", rc);
					status_set_error(rc);
					break;
				}
			}

			rc = mqtt_live(&client_ctx);
			if (rc == 0) {
				rc = mqtt_input(&client_ctx);
				if (rc) {
					LOG_ERR("MQTT ping response failed: %d", rc);
					status_set_error(rc);
					break;
				}
			} else if (rc != -EAGAIN) {
				LOG_ERR("MQTT keepalive failed: %d", rc);
				status_set_error(rc);
				break;
			}
		}

		if (thread_running) {
			(void)mqtt_abort(&client_ctx);
		}

		k_mutex_lock(&status_lock, K_FOREVER);
		current_status.connecting = false;
		current_status.connected = false;
		current_status.subscribed = false;
		k_mutex_unlock(&status_lock);

		thread_running = false;
		stop_requested = false;
	}
}

K_THREAD_DEFINE(mqtt_thread, CONFIG_BIKE_MQTT_THREAD_STACK_SIZE, mqtt_worker,
		NULL, NULL, NULL, CONFIG_BIKE_MQTT_THREAD_PRIORITY, 0, 0);

int bike_mqtt_init(void)
{
	if (current_status.initialized) {
		return 0;
	}

	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.initialized = true;
	current_status.last_error = 0;
	k_mutex_unlock(&status_lock);
	LOG_INF("MQTT client initialized");
	return 0;
}

int bike_mqtt_connect(void)
{
	const char *host = bike_config_get_mqtt_host();
	uint16_t port = bike_config_get_mqtt_port();
	const char *id = bike_config_get_id();
	int rc;

	if (!current_status.initialized) {
		rc = bike_mqtt_init();
		if (rc) {
			return rc;
		}
	}

	if (thread_running) {
		return 0;
	}

	rc = build_command_topic(id, command_topic, sizeof(command_topic));
	if (rc) {
		status_set_error(rc);
		return rc;
	}

	rc = resolve_broker(host, port);
	if (rc) {
		status_set_error(rc);
		return rc;
	}

	strncpy(client_id, id, sizeof(client_id) - 1);
	client_id[sizeof(client_id) - 1] = '\0';
	client_init();

	rc = mqtt_connect(&client_ctx);
	if (rc) {
		status_set_error(rc);
		return rc;
	}

	prepare_fds(&client_ctx);

	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.connecting = true;
	current_status.connected = false;
	current_status.subscribed = false;
	current_status.broker_port = port;
	current_status.last_error = 0;
	current_status.disconnect_reason = 0;
	strncpy(current_status.broker_host, host,
		sizeof(current_status.broker_host) - 1);
	current_status.broker_host[sizeof(current_status.broker_host) - 1] = '\0';
	strncpy(current_status.command_topic, command_topic,
		sizeof(current_status.command_topic) - 1);
	current_status.command_topic[sizeof(current_status.command_topic) - 1] = '\0';
	k_mutex_unlock(&status_lock);

	thread_running = true;
	stop_requested = false;
	k_sem_give(&connect_sem);
	LOG_INF("MQTT connect started: %s:%u", host, port);
	return 0;
}

int bike_mqtt_disconnect(void)
{
	int rc = 0;

	if (!thread_running) {
		return 0;
	}

	stop_requested = true;
	if (current_status.connected || current_status.connecting) {
		rc = mqtt_disconnect(&client_ctx);
		if (rc) {
			(void)mqtt_abort(&client_ctx);
		}
	}

	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.connecting = false;
	current_status.connected = false;
	current_status.subscribed = false;
	current_status.disconnect_reason = rc;
	current_status.last_error = rc;
	k_mutex_unlock(&status_lock);

	return rc;
}

#else

int bike_mqtt_init(void)
{
	current_status.last_error = -ENOTSUP;
	return -ENOTSUP;
}

int bike_mqtt_connect(void)
{
	current_status.last_error = -ENOTSUP;
	return -ENOTSUP;
}

int bike_mqtt_disconnect(void)
{
	current_status.last_error = -ENOTSUP;
	return -ENOTSUP;
}

#endif
