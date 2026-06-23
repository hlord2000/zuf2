#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zuf2_hello, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#define HAS_LED 1
#else
#define HAS_LED 0
#endif

int main(void)
{
	LOG_INF("zuf2 chain-loaded hello app");

#if HAS_LED
	if (!device_is_ready(led.port)) {
		LOG_ERR("LED device is not ready");
		return 0;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_ERR("failed to configure LED");
		return 0;
	}

	while (true) {
		(void)gpio_pin_toggle_dt(&led);
		k_sleep(K_MSEC(500));
	}
#else
	while (true) {
		k_sleep(K_SECONDS(1));
	}
#endif
}
