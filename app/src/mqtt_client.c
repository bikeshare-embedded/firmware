/*
 * Copyright (c) 2026 Bikeshare Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "channels.h"
#include "config.h"
#include "lte.h"
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

enum mqtt_status_error {
	MQTT_STATUS_ERR_DNS = -1000,
	MQTT_STATUS_ERR_CONNACK = -1001,
	MQTT_STATUS_ERR_SUBACK = -1002,
	MQTT_STATUS_ERR_PARSE = -1003,
	MQTT_STATUS_ERR_PUBLISH = -1004,
	MQTT_STATUS_ERR_CONNECT = -1005,
};

#if defined(CONFIG_MQTT_LIB) && defined(CONFIG_NET_SOCKETS)
static char event_topic[BIKE_MQTT_TOPIC_MAX_LEN];
static int publish_payload(const char *topic, const char *payload);
#endif

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

const char *bike_mqtt_error_name(int error)
{
	switch (error) {
	case 0:
		return "none";
	case MQTT_STATUS_ERR_DNS:
		return "dns_failure";
	case MQTT_STATUS_ERR_CONNECT:
		return "connect_failure";
	case MQTT_STATUS_ERR_CONNACK:
		return "connack_refused";
	case MQTT_STATUS_ERR_SUBACK:
		return "suback_failure";
	case MQTT_STATUS_ERR_PARSE:
		return "parse_failure";
	case MQTT_STATUS_ERR_PUBLISH:
		return "publish_failure";
	case -EINVAL:
		return "invalid_argument";
	case -ENOTCONN:
		return "not_connected";
	case -ENOTSUP:
		return "not_supported";
	case -EIO:
		return "io_failure";
	case -ECONNREFUSED:
		return "connection_refused";
	case -ECONNRESET:
		return "connection_reset";
	case -ETIMEDOUT:
		return "timed_out";
	case -EHOSTUNREACH:
		return "host_unreachable";
	case -ENETUNREACH:
		return "network_unreachable";
	default:
		return "errno";
	}
}

static void status_set_error(int error)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.last_error = error;
	k_mutex_unlock(&status_lock);
}

static void status_count_parse_error(int error)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.parse_error_count++;
	current_status.last_error = error;
	k_mutex_unlock(&status_lock);
}

int bike_mqtt_build_command_topic(const char *bike_id, char *topic,
				  size_t topic_len)
{
	int len;

	if (!bike_id || !bike_id[0] || !topic || topic_len == 0) {
		return -EINVAL;
	}

	len = snprintk(topic, topic_len, "bikes/%s/commands", bike_id);
	if (len < 0 || len >= topic_len) {
		return -ENAMETOOLONG;
	}

	return 0;
}

int bike_mqtt_build_telemetry_topic(const char *bike_id, char *topic,
				    size_t topic_len)
{
	int len;

	if (!bike_id || !bike_id[0] || !topic || topic_len == 0) {
		return -EINVAL;
	}

	len = snprintk(topic, topic_len, "bikes/%s/telemetry", bike_id);
	if (len < 0 || len >= topic_len) {
		return -ENAMETOOLONG;
	}

	return 0;
}

int bike_mqtt_build_event_topic(const char *bike_id, char *topic,
				size_t topic_len)
{
	int len;

	if (!bike_id || !bike_id[0] || !topic || topic_len == 0) {
		return -EINVAL;
	}

	len = snprintk(topic, topic_len, "bikes/%s/events", bike_id);
	if (len < 0 || len >= topic_len) {
		return -ENAMETOOLONG;
	}

	return 0;
}

int bike_mqtt_parse_command(char *payload, size_t payload_len,
			    struct bike_backend_command_msg *msg)
{
	struct command_payload {
		int protocol_version;
		const char *type;
		const char *rental_id;
	} decoded = { 0 };
	static const struct json_obj_descr command_descr[] = {
		JSON_OBJ_DESCR_PRIM_NAMED(struct command_payload,
					  "protocolVersion",
					  protocol_version, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM(struct command_payload, type,
				    JSON_TOK_STRING),
		JSON_OBJ_DESCR_PRIM(struct command_payload, rental_id,
				    JSON_TOK_STRING),
	};
	int ret;

	if (!payload || payload_len == 0 || !msg) {
		return -EINVAL;
	}

	/*
	 * json_obj_parse() returns string pointers into the mutable payload
	 * buffer. Copy only validated fields into the zbus command message.
	 */
	memset(msg, 0, sizeof(*msg));
	ret = json_obj_parse(payload, payload_len, command_descr,
			     ARRAY_SIZE(command_descr), &decoded);
	if (ret < 0) {
		return ret;
	}

	if (decoded.protocol_version != 1) {
		return -EPROTONOSUPPORT;
	}

	if (!decoded.type || !decoded.rental_id || !decoded.rental_id[0]) {
		return -EINVAL;
	}

	if (strlen(decoded.rental_id) >= sizeof(msg->rental_id)) {
		return -ENAMETOOLONG;
	}

	if (strcmp(decoded.type, "rent_authorize") == 0) {
		msg->type = BIKE_BACKEND_RENT_AUTHORIZE;
	} else if (strcmp(decoded.type, "rent_cancel") == 0) {
		msg->type = BIKE_BACKEND_RENT_CANCEL;
	} else {
		return -ENOTSUP;
	}

	strcpy(msg->rental_id, decoded.rental_id);
	return 0;
}

static const char *backend_command_name(enum bike_backend_command_type type)
{
	switch (type) {
	case BIKE_BACKEND_RENT_AUTHORIZE:
		return "rent_authorize";
	case BIKE_BACKEND_RENT_CANCEL:
		return "rent_cancel";
	default:
		return "unknown";
	}
}

static const char *command_reject_reason(const struct bike_backend_command_msg *msg)
{
	const char *active_rental_id = bike_state_get_rental_id();
	enum bike_state_value state = bike_state_get();

	if (msg->type == BIKE_BACKEND_RENT_AUTHORIZE) {
		return state == BIKE_STATE_AVAILABLE ? NULL : "not_available";
	}

	if (msg->type == BIKE_BACKEND_RENT_CANCEL) {
		if (state != BIKE_STATE_RESERVED) {
			return "no_active_reservation";
		}
		if (msg->rental_id[0] &&
		    strcmp(msg->rental_id, active_rental_id) != 0) {
			return "ride_id_mismatch";
		}
	}

	return NULL;
}

int bike_mqtt_handle_command_payload(char *payload, size_t payload_len)
{
	struct bike_backend_command_msg msg;
	const char *reason;
	int rc;

	rc = bike_mqtt_parse_command(payload, payload_len, &msg);
	if (rc) {
		LOG_WRN("Rejected MQTT command payload: %d", rc);
		status_count_parse_error(MQTT_STATUS_ERR_PARSE);
		return rc;
	}

	reason = command_reject_reason(&msg);
	if (reason != NULL) {
		char rejected_payload[192];

		LOG_WRN("Rejected MQTT command %s: %s",
			backend_command_name(msg.type), reason);
		status_count_parse_error(MQTT_STATUS_ERR_PARSE);
		rc = bike_mqtt_format_command_rejected_json(
			backend_command_name(msg.type), reason,
			rejected_payload, sizeof(rejected_payload));
		if (rc > 0) {
#if defined(CONFIG_MQTT_LIB) && defined(CONFIG_NET_SOCKETS)
			(void)publish_payload(event_topic, rejected_payload);
#endif
		}
		return -EACCES;
	}

	/* Keep all rental behavior inside the state module's zbus listener. */
	rc = zbus_chan_pub(&backend_command_chan, &msg, K_MSEC(100));
	if (rc) {
		LOG_ERR("Failed to publish backend command: %d", rc);
		status_set_error(rc);
		return rc;
	}

	return 0;
}

static void format_scaled_decimal(char *buf, size_t buf_len, int32_t value,
				  int32_t scale, int precision)
{
	int64_t magnitude = value;
	const char *sign = "";

	if (magnitude < 0) {
		sign = "-";
		magnitude = -magnitude;
	}

	snprintk(buf, buf_len, "%s%lld.%0*lld", sign, magnitude / scale,
		 precision, magnitude % scale);
}

static void format_milli_decimal(char *buf, size_t buf_len, int32_t value)
{
	format_scaled_decimal(buf, buf_len, value, 1000, 3);
}

static void format_micro_decimal(char *buf, size_t buf_len, int32_t value)
{
	format_scaled_decimal(buf, buf_len, value, 1000000, 6);
}

static const char *nullable_ride_id(const char *rental_id,
				    char *buf, size_t buf_len)
{
	if (rental_id == NULL || rental_id[0] == '\0') {
		return "null";
	}

	snprintk(buf, buf_len, "\"%s\"", rental_id);
	return buf;
}

int bike_mqtt_format_telemetry_json(const struct telemetry_sample_msg *sample,
				    char *payload, size_t payload_len)
{
	char ride_id[BIKE_RENTAL_ID_MAX_LEN + 2];
	char speed[18];
	char latitude[20];
	char longitude[20];
	char altitude[18];
	char accuracy[18];
	char ax[18];
	char ay[18];
	char az[18];
	char gx[18];
	char gy[18];
	char gz[18];
	char temp[18];
	const char *speed_json = "null";
	const char *gnss_json = "\"gnss\":{\"valid\":false}";
	const char *motion_json = "\"motion\":{\"valid\":false}";
	char gnss_buf[160];
	char motion_buf[320];
	int len;

	if (!sample || !payload || payload_len == 0) {
		return -EINVAL;
	}

	if (sample->speed_valid) {
		format_milli_decimal(speed, sizeof(speed), sample->speed_milli_m_s);
		speed_json = speed;
	}

	if (sample->gnss_fix_valid) {
		format_micro_decimal(latitude, sizeof(latitude),
				     sample->gnss_latitude_microdegrees);
		format_micro_decimal(longitude, sizeof(longitude),
				     sample->gnss_longitude_microdegrees);
		format_milli_decimal(altitude, sizeof(altitude),
				     sample->gnss_altitude_mm);
		format_milli_decimal(accuracy, sizeof(accuracy),
				     (int32_t)sample->gnss_accuracy_mm);
		len = snprintk(gnss_buf, sizeof(gnss_buf),
			       "\"gnss\":{\"valid\":true,\"latitude\":%s,"
			       "\"longitude\":%s,\"altitudeMeters\":%s,"
			       "\"accuracyMeters\":%s}",
			       latitude, longitude, altitude, accuracy);
		if (len < 0 || len >= sizeof(gnss_buf)) {
			return -ENAMETOOLONG;
		}
		gnss_json = gnss_buf;
	}

	if (sample->motion_valid) {
		format_milli_decimal(ax, sizeof(ax), sample->motion_accel_milli_ms2[0]);
		format_milli_decimal(ay, sizeof(ay), sample->motion_accel_milli_ms2[1]);
		format_milli_decimal(az, sizeof(az), sample->motion_accel_milli_ms2[2]);
		format_milli_decimal(gx, sizeof(gx), sample->motion_gyro_milli_rad_s[0]);
		format_milli_decimal(gy, sizeof(gy), sample->motion_gyro_milli_rad_s[1]);
		format_milli_decimal(gz, sizeof(gz), sample->motion_gyro_milli_rad_s[2]);
		format_milli_decimal(temp, sizeof(temp), sample->motion_temp_milli_c);
		len = snprintk(motion_buf, sizeof(motion_buf),
			       "\"motion\":{\"valid\":true,\"moving\":%s,"
			       "\"accelMetersPerSecondSquared\":{\"x\":%s,"
			       "\"y\":%s,\"z\":%s},"
			       "\"gyroRadiansPerSecond\":{\"x\":%s,\"y\":%s,"
			       "\"z\":%s},\"temperatureCelsius\":%s}",
			       sample->motion_moving ? "true" : "false",
			       ax, ay, az, gx, gy, gz, temp);
		if (len < 0 || len >= sizeof(motion_buf)) {
			return -ENAMETOOLONG;
		}
		motion_json = motion_buf;
	}

	len = snprintk(payload, payload_len,
		       "{\"protocolVersion\":1,\"bikeId\":\"%s\","
		       "\"status\":\"%s\",\"uptimeMs\":%lld,\"rideId\":%s,"
		       "\"speedMetersPerSecond\":%s,%s,%s}",
		       sample->bike_id, bike_state_name(sample->state),
		       sample->uptime_ms,
		       nullable_ride_id(sample->rental_id, ride_id,
					sizeof(ride_id)),
		       speed_json, gnss_json, motion_json);
	if (len < 0 || len >= payload_len) {
		return -ENAMETOOLONG;
	}

	return len;
}

static const char *state_event_name(enum bike_state_event event)
{
	switch (event) {
	case BIKE_STATE_EVENT_BICYCLE_ONLINE:
		return "bicycle_online";
	case BIKE_STATE_EVENT_RESERVATION_EXPIRED:
		return "reservation_expired";
	case BIKE_STATE_EVENT_RIDE_STARTED:
		return "ride_started";
	case BIKE_STATE_EVENT_RIDE_ENDED:
		return "ride_ended";
	default:
		return NULL;
	}
}

int bike_mqtt_format_state_event_json(const struct bike_state_msg *msg,
				      char *payload, size_t payload_len)
{
	char ride_id[BIKE_RENTAL_ID_MAX_LEN + 2];
	const char *event_name;
	int len;

	if (!msg || !payload || payload_len == 0) {
		return -EINVAL;
	}

	event_name = state_event_name(msg->event);
	if (event_name == NULL) {
		return -EINVAL;
	}

	len = snprintk(payload, payload_len,
		       "{\"protocolVersion\":1,\"bikeId\":\"%s\","
		       "\"event\":\"%s\",\"status\":\"%s\",\"rideId\":%s,"
		       "\"uptimeMs\":%lld}",
		       bike_config_get_id(), event_name, bike_state_name(msg->state),
		       nullable_ride_id(msg->rental_id, ride_id, sizeof(ride_id)),
		       msg->updated_at_ms);
	if (len < 0 || len >= payload_len) {
		return -ENAMETOOLONG;
	}

	return len;
}

int bike_mqtt_format_command_rejected_json(const char *command,
					   const char *reason,
					   char *payload,
					   size_t payload_len)
{
	char ride_id[BIKE_RENTAL_ID_MAX_LEN + 2];
	int len;

	if (!command || !reason || !payload || payload_len == 0) {
		return -EINVAL;
	}

	len = snprintk(payload, payload_len,
		       "{\"protocolVersion\":1,\"bikeId\":\"%s\","
		       "\"event\":\"command_rejected\",\"status\":\"%s\","
		       "\"rideId\":%s,\"command\":\"%s\",\"reason\":\"%s\"}",
		       bike_config_get_id(), bike_state_name(bike_state_get()),
		       nullable_ride_id(bike_state_get_rental_id(), ride_id,
					sizeof(ride_id)),
		       command, reason);
	if (len < 0 || len >= payload_len) {
		return -ENAMETOOLONG;
	}

	return len;
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
static atomic_t thread_running;
static atomic_t stop_requested;
static atomic_t session_ended;
static int session_end_reason;
static atomic_t mqtt_enabled;
static char client_id[BIKE_ID_MAX_LEN];
static struct mqtt_utf8 username;
static struct mqtt_utf8 password;
static char command_topic[BIKE_MQTT_TOPIC_MAX_LEN];
static char telemetry_topic[BIKE_MQTT_TOPIC_MAX_LEN];
static sec_tag_t tls_sec_tags[] = { CONFIG_BIKE_MQTT_TLS_SEC_TAG };

static void status_count_publish_error(int error)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.publish_error_count++;
	current_status.last_error = error;
	k_mutex_unlock(&status_lock);
}

static bool status_publish_ready(void)
{
	bool ready;

	k_mutex_lock(&status_lock, K_FOREVER);
	ready = current_status.connected && current_status.subscribed;
	k_mutex_unlock(&status_lock);

	return ready;
}

static bool status_session_active(void)
{
	bool active;

	k_mutex_lock(&status_lock, K_FOREVER);
	active = current_status.connected || current_status.connecting;
	k_mutex_unlock(&status_lock);

	return active;
}

static int resolve_broker(const char *host, uint16_t port,
			  struct sockaddr_storage *addr)
{
	struct zsock_addrinfo hints = {
#if defined(CONFIG_NRF_MODEM_LIB) && defined(CONFIG_NET_SOCKETS_OFFLOAD)
		.ai_family = AF_INET,
#endif
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	struct zsock_addrinfo *result;
	char port_str[BIKE_MQTT_PORT_MAX_LEN];
	int rc;

	if (!host || !host[0] || !port || !addr) {
		return -EINVAL;
	}

	snprintk(port_str, sizeof(port_str), "%u", port);

	rc = zsock_getaddrinfo(host, port_str, &hints, &result);
	if (rc) {
		LOG_ERR("Failed to resolve MQTT broker %s:%u: %d", host, port,
			rc);
		return MQTT_STATUS_ERR_DNS;
	}

	memset(addr, 0, sizeof(*addr));
	memcpy(addr, result->ai_addr, result->ai_addrlen);
	LOG_INF("MQTT broker resolved: %s:%u family=%d", host, port,
		result->ai_addr->sa_family);
	zsock_freeaddrinfo(result);
	return 0;
}

static socklen_t sockaddr_len(const struct sockaddr *addr)
{
	if (addr->sa_family == AF_INET) {
		return sizeof(struct sockaddr_in);
	}

	return sizeof(struct sockaddr_in6);
}

static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_NON_SECURE) {
		fds[0].fd = client->transport.tcp.sock;
	}
#if defined(CONFIG_MQTT_LIB_TLS)
	if (client->transport.type == MQTT_TRANSPORT_SECURE) {
		fds[0].fd = client->transport.tls.sock;
	}
#endif
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

static void mark_session_ended(int reason)
{
	/*
	 * Broker-side failures end the active session but do not disable MQTT.
	 * The worker decides whether to retry based on mqtt_enabled.
	 */
	session_end_reason = reason;
	atomic_set(&session_ended, true);
}

static int publish_payload(const char *topic, const char *payload)
{
	struct mqtt_publish_param param = { 0 };
	int rc;

	/* zbus observers may fire before the operator requests MQTT. */
	if (!atomic_get(&mqtt_enabled)) {
		return 0;
	}

	if (!status_publish_ready()) {
		status_count_publish_error(-ENOTCONN);
		return -ENOTCONN;
	}

	param.message.topic.topic.utf8 = (uint8_t *)topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	param.message.payload.data = (uint8_t *)payload;
	param.message.payload.len = strlen(payload);
	param.message_id = sys_rand16_get();

	rc = mqtt_publish(&client_ctx, &param);
	if (rc) {
		LOG_ERR("MQTT publish failed on %s: %d", topic, rc);
		status_count_publish_error(MQTT_STATUS_ERR_PUBLISH);
		return rc;
	}

	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.publish_count++;
	current_status.last_error = 0;
	k_mutex_unlock(&status_lock);
	return 0;
}

static int publish_telemetry_msg(const struct telemetry_sample_msg *msg)
{
	char payload[640];
	int rc;

	rc = bike_mqtt_format_telemetry_json(msg, payload, sizeof(payload));
	if (rc < 0) {
		status_count_publish_error(rc);
		return rc;
	}

	return publish_payload(telemetry_topic, payload);
}

static int publish_state_event_msg(const struct bike_state_msg *msg)
{
	char payload[192];
	int rc;

	if (msg->event == BIKE_STATE_EVENT_NONE) {
		return 0;
	}

	rc = bike_mqtt_format_state_event_json(msg, payload, sizeof(payload));
	if (rc < 0) {
		status_count_publish_error(rc);
		return rc;
	}

	return publish_payload(event_topic, payload);
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
		status_count_parse_error(MQTT_STATUS_ERR_PARSE);
		return;
	}

	(void)bike_mqtt_handle_command_payload(payload_buffer, rc);
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
			current_status.last_error = MQTT_STATUS_ERR_CONNACK;
			k_mutex_unlock(&status_lock);
			mark_session_ended(MQTT_STATUS_ERR_CONNACK);
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
			status_set_error(MQTT_STATUS_ERR_SUBACK);
			mark_session_ended(MQTT_STATUS_ERR_SUBACK);
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
		mark_session_ended(evt->result);
		break;
	default:
		break;
	}
}

static bool should_use_secure_transport(uint16_t port)
{
	if (port == 1883) {
		return false;
	}
	if (port == 8883) {
		return true;
	}

	return IS_ENABLED(CONFIG_BIKE_MQTT_SECURE_DEFAULT);
}

static void client_init(bool secure, const char *host)
{
	const char *token = bike_config_get_device_token();

	mqtt_client_init(&client_ctx);

	client_ctx.broker = &broker_addr;
	client_ctx.evt_cb = mqtt_evt_handler;
	client_ctx.client_id.utf8 = (uint8_t *)client_id;
	client_ctx.client_id.size = strlen(client_id);
	username.utf8 = (uint8_t *)client_id;
	username.size = strlen(client_id);
	password.utf8 = (uint8_t *)token;
	password.size = strlen(token);
	client_ctx.user_name = username.size > 0 ? &username : NULL;
	client_ctx.password = password.size > 0 ? &password : NULL;
	client_ctx.protocol_version = MQTT_VERSION_3_1_1;
	client_ctx.rx_buf = rx_buffer;
	client_ctx.rx_buf_size = sizeof(rx_buffer);
	client_ctx.tx_buf = tx_buffer;
	client_ctx.tx_buf_size = sizeof(tx_buffer);
	client_ctx.transport.type = MQTT_TRANSPORT_NON_SECURE;

#if defined(CONFIG_MQTT_LIB_TLS)
	if (secure) {
		struct mqtt_sec_config *tls_config;

		client_ctx.transport.type = MQTT_TRANSPORT_SECURE;
		tls_config = &client_ctx.transport.tls.config;
		tls_config->peer_verify = TLS_PEER_VERIFY_REQUIRED;
		tls_config->sec_tag_list = tls_sec_tags;
		tls_config->sec_tag_count = ARRAY_SIZE(tls_sec_tags);
		tls_config->hostname = host;
	}
#else
	ARG_UNUSED(secure);
	ARG_UNUSED(host);
#endif
}

static bool lte_ready_for_mqtt(void)
{
	struct bike_lte_status status;

	bike_lte_status_get(&status);
	return !status.supported || status.connected;
}

static void clear_session_status(int reason)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.connecting = false;
	current_status.connected = false;
	current_status.subscribed = false;
	current_status.disconnect_reason = reason;
	k_mutex_unlock(&status_lock);
}

static int start_session(void)
{
	const char *host = bike_config_get_mqtt_host();
	uint16_t port = bike_config_get_mqtt_port();
	const char *id = bike_config_get_id();
	bool secure = should_use_secure_transport(port);
	int rc;

	rc = bike_mqtt_build_command_topic(id, command_topic,
					   sizeof(command_topic));
	if (rc) {
		status_set_error(rc);
		return rc;
	}

	rc = bike_mqtt_build_telemetry_topic(id, telemetry_topic,
					     sizeof(telemetry_topic));
	if (rc) {
		status_set_error(rc);
		return rc;
	}

	rc = bike_mqtt_build_event_topic(id, event_topic, sizeof(event_topic));
	if (rc) {
		status_set_error(rc);
		return rc;
	}

	rc = resolve_broker(host, port, &broker_addr);
	if (rc) {
		status_set_error(rc);
		return rc;
	}

	strncpy(client_id, id, sizeof(client_id) - 1);
	client_id[sizeof(client_id) - 1] = '\0';
	client_init(secure, host);

	/* Reset per-session state before MQTT events can arrive. */
	atomic_clear(&session_ended);
	session_end_reason = 0;

	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.connecting = true;
	current_status.connected = false;
	current_status.subscribed = false;
	current_status.secure = secure;
	current_status.broker_port = port;
	current_status.last_error = 0;
	current_status.disconnect_reason = 0;
	strncpy(current_status.broker_host, host,
		sizeof(current_status.broker_host) - 1);
	current_status.broker_host[sizeof(current_status.broker_host) - 1] = '\0';
	strncpy(current_status.command_topic, command_topic,
		sizeof(current_status.command_topic) - 1);
	current_status.command_topic[sizeof(current_status.command_topic) - 1] = '\0';
	strncpy(current_status.telemetry_topic, telemetry_topic,
		sizeof(current_status.telemetry_topic) - 1);
	current_status.telemetry_topic[sizeof(current_status.telemetry_topic) - 1] = '\0';
	strncpy(current_status.event_topic, event_topic,
		sizeof(current_status.event_topic) - 1);
	current_status.event_topic[sizeof(current_status.event_topic) - 1] = '\0';
	k_mutex_unlock(&status_lock);

	rc = mqtt_connect(&client_ctx);
	if (rc) {
		LOG_ERR("MQTT socket connect failed: %d", rc);
		clear_session_status(MQTT_STATUS_ERR_CONNECT);
		status_set_error(MQTT_STATUS_ERR_CONNECT);
		return rc;
	}

	prepare_fds(&client_ctx);
	LOG_INF("MQTT connect started: %s:%u (%s)", host, port,
		secure ? "tls" : "tcp");
	return 0;
}

static int poll_session(void)
{
	while (atomic_get(&mqtt_enabled) && !atomic_get(&stop_requested) &&
	       !atomic_get(&session_ended)) {
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
			return rc;
		}

		if (rc > 0 && (fds[0].revents & ZSOCK_POLLIN)) {
			rc = mqtt_input(&client_ctx);
			if (rc) {
				LOG_ERR("MQTT input failed: %d", rc);
				status_set_error(rc);
				return rc;
			}
			if (atomic_get(&session_ended)) {
				return session_end_reason;
			}
		}

		rc = mqtt_live(&client_ctx);
		if (rc == 0) {
			rc = mqtt_input(&client_ctx);
			if (rc) {
				LOG_ERR("MQTT ping response failed: %d", rc);
				status_set_error(rc);
				return rc;
			}
			if (atomic_get(&session_ended)) {
				return session_end_reason;
			}
		} else if (rc != -EAGAIN) {
			LOG_ERR("MQTT keepalive failed: %d", rc);
			status_set_error(rc);
			return rc;
		}
	}

	return atomic_get(&session_ended) ? session_end_reason : 0;
}

static void wait_before_retry(int32_t backoff_ms)
{
	int64_t deadline = k_uptime_get() + backoff_ms;

	while (atomic_get(&mqtt_enabled) && !atomic_get(&stop_requested) &&
	       k_uptime_get() < deadline) {
		k_sleep(K_MSEC(MIN(1000, deadline - k_uptime_get())));
	}
}

static void mqtt_worker(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	for (;;) {
		int32_t backoff_ms = CONFIG_BIKE_MQTT_RECONNECT_INITIAL_MS;

		k_sem_take(&connect_sem, K_FOREVER);

		while (atomic_get(&mqtt_enabled) && !atomic_get(&stop_requested)) {
			bool session_opened = false;
			int rc;

			if (!lte_ready_for_mqtt()) {
				/* LTE readiness gates auto-retry and avoids boot loops. */
				wait_before_retry(1000);
				continue;
			}

			rc = start_session();
			if (!rc) {
				session_opened = true;
				backoff_ms = CONFIG_BIKE_MQTT_RECONNECT_INITIAL_MS;
				rc = poll_session();
			}

			if (session_opened) {
				(void)mqtt_abort(&client_ctx);
			}
			clear_session_status(rc);

			if (!atomic_get(&mqtt_enabled) ||
			    atomic_get(&stop_requested)) {
				break;
			}

			k_mutex_lock(&status_lock, K_FOREVER);
			current_status.reconnect_count++;
			k_mutex_unlock(&status_lock);
			wait_before_retry(backoff_ms);
			/* Bounded exponential backoff keeps retries visible but quiet. */
			backoff_ms = MIN(backoff_ms * 2,
					 CONFIG_BIKE_MQTT_RECONNECT_MAX_MS);
		}

		if (atomic_get(&mqtt_enabled) &&
		    !atomic_get(&stop_requested)) {
			continue;
		}

		atomic_clear(&thread_running);
		atomic_clear(&stop_requested);
		clear_session_status(0);
	}
}

K_THREAD_DEFINE(mqtt_thread, CONFIG_BIKE_MQTT_THREAD_STACK_SIZE, mqtt_worker,
		NULL, NULL, NULL, CONFIG_BIKE_MQTT_THREAD_PRIORITY, 0, 0);

int bike_mqtt_init(void)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	if (current_status.initialized) {
		k_mutex_unlock(&status_lock);
		return 0;
	}

	current_status.initialized = true;
	current_status.last_error = 0;
	k_mutex_unlock(&status_lock);
	LOG_INF("MQTT client initialized");
	return 0;
}

int bike_mqtt_connect(void)
{
	int rc;

	rc = bike_mqtt_init();
	if (rc) {
		return rc;
	}

	if (!bike_config_is_valid(bike_config_get())) {
		status_set_error(-EINVAL);
		return -EINVAL;
	}

	atomic_set(&mqtt_enabled, true);
	atomic_clear(&stop_requested);
	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.enabled = true;
	k_mutex_unlock(&status_lock);

	if (atomic_get(&thread_running)) {
		k_sem_give(&connect_sem);
		return 0;
	}

	atomic_set(&thread_running, true);
	k_sem_give(&connect_sem);
	LOG_INF("MQTT connect requested");
	return 0;
}

int bike_mqtt_tcp_probe(void)
{
	const char *host = bike_config_get_mqtt_host();
	uint16_t port = bike_config_get_mqtt_port();
	struct sockaddr_storage probe_addr;
	const struct sockaddr *addr = (const struct sockaddr *)&probe_addr;
	int sock;
	int rc;

	if (!host[0] || !port) {
		status_set_error(-EINVAL);
		return -EINVAL;
	}

	if (!lte_ready_for_mqtt()) {
		status_set_error(-ENETUNREACH);
		return -ENETUNREACH;
	}

	rc = resolve_broker(host, port, &probe_addr);
	if (rc) {
		status_set_error(rc);
		return rc;
	}

	sock = zsock_socket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		rc = -errno;
		LOG_ERR("MQTT TCP probe socket failed: %d", rc);
		status_set_error(rc);
		return rc;
	}

	rc = zsock_connect(sock, addr, sockaddr_len(addr));
	if (rc < 0) {
		rc = -errno;
		LOG_ERR("MQTT TCP probe failed: %d", rc);
		status_set_error(rc);
		(void)zsock_close(sock);
		return rc;
	}

	LOG_INF("MQTT TCP probe connected: %s:%u", host, port);
	status_set_error(0);
	(void)zsock_close(sock);
	return 0;
}

int bike_mqtt_disconnect(void)
{
	int rc = 0;

	if (!atomic_get(&thread_running)) {
		atomic_clear(&mqtt_enabled);
		k_mutex_lock(&status_lock, K_FOREVER);
		current_status.enabled = false;
		k_mutex_unlock(&status_lock);
		return 0;
	}

	atomic_clear(&mqtt_enabled);
	atomic_set(&stop_requested, true);
	if (status_session_active()) {
		rc = mqtt_disconnect(&client_ctx);
		if (rc) {
			(void)mqtt_abort(&client_ctx);
		}
	}

	k_mutex_lock(&status_lock, K_FOREVER);
	current_status.connecting = false;
	current_status.connected = false;
	current_status.subscribed = false;
	current_status.enabled = false;
	current_status.disconnect_reason = rc;
	current_status.last_error = rc;
	k_mutex_unlock(&status_lock);

	return rc;
}

static void state_listener(const struct zbus_channel *chan)
{
	const struct bike_state_msg *msg = zbus_chan_const_msg(chan);

	(void)publish_state_event_msg(msg);
}

static void telemetry_listener(const struct zbus_channel *chan)
{
	const struct telemetry_sample_msg *msg = zbus_chan_const_msg(chan);

	(void)publish_telemetry_msg(msg);
}

ZBUS_LISTENER_DEFINE(mqtt_state_listener_node, state_listener);
ZBUS_CHAN_ADD_OBS(bike_state_chan, mqtt_state_listener_node, 20);

ZBUS_LISTENER_DEFINE(mqtt_telemetry_listener_node, telemetry_listener);
ZBUS_CHAN_ADD_OBS(telemetry_sample_chan, mqtt_telemetry_listener_node, 20);

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

int bike_mqtt_tcp_probe(void)
{
	current_status.last_error = -ENOTSUP;
	return -ENOTSUP;
}

#endif
