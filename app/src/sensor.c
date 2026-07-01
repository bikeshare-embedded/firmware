#include "sensor.h"

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(motion_sensor, LOG_LEVEL_INF);

#define MOTION_SAMPLE_PERIOD K_SECONDS(2)
#define MOTION_GRAVITY_MILLI_MS2 9810
#define MOTION_STILL_TOLERANCE_MILLI_MS2 2000
#define MOTION_RAD_TO_DEG_X10 57296

static struct motion_sensor_sample latest_sample;
static struct k_mutex latest_lock;

bool motion_sensor_get_latest(struct motion_sensor_sample *sample)
{
	if (sample == NULL) {
		return false;
	}

	k_mutex_lock(&latest_lock, K_FOREVER);
	*sample = latest_sample;
	k_mutex_unlock(&latest_lock);

	return sample->valid;
}

#if DT_HAS_COMPAT_STATUS_OKAY(invensense_mpu6050) && IS_ENABLED(CONFIG_SENSOR)

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

static const struct device *const mpu6050 = DEVICE_DT_GET_ONE(invensense_mpu6050);
static struct k_work_delayable sample_work;

static bool acceleration_indicates_motion(const int32_t accel_milli_ms2[3])
{
	const int32_t lower = MOTION_GRAVITY_MILLI_MS2 - MOTION_STILL_TOLERANCE_MILLI_MS2;
	const int32_t upper = MOTION_GRAVITY_MILLI_MS2 + MOTION_STILL_TOLERANCE_MILLI_MS2;
	int64_t magnitude_sq = 0;

	for (size_t i = 0; i < 3; i++) {
		magnitude_sq += (int64_t)accel_milli_ms2[i] * accel_milli_ms2[i];
	}

	return magnitude_sq < ((int64_t)lower * lower) ||
	       magnitude_sq > ((int64_t)upper * upper);
}

static int32_t sensor_value_to_milli_i32(const struct sensor_value *value)
{
	return (int32_t)sensor_value_to_milli(value);
}

static int32_t gyro_mrad_s_to_deg_s_x10(int32_t gyro_milli_rad_s)
{
	return (gyro_milli_rad_s * MOTION_RAD_TO_DEG_X10) / 100000;
}

static int32_t decimal_abs(int32_t value)
{
	return value < 0 ? -value : value;
}

static int read_sample(struct motion_sensor_sample *sample)
{
	struct sensor_value accel[3];
	struct sensor_value gyro[3];
	struct sensor_value die_temp;
	int ret;

	ret = sensor_sample_fetch(mpu6050);
	if (ret != 0) {
		return ret;
	}

	ret = sensor_channel_get(mpu6050, SENSOR_CHAN_ACCEL_XYZ, accel);
	if (ret != 0) {
		return ret;
	}

	ret = sensor_channel_get(mpu6050, SENSOR_CHAN_GYRO_XYZ, gyro);
	if (ret != 0) {
		return ret;
	}

	ret = sensor_channel_get(mpu6050, SENSOR_CHAN_DIE_TEMP, &die_temp);
	if (ret != 0) {
		return ret;
	}

	*sample = (struct motion_sensor_sample) {
		.valid = true,
		.uptime_ms = k_uptime_get(),
		.accel_milli_ms2 = {
			sensor_value_to_milli_i32(&accel[0]),
			sensor_value_to_milli_i32(&accel[1]),
			sensor_value_to_milli_i32(&accel[2]),
		},
		.gyro_milli_rad_s = {
			sensor_value_to_milli_i32(&gyro[0]),
			sensor_value_to_milli_i32(&gyro[1]),
			sensor_value_to_milli_i32(&gyro[2]),
		},
		.die_temp_milli_c = sensor_value_to_milli_i32(&die_temp),
	};
	sample->moving = acceleration_indicates_motion(sample->accel_milli_ms2);

	return 0;
}

static void sample_work_handler(struct k_work *work)
{
	struct motion_sensor_sample sample;
	int ret = read_sample(&sample);

	ARG_UNUSED(work);

	if (ret == 0) {
		k_mutex_lock(&latest_lock, K_FOREVER);
		latest_sample = sample;
		k_mutex_unlock(&latest_lock);

		int32_t ax_x100 = sample.accel_milli_ms2[0] / 10;
		int32_t ay_x100 = sample.accel_milli_ms2[1] / 10;
		int32_t az_x100 = sample.accel_milli_ms2[2] / 10;
		int32_t gx_x10 = gyro_mrad_s_to_deg_s_x10(sample.gyro_milli_rad_s[0]);
		int32_t gy_x10 = gyro_mrad_s_to_deg_s_x10(sample.gyro_milli_rad_s[1]);
		int32_t gz_x10 = gyro_mrad_s_to_deg_s_x10(sample.gyro_milli_rad_s[2]);

		LOG_INF("motion accel=(%d.%02d,%d.%02d,%d.%02d) m/s2 "
			"velocity=(%d.%01d,%d.%01d,%d.%01d) deg/s "
			"moving=%d",
			ax_x100 / 100, decimal_abs(ax_x100 % 100),
			ay_x100 / 100, decimal_abs(ay_x100 % 100),
			az_x100 / 100, decimal_abs(az_x100 % 100),
			gx_x10 / 10, decimal_abs(gx_x10 % 10),
			gy_x10 / 10, decimal_abs(gy_x10 % 10),
			gz_x10 / 10, decimal_abs(gz_x10 % 10),
			sample.moving);
	} else {
		LOG_WRN("MPU6050 sample failed: %d", ret);
	}

	k_work_schedule(&sample_work, MOTION_SAMPLE_PERIOD);
}

void motion_sensor_init(void)
{
	k_mutex_init(&latest_lock);

	if (!device_is_ready(mpu6050)) {
		LOG_WRN("MPU6050 device is not ready");
		return;
	}

	k_work_init_delayable(&sample_work, sample_work_handler);
	k_work_schedule(&sample_work, K_NO_WAIT);
	LOG_INF("MPU6050 motion sensor initialized");
}

#else

void motion_sensor_init(void)
{
	k_mutex_init(&latest_lock);
	LOG_INF("motion sensor disabled: no MPU6050 devicetree node");
}

#endif
