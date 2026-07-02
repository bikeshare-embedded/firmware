#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "channels.h"
#include "button.h"
#include "config.h"
#include "gnss.h"
#include "state.h"
#include "led.h"
#include "telemetry.h"

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

ZTEST(telemetry, test_sample_includes_valid_gnss_fix)
{
	struct bike_gnss_fix fix = {
		.supported = true,
		.running = true,
		.valid = true,
		.uptime_ms = 1200,
		.latitude_microdegrees = -3456789,
		.longitude_microdegrees = -38123456,
		.altitude_mm = 12345,
		.accuracy_mm = 6789,
	};
	struct telemetry_sample_msg sample;

	bike_telemetry_fill_sample(&sample, "BIKE_TST", BIKE_STATE_IN_USE,
				   4567, &fix);

	zassert_equal(strcmp(sample.bike_id, "BIKE_TST"), 0);
	zassert_equal(sample.state, BIKE_STATE_IN_USE);
	zassert_equal(sample.uptime_ms, 4567);
	zassert_true(sample.gnss_fix_valid);
	zassert_equal(sample.gnss_latitude_microdegrees, -3456789);
	zassert_equal(sample.gnss_longitude_microdegrees, -38123456);
	zassert_equal(sample.gnss_altitude_mm, 12345);
	zassert_equal(sample.gnss_accuracy_mm, 6789);
}

ZTEST(telemetry, test_sample_reports_no_fix_explicitly)
{
	struct bike_gnss_fix fix = {
		.supported = true,
		.running = true,
		.valid = false,
		.uptime_ms = 1200,
		.latitude_microdegrees = -3456789,
		.longitude_microdegrees = -38123456,
		.altitude_mm = 12345,
		.accuracy_mm = 6789,
	};
	struct telemetry_sample_msg sample;

	bike_telemetry_fill_sample(&sample, "BIKE_TST", BIKE_STATE_AVAILABLE,
				   4567, &fix);

	zassert_equal(strcmp(sample.bike_id, "BIKE_TST"), 0);
	zassert_equal(sample.state, BIKE_STATE_AVAILABLE);
	zassert_equal(sample.uptime_ms, 4567);
	zassert_false(sample.gnss_fix_valid);
	zassert_equal(sample.gnss_latitude_microdegrees, 0);
	zassert_equal(sample.gnss_longitude_microdegrees, 0);
	zassert_equal(sample.gnss_altitude_mm, 0);
	zassert_equal(sample.gnss_accuracy_mm, 0);
}

ZTEST(telemetry, test_gnss_cache_transitions_valid_to_no_fix)
{
	struct bike_gnss_fix valid_fix = {
		.supported = true,
		.running = true,
		.valid = true,
		.uptime_ms = 100,
		.latitude_microdegrees = 111,
		.longitude_microdegrees = 222,
		.altitude_mm = 333,
	};
	struct bike_gnss_fix no_fix = {
		.supported = true,
		.running = true,
		.valid = false,
		.uptime_ms = 200,
	};
	struct bike_gnss_fix latest;

	bike_gnss_init();
	bike_gnss_store_fix(&valid_fix);
	zassert_true(bike_gnss_get_latest(&latest));
	zassert_equal(latest.latitude_microdegrees, 111);

	bike_gnss_store_fix(&no_fix);
	zassert_false(bike_gnss_get_latest(&latest));
	zassert_false(latest.valid);
	zassert_equal(latest.uptime_ms, 200);
	zassert_equal(latest.latitude_microdegrees, 0);
}

ZTEST_SUITE(bike_config, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(bike_state, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(led_status, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(button_input, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(telemetry, NULL, NULL, NULL, NULL, NULL);
