#include "uf2_disk.h"
#include "usb_descriptors.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_DECLARE(zuf2, CONFIG_ZUF2_LOG_LEVEL);

USBD_DEVICE_DEFINE(zuf2_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   CONFIG_ZUF2_USB_VID, CONFIG_ZUF2_USB_PID);

USBD_DESC_LANG_DEFINE(zuf2_lang);
USBD_DESC_MANUFACTURER_DEFINE(zuf2_mfr, CONFIG_ZUF2_USB_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(zuf2_product, CONFIG_ZUF2_USB_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(zuf2_sn)));

USBD_DESC_CONFIG_DEFINE(zuf2_fs_desc, "UF2 FS");
USBD_DESC_CONFIG_DEFINE(zuf2_hs_desc, "UF2 HS");
USBD_CONFIGURATION_DEFINE(zuf2_fs_config, 0, 100, &zuf2_fs_desc);
USBD_CONFIGURATION_DEFINE(zuf2_hs_config, 0, 100, &zuf2_hs_desc);

USBD_DEFINE_MSC_LUN(zuf2_lun, ZUF2_DISK_NAME, "zuf2", "UF2 Boot", "0.1");

static struct usbd_context *setup_usb(void)
{
	int ret;

	ret = usbd_add_descriptor(&zuf2_usbd, &zuf2_lang);
	if (ret != 0) {
		return NULL;
	}

	ret = usbd_add_descriptor(&zuf2_usbd, &zuf2_mfr);
	if (ret != 0) {
		return NULL;
	}

	ret = usbd_add_descriptor(&zuf2_usbd, &zuf2_product);
	if (ret != 0) {
		return NULL;
	}

	IF_ENABLED(CONFIG_HWINFO, (ret = usbd_add_descriptor(&zuf2_usbd, &zuf2_sn);))
	if (ret != 0) {
		return NULL;
	}

	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_caps_speed(&zuf2_usbd) == USBD_SPEED_HS) {
		ret = usbd_add_configuration(&zuf2_usbd, USBD_SPEED_HS,
					     &zuf2_hs_config);
		if (ret != 0) {
			return NULL;
		}

		ret = usbd_register_all_classes(&zuf2_usbd, USBD_SPEED_HS, 1, NULL);
		if (ret != 0) {
			return NULL;
		}

		usbd_device_set_code_triple(&zuf2_usbd, USBD_SPEED_HS, 0, 0, 0);
	}

	ret = usbd_add_configuration(&zuf2_usbd, USBD_SPEED_FS, &zuf2_fs_config);
	if (ret != 0) {
		return NULL;
	}

	ret = usbd_register_all_classes(&zuf2_usbd, USBD_SPEED_FS, 1, NULL);
	if (ret != 0) {
		return NULL;
	}

	usbd_device_set_code_triple(&zuf2_usbd, USBD_SPEED_FS, 0, 0, 0);

	ret = usbd_init(&zuf2_usbd);
	if (ret != 0) {
		return NULL;
	}

	return &zuf2_usbd;
}

int zuf2_usb_start(void)
{
	struct usbd_context *ctx;
	int ret;

	ctx = setup_usb();
	if (ctx == NULL) {
		LOG_ERR("failed to initialize USB");
		return -ENODEV;
	}

	ret = usbd_enable(ctx);
	if (ret != 0) {
		LOG_ERR("failed to enable USB: %d", ret);
		return ret;
	}

	LOG_INF("UF2 MSC device is active");
	return 0;
}
