#include "boot.h"
#include "usb_descriptors.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zuf2, CONFIG_ZUF2_LOG_LEVEL);

#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
#define HAS_BOOT_BUTTON 1
static const struct gpio_dt_spec boot_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#else
#define HAS_BOOT_BUTTON 0
#endif

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

int main(void)
{
	bool force_uf2 = IS_ENABLED(CONFIG_ZUF2_ALWAYS_ENTER) || boot_button_pressed();

	if (!force_uf2 && zuf2_app_is_valid()) {
		zuf2_boot_app();
	}

	if (force_uf2) {
		LOG_INF("staying in UF2 mode");
	} else {
		LOG_INF("no valid app found; staying in UF2 mode");
	}

	return zuf2_usb_start();
}
