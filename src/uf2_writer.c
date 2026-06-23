#include "app_partition.h"
#include "uf2_format.h"
#include "uf2_writer.h"

#include <errno.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zuf2, CONFIG_ZUF2_LOG_LEVEL);

#define ZUF2_TRACKED_BLOCKS                                                                  \
	(((ZUF2_APP_PARTITION_SIZE + UF2_DEFAULT_PAYLOAD_SIZE - 1U) /                         \
	  UF2_DEFAULT_PAYLOAD_SIZE) + CONFIG_ZUF2_MAX_EXTRA_UF2_BLOCKS)

struct write_state {
	bool active;
	bool flash_prepared;
	bool complete;
	uint32_t num_blocks;
	uint32_t num_written;
	uint8_t written_mask[(ZUF2_TRACKED_BLOCKS + 7U) / 8U];
};

static const struct flash_area *app_area;
static struct write_state state;
static K_MUTEX_DEFINE(writer_lock);

static void reset_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(reset_work, reset_work_handler);

static void reset_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)sys_cache_instr_invd_all();
	LOG_INF("resetting after UF2 update");
	sys_reboot(SYS_REBOOT_WARM);
}

int zuf2_writer_init(void)
{
	int ret;

	if (app_area != NULL) {
		return 0;
	}

	ret = flash_area_open(ZUF2_APP_PARTITION_ID, &app_area);
	if (ret != 0) {
		LOG_ERR("failed to open app flash area: %d", ret);
		return ret;
	}

	LOG_INF("app partition offset=0x%08x address=0x%08x size=%u",
		(uint32_t)app_area->fa_off, (uint32_t)ZUF2_APP_PARTITION_ADDRESS,
		(uint32_t)app_area->fa_size);

	return 0;
}

int zuf2_writer_read_app(uint32_t offset, void *buf, size_t len)
{
	int ret;

	ret = zuf2_writer_init();
	if (ret != 0) {
		return ret;
	}

	return flash_area_read(app_area, offset, buf, len);
}

static void state_begin(uint32_t num_blocks)
{
	memset(&state, 0, sizeof(state));
	state.active = true;
	state.num_blocks = num_blocks;
}

static int prepare_flash(void)
{
	int ret;

	if (state.flash_prepared) {
		return 0;
	}

	if (IS_ENABLED(CONFIG_FLASH_HAS_EXPLICIT_ERASE)) {
		LOG_INF("erasing app partition");
		ret = flash_area_flatten(app_area, 0, app_area->fa_size);
		if (ret != 0) {
			LOG_ERR("failed to erase app partition: %d", ret);
			return ret;
		}
	}

	state.flash_prepared = true;
	return 0;
}

static void mark_block_written(uint32_t block_no)
{
	uint32_t pos = block_no / 8U;
	uint8_t mask = BIT(block_no % 8U);

	if (block_no >= ZUF2_TRACKED_BLOCKS) {
		return;
	}

	if ((state.written_mask[pos] & mask) == 0U) {
		state.written_mask[pos] |= mask;
		state.num_written++;
	}
}

static void check_complete(void)
{
	if (state.complete || state.num_blocks == 0U ||
	    state.num_blocks > ZUF2_TRACKED_BLOCKS) {
		return;
	}

	if (state.num_written >= state.num_blocks) {
		state.complete = true;
		LOG_INF("UF2 write complete: %u blocks", state.num_written);
		(void)k_work_reschedule(&reset_work, K_MSEC(CONFIG_ZUF2_RESET_DELAY_MS));
	}
}

static bool uf2_magic_ok(const uint8_t sector[UF2_BLOCK_SIZE])
{
	return sys_get_le32(&sector[0]) == UF2_MAGIC_START0 &&
	       sys_get_le32(&sector[4]) == UF2_MAGIC_START1 &&
	       sys_get_le32(&sector[508]) == UF2_MAGIC_END;
}

int zuf2_writer_process_sector(const uint8_t sector[512])
{
	uint32_t flags;
	uint32_t target_addr;
	uint32_t payload_size;
	uint32_t block_no;
	uint32_t num_blocks;
	uint32_t family_id;
	uint32_t app_base = ZUF2_APP_PARTITION_ADDRESS;
	uint32_t app_size = ZUF2_APP_PARTITION_SIZE;
	uint32_t offset;
	uint32_t align;
	int ret;

	if (!uf2_magic_ok(sector)) {
		return 0;
	}

	flags = sys_get_le32(&sector[8]);
	target_addr = sys_get_le32(&sector[12]);
	payload_size = sys_get_le32(&sector[16]);
	block_no = sys_get_le32(&sector[20]);
	num_blocks = sys_get_le32(&sector[24]);
	family_id = sys_get_le32(&sector[28]);

	if ((flags & UF2_FLAG_FILE_CONTAINER) != 0U) {
		LOG_WRN("ignoring UF2 file-container block");
		return 0;
	}

	if (payload_size > UF2_PAYLOAD_MAX || num_blocks == 0U || block_no >= num_blocks) {
		LOG_ERR("invalid UF2 block metadata");
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_ZUF2_REQUIRE_FAMILY_ID)) {
		if ((flags & UF2_FLAG_FAMILY_ID) == 0U) {
			LOG_WRN("ignoring UF2 block without family ID");
			return 0;
		}

		if (family_id != CONFIG_ZUF2_FAMILY_ID) {
			LOG_WRN("ignoring UF2 family 0x%08x", family_id);
			return 0;
		}
	}

	k_mutex_lock(&writer_lock, K_FOREVER);

	ret = zuf2_writer_init();
	if (ret != 0) {
		goto out;
	}

	if (!state.active || state.num_blocks != num_blocks) {
		if (num_blocks > ZUF2_TRACKED_BLOCKS) {
			LOG_WRN("UF2 has %u blocks; tracking limit is %u",
				num_blocks, (uint32_t)ZUF2_TRACKED_BLOCKS);
		}
		state_begin(num_blocks);
	}

	if ((flags & UF2_FLAG_NOFLASH) != 0U) {
		mark_block_written(block_no);
		check_complete();
		ret = 0;
		goto out;
	}

	if (target_addr < app_base || payload_size > app_size ||
	    target_addr > (app_base + app_size - payload_size)) {
		LOG_ERR("UF2 target 0x%08x size %u outside app partition",
			target_addr, payload_size);
		ret = -EINVAL;
		goto out;
	}

	offset = target_addr - app_base;
	align = flash_area_align(app_area);
	if (((offset % align) != 0U) || ((payload_size % align) != 0U)) {
		LOG_ERR("UF2 write is not aligned to %u bytes", align);
		ret = -EINVAL;
		goto out;
	}

	ret = prepare_flash();
	if (ret != 0) {
		goto out;
	}

	ret = flash_area_write(app_area, offset, &sector[32], payload_size);
	if (ret != 0) {
		LOG_ERR("flash write failed at 0x%08x: %d", target_addr, ret);
		goto out;
	}

	mark_block_written(block_no);
	check_complete();

out:
	k_mutex_unlock(&writer_lock);
	return ret;
}
