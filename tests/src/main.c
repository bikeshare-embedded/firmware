#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "channels.h"
#include "button.h"
#include "config.h"
#include "state.h"
#include "led.h"
#include "mqtt_client.h"

ZTEST(bike_config, test_config_validation)
{
	struct bike_config cfg = { 0 };
	uint16_t port;

	zassert_false(bike_config_is_valid(&cfg));

	zassert_ok(bike_config_parse_mqtt_port("1883", &port));
	zassert_equal(port, 1883);
	zassert_equal(bike_config_parse_mqtt_port("0", &port), -EINVAL);
	zassert_equal(bike_config_parse_mqtt_port("65536", &port), -EINVAL);
	zassert_equal(bike_config_parse_mqtt_port("abc", &port), -EINVAL);

	strcpy(cfg.id, "BIKE_001");
	strcpy(cfg.device_token, "TOKEN");
	strcpy(cfg.mqtt_host, "broker.example.com");
	cfg.mqtt_port = 1883;
	strcpy(cfg.apn, "internet");

	zassert_true(bike_config_is_valid(&cfg));
}

ZTEST(bike_state, test_core_state_transitions)
{
	zassert_ok(bike_config_init());
	zassert_ok(bike_state_init());
	zassert_equal(bike_state_get(), BIKE_STATE_UNREGISTERED);

	zassert_ok(bike_config_set_id("BIKE_001"));
	zassert_ok(bike_config_set_device_token("TOKEN"));
	zassert_ok(bike_config_set_mqtt_host("broker.example.com"));
	zassert_ok(bike_config_set_mqtt_port(1883));
	zassert_ok(bike_config_set_apn("internet"));
	zassert_ok(bike_state_refresh_config());
	zassert_equal(bike_state_get(), BIKE_STATE_AVAILABLE);

	zassert_equal(bike_state_cancel("RENTAL_000"), -EACCES);
	zassert_ok(bike_state_authorize("RENTAL_001"));
	zassert_equal(bike_state_get(), BIKE_STATE_RESERVED);
	zassert_equal(strcmp(bike_state_get_rental_id(), "RENTAL_001"), 0);

	zassert_equal(bike_state_authorize("RENTAL_002"), -EACCES);
	zassert_equal(bike_state_cancel("RENTAL_002"), -EINVAL);
	zassert_equal(bike_state_get(), BIKE_STATE_RESERVED);

	zassert_ok(bike_state_button_press());
	zassert_equal(bike_state_get(), BIKE_STATE_IN_USE);
	zassert_equal(bike_state_cancel("RENTAL_001"), -EACCES);

	zassert_ok(bike_state_button_press());
	zassert_equal(bike_state_get(), BIKE_STATE_AVAILABLE);
	zassert_equal(bike_state_get_rental_id()[0], '\0');
}

ZTEST(led_status, test_state_to_pattern_mapping)
{
	zassert_equal(led_status_pattern_for_state(BIKE_STATE_UNREGISTERED),
		      LED_STATUS_OFF);
	zassert_equal(led_status_pattern_for_state(BIKE_STATE_AVAILABLE),
		      LED_STATUS_BLINK_SLOW);
	zassert_equal(led_status_pattern_for_state(BIKE_STATE_RESERVED),
		      LED_STATUS_BLINK_FAST);
	zassert_equal(led_status_pattern_for_state(BIKE_STATE_IN_USE),
		      LED_STATUS_SOLID_ON);
	zassert_equal(led_status_pattern_for_state(BIKE_STATE_ERROR),
		      LED_STATUS_ERROR);
}

ZTEST(led_status, test_cached_pattern_is_applied_on_init)
{
	struct bike_state_msg msg = {
		.state = BIKE_STATE_AVAILABLE,
	};

	zassert_ok(zbus_chan_pub(&bike_state_chan, &msg, K_MSEC(100)));
	zassert_equal(led_status_get_pattern(), LED_STATUS_BLINK_SLOW);

	zassert_ok(led_status_init());
	zassert_equal(led_status_get_pattern(), LED_STATUS_BLINK_SLOW);
	zassert_true(led_status_is_on());
}

ZTEST(button_input, test_publish_press_drives_state_machine)
{
	button_input_reset_debounce();
	zassert_ok(bike_config_init());
	zassert_ok(bike_config_set_id("BIKE_001"));
	zassert_ok(bike_config_set_device_token("TOKEN"));
	zassert_ok(bike_config_set_mqtt_host("broker.example.com"));
	zassert_ok(bike_config_set_mqtt_port(1883));
	zassert_ok(bike_config_set_apn("internet"));
	zassert_ok(bike_state_init());
	zassert_equal(bike_state_get(), BIKE_STATE_AVAILABLE);

	zassert_ok(bike_state_authorize("RENTAL_003"));
	zassert_equal(bike_state_get(), BIKE_STATE_RESERVED);

	zassert_ok(button_input_publish_press(1234));
	zassert_equal(bike_state_get(), BIKE_STATE_IN_USE);

	zassert_ok(button_input_publish_press(5678));
	zassert_equal(bike_state_get(), BIKE_STATE_AVAILABLE);
}

ZTEST(button_input, test_debounce_rejects_duplicate_press)
{
	button_input_reset_debounce();
	zassert_ok(bike_config_init());
	zassert_ok(bike_config_set_id("BIKE_002"));
	zassert_ok(bike_config_set_device_token("TOKEN"));
	zassert_ok(bike_config_set_mqtt_host("broker.example.com"));
	zassert_ok(bike_config_set_mqtt_port(1883));
	zassert_ok(bike_config_set_apn("internet"));
	zassert_ok(bike_state_init());
	zassert_equal(bike_state_get(), BIKE_STATE_AVAILABLE);

	zassert_ok(bike_state_authorize("RENTAL_004"));
	zassert_equal(bike_state_get(), BIKE_STATE_RESERVED);

	zassert_ok(button_input_publish_press_debounced(1000));
	zassert_equal(bike_state_get(), BIKE_STATE_IN_USE);

	zassert_equal(button_input_publish_press_debounced(
			      1000 + BUTTON_INPUT_DEBOUNCE_MS - 1),
		      -EALREADY);
	zassert_equal(bike_state_get(), BIKE_STATE_IN_USE);

	zassert_ok(button_input_publish_press_debounced(
		1000 + BUTTON_INPUT_DEBOUNCE_MS));
	zassert_equal(bike_state_get(), BIKE_STATE_AVAILABLE);
}

ZTEST(mqtt_client, test_topic_construction)
{
	char topic[BIKE_MQTT_TOPIC_MAX_LEN];

	zassert_ok(bike_mqtt_build_command_topic("BIKE_001", topic,
						 sizeof(topic)));
	zassert_equal(strcmp(topic, "bikes/BIKE_001/commands"), 0);

	zassert_ok(bike_mqtt_build_state_topic("BIKE_001", topic,
					       sizeof(topic)));
	zassert_equal(strcmp(topic, "bikes/BIKE_001/state"), 0);

	zassert_equal(bike_mqtt_build_command_topic("", topic, sizeof(topic)),
		      -EINVAL);
	zassert_equal(bike_mqtt_build_state_topic("BIKE_001", topic, 8),
		      -ENAMETOOLONG);
}

ZTEST(mqtt_client, test_command_parsing)
{
	struct bike_backend_command_msg msg;
	char authorize[] = "{\"type\":\"rent_authorize\",\"rental_id\":\"R1\"}";
	char cancel[] = "{\"type\":\"rent_cancel\",\"rental_id\":\"R2\"}";
	char unknown[] = "{\"type\":\"lock\",\"rental_id\":\"R3\"}";
	char malformed[] = "{\"type\":\"rent_authorize\"";

	zassert_ok(bike_mqtt_parse_command(authorize, strlen(authorize), &msg));
	zassert_equal(msg.type, BIKE_BACKEND_RENT_AUTHORIZE);
	zassert_equal(strcmp(msg.rental_id, "R1"), 0);

	zassert_ok(bike_mqtt_parse_command(cancel, strlen(cancel), &msg));
	zassert_equal(msg.type, BIKE_BACKEND_RENT_CANCEL);
	zassert_equal(strcmp(msg.rental_id, "R2"), 0);

	zassert_equal(bike_mqtt_parse_command(unknown, strlen(unknown), &msg),
		      -ENOTSUP);
	zassert_true(bike_mqtt_parse_command(malformed, strlen(malformed),
					     &msg) < 0);
}

ZTEST(mqtt_client, test_command_handler_publishes_and_counts_errors)
{
	struct bike_backend_command_msg msg;
	struct bike_mqtt_status before;
	struct bike_mqtt_status after;
	char authorize[] = "{\"type\":\"rent_authorize\",\"rental_id\":\"R9\"}";
	char unknown[] = "{\"type\":\"bad\",\"rental_id\":\"R9\"}";

	bike_mqtt_status_get(&before);
	zassert_ok(bike_mqtt_handle_command_payload(authorize,
						    strlen(authorize)));
	zassert_ok(zbus_chan_read(&backend_command_chan, &msg, K_MSEC(100)));
	zassert_equal(msg.type, BIKE_BACKEND_RENT_AUTHORIZE);
	zassert_equal(strcmp(msg.rental_id, "R9"), 0);

	zassert_equal(bike_mqtt_handle_command_payload(unknown,
						       strlen(unknown)),
		      -ENOTSUP);
	bike_mqtt_status_get(&after);
	zassert_equal(after.parse_error_count, before.parse_error_count + 1);
}

ZTEST(mqtt_client, test_telemetry_json_formatting)
{
	struct bike_state_msg state_msg = {
		.state = BIKE_STATE_RESERVED,
		.rental_id = "R10",
		.updated_at_ms = 1234,
	};
	struct bike_button_event_msg button_msg = {
		.uptime_ms = 5678,
	};
	char payload[160];

	zassert_true(bike_mqtt_format_state_json(&state_msg, payload,
						 sizeof(payload)) > 0);
	zassert_equal(strcmp(payload,
			     "{\"state\":\"RESERVED\",\"rental_id\":\"R10\","
			     "\"updated_at_ms\":1234}"), 0);

	zassert_true(bike_mqtt_format_button_json(&button_msg, payload,
						  sizeof(payload)) > 0);
	zassert_equal(strcmp(payload,
			     "{\"type\":\"button_press\",\"uptime_ms\":5678}"),
		      0);
}

ZTEST(mqtt_client, test_error_names)
{
	zassert_equal(strcmp(bike_mqtt_error_name(0), "none"), 0);
	zassert_equal(strcmp(bike_mqtt_error_name(-EINVAL),
			     "invalid_argument"), 0);
	zassert_equal(strcmp(bike_mqtt_error_name(-ECONNREFUSED),
			     "connection_refused"), 0);
	zassert_equal(strcmp(bike_mqtt_error_name(-12345), "errno"), 0);
}

ZTEST_SUITE(bike_config, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(bike_state, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(led_status, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(button_input, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(mqtt_client, NULL, NULL, NULL, NULL, NULL);
