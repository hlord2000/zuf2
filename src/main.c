#include "app_partition.h"
#include "boot.h"
#include "usb_descriptors.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zuf2, CONFIG_ZUF2_LOG_LEVEL);

#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
#define HAS_BOOT_BUTTON 1
static const struct gpio_dt_spec boot_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#else
#define HAS_BOOT_BUTTON 0
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_gpregret)
#define HAS_DOUBLE_TAP_RETENTION 1
static const struct device *const double_tap_retention =
	DEVICE_DT_GET(DT_INST(0, nordic_nrf_gpregret));
#else
#define HAS_DOUBLE_TAP_RETENTION 0
struct double_tap_state {
	uint32_t magic;
	uint32_t magic_inv;
	uint32_t app_base;
};
static struct double_tap_state double_tap_state __noinit;
#endif

#define DOUBLE_TAP_MAGIC ((uint8_t)CONFIG_ZUF2_DOUBLE_TAP_MAGIC)
#define DOUBLE_TAP_MAGIC_INV (~DOUBLE_TAP_MAGIC)

static bool boot_button_pressed(void)
{
	int ret;

	if (!IS_ENABLED(CONFIG_ZUF2_BOOT_BUTTON) || !HAS_BOOT_BUTTON) {
		return false;
	}

#if HAS_BOOT_BUTTON
	if (!device_is_ready(boot_button.port)) {
		LOG_WRN("boot button device is not ready");
		return false;
	}

	ret = gpio_pin_configure_dt(&boot_button, GPIO_INPUT);
	if (ret != 0) {
		LOG_WRN("failed to configure boot button: %d", ret);
		return false;
	}

	ret = gpio_pin_get_dt(&boot_button);
	if (ret < 0) {
		LOG_WRN("failed to read boot button: %d", ret);
		return false;
	}

	return ret > 0;
#else
	return false;
#endif
}

static void double_tap_clear(void)
{
#if HAS_DOUBLE_TAP_RETENTION
	int ret;

	if (!device_is_ready(double_tap_retention)) {
		return;
	}

	ret = retained_mem_clear(double_tap_retention);
	if (ret != 0) {
		LOG_WRN("failed to clear double-tap marker: %d", ret);
	}
#else
	double_tap_state.magic = 0U;
	double_tap_state.magic_inv = 0U;
	double_tap_state.app_base = 0U;
#endif
}

static bool double_tap_is_armed(void)
{
	if (!IS_ENABLED(CONFIG_ZUF2_DOUBLE_TAP_RESET)) {
		return false;
	}

#if HAS_DOUBLE_TAP_RETENTION
	uint8_t marker;
	int ret;

	if (!device_is_ready(double_tap_retention)) {
		LOG_WRN("double-tap retention device is not ready");
		return false;
	}

	ret = retained_mem_read(double_tap_retention, 0, &marker, sizeof(marker));
	if (ret != 0) {
		LOG_WRN("failed to read double-tap marker: %d", ret);
		return false;
	}

	return marker == DOUBLE_TAP_MAGIC;
#else
	return IS_ENABLED(CONFIG_ZUF2_DOUBLE_TAP_RESET) &&
	       double_tap_state.magic == (uint32_t)DOUBLE_TAP_MAGIC &&
	       double_tap_state.magic_inv == (uint32_t)DOUBLE_TAP_MAGIC_INV &&
	       double_tap_state.app_base == ZUF2_APP_PARTITION_ADDRESS;
#endif
}

static void double_tap_arm(void)
{
#if HAS_DOUBLE_TAP_RETENTION
	uint8_t marker = DOUBLE_TAP_MAGIC;
	int ret;

	if (!device_is_ready(double_tap_retention)) {
		LOG_WRN("double-tap retention device is not ready");
		return;
	}

	ret = retained_mem_write(double_tap_retention, 0, &marker, sizeof(marker));
	if (ret != 0) {
		LOG_WRN("failed to arm double-tap marker: %d", ret);
	}
#else
	double_tap_state.magic = DOUBLE_TAP_MAGIC;
	double_tap_state.magic_inv = DOUBLE_TAP_MAGIC_INV;
	double_tap_state.app_base = ZUF2_APP_PARTITION_ADDRESS;
#endif
}

static void double_tap_wait_window(void)
{
	if (!IS_ENABLED(CONFIG_ZUF2_DOUBLE_TAP_RESET)) {
		return;
	}

	k_busy_wait(CONFIG_ZUF2_DOUBLE_TAP_DELAY_MS * 1000U);
	double_tap_clear();
}

int main(void)
{
	bool double_tap_detected = false;
	bool force_uf2;
	bool app_valid;

	if (double_tap_is_armed()) {
		double_tap_clear();
		double_tap_detected = true;
	} else {
		double_tap_arm();
	}

	app_valid = zuf2_app_is_valid();
	force_uf2 = IS_ENABLED(CONFIG_ZUF2_ALWAYS_ENTER) || boot_button_pressed() ||
		    double_tap_detected;

	if (!force_uf2 && app_valid) {
		double_tap_wait_window();
		zuf2_boot_app();
	}

	double_tap_clear();

	if (double_tap_detected) {
		LOG_INF("double-tap reset detected; staying in UF2 mode");
	} else if (force_uf2) {
		LOG_INF("staying in UF2 mode");
	} else {
		LOG_INF("no valid app found; staying in UF2 mode");
	}

	return zuf2_usb_start();
}
