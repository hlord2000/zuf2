#include "app_partition.h"
#include "uf2_disk.h"
#include "uf2_format.h"
#include "uf2_writer.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zuf2, CONFIG_ZUF2_LOG_LEVEL);

#define SECTOR_SIZE 512U
#define SECTORS_PER_CLUSTER 1U
#define RESERVED_SECTORS 1U
#define FAT_COPIES 2U
#define ROOT_DIR_ENTRIES 64U
#define ROOT_DIR_SECTORS ((ROOT_DIR_ENTRIES * 32U) / SECTOR_SIZE)
#define MEDIA_DESCRIPTOR 0xf8U
#define FAT_ENTRIES_PER_SECTOR (SECTOR_SIZE / 2U)
#define SECTORS_PER_FAT                                                                 \
	((CONFIG_ZUF2_DISK_SECTOR_COUNT + FAT_ENTRIES_PER_SECTOR - 1U) /               \
	 FAT_ENTRIES_PER_SECTOR)

#define FAT0_START RESERVED_SECTORS
#define FAT1_START (FAT0_START + SECTORS_PER_FAT)
#define ROOT_DIR_START (FAT1_START + SECTORS_PER_FAT)
#define DATA_START (ROOT_DIR_START + ROOT_DIR_SECTORS)
#define DATA_SECTORS (CONFIG_ZUF2_DISK_SECTOR_COUNT - DATA_START)
#define CLUSTER_COUNT DATA_SECTORS

#define INFO_CLUSTER 2U
#define INDEX_CLUSTER 3U
#define CURRENT_CLUSTER 4U
#define CURRENT_UF2_BLOCKS                                                               \
	((ZUF2_APP_PARTITION_SIZE + UF2_DEFAULT_PAYLOAD_SIZE - 1U) /                    \
	 UF2_DEFAULT_PAYLOAD_SIZE)
#define CURRENT_UF2_SIZE (CURRENT_UF2_BLOCKS * SECTOR_SIZE)
#define CURRENT_LAST_CLUSTER (CURRENT_CLUSTER + CURRENT_UF2_BLOCKS - 1U)

BUILD_ASSERT(SECTOR_SIZE == UF2_BLOCK_SIZE);
BUILD_ASSERT(ROOT_DIR_SECTORS != 0U);
BUILD_ASSERT(CLUSTER_COUNT >= 0x1015U && CLUSTER_COUNT < 0xffd5U,
	     "virtual disk must produce a conservative FAT16 cluster count");
BUILD_ASSERT((DATA_START + (CURRENT_LAST_CLUSTER - 2U)) < CONFIG_ZUF2_DISK_SECTOR_COUNT,
	     "CONFIG_ZUF2_DISK_SECTOR_COUNT is too small for CURRENT.UF2");

static void fill_spaces(uint8_t *dst, size_t len)
{
	memset(dst, ' ', len);
}

static void write_padded(uint8_t *dst, size_t len, const char *src)
{
	fill_spaces(dst, len);
	for (size_t i = 0; i < len && src[i] != '\0'; i++) {
		dst[i] = src[i];
	}
}

static void volume_label(uint8_t label[11])
{
	write_padded(label, 11, CONFIG_ZUF2_VOLUME_LABEL);
}

static void make_boot_sector(uint8_t sector[SECTOR_SIZE])
{
	uint8_t label[11];

	memset(sector, 0, SECTOR_SIZE);
	sector[0] = 0xeb;
	sector[1] = 0x3c;
	sector[2] = 0x90;
	memcpy(&sector[3], "ZUF2    ", 8);
	sys_put_le16(SECTOR_SIZE, &sector[11]);
	sector[13] = SECTORS_PER_CLUSTER;
	sys_put_le16(RESERVED_SECTORS, &sector[14]);
	sector[16] = FAT_COPIES;
	sys_put_le16(ROOT_DIR_ENTRIES, &sector[17]);
	if (CONFIG_ZUF2_DISK_SECTOR_COUNT <= 0xffff) {
		sys_put_le16(CONFIG_ZUF2_DISK_SECTOR_COUNT, &sector[19]);
	} else {
		sys_put_le32(CONFIG_ZUF2_DISK_SECTOR_COUNT, &sector[32]);
	}
	sector[21] = MEDIA_DESCRIPTOR;
	sys_put_le16(SECTORS_PER_FAT, &sector[22]);
	sys_put_le16(1, &sector[24]);
	sys_put_le16(1, &sector[26]);
	sector[36] = 0x80;
	sector[38] = 0x29;
	sys_put_le32(0x5a554632, &sector[39]);
	volume_label(label);
	memcpy(&sector[43], label, sizeof(label));
	memcpy(&sector[54], "FAT16   ", 8);
	sector[510] = 0x55;
	sector[511] = 0xaa;
}

static uint16_t fat_entry_value(uint32_t cluster)
{
	if (cluster == 0U) {
		return 0xff00U | MEDIA_DESCRIPTOR;
	}

	if (cluster == 1U || cluster == INFO_CLUSTER || cluster == INDEX_CLUSTER) {
		return 0xffffU;
	}

	if (cluster >= CURRENT_CLUSTER && cluster <= CURRENT_LAST_CLUSTER) {
		return cluster == CURRENT_LAST_CLUSTER ? 0xffffU : (uint16_t)(cluster + 1U);
	}

	return 0U;
}

static void make_fat_sector(uint32_t fat_sector, uint8_t sector[SECTOR_SIZE])
{
	memset(sector, 0, SECTOR_SIZE);

	for (uint32_t i = 0; i < FAT_ENTRIES_PER_SECTOR; i++) {
		uint32_t cluster = fat_sector * FAT_ENTRIES_PER_SECTOR + i;

		sys_put_le16(fat_entry_value(cluster), &sector[i * 2U]);
	}
}

static void make_dir_entry(uint8_t *entry, const char name[11], uint8_t attrs,
			   uint16_t cluster, uint32_t size)
{
	memcpy(&entry[0], name, 11);
	entry[11] = attrs;
	sys_put_le16(cluster, &entry[26]);
	sys_put_le32(size, &entry[28]);
}

static int info_file(uint8_t *dst, size_t len)
{
	int ret;

	ret = snprintf((char *)dst, len,
		       "UF2 Bootloader zuf2\r\n"
		       "Model: %s\r\n"
		       "Board-ID: %s\r\n"
		       "Family-ID: 0x%08x\r\n"
		       "App-Start: 0x%08x\r\n"
		       "App-Size: %u\r\n",
		       CONFIG_ZUF2_MODEL, CONFIG_ZUF2_BOARD_ID, CONFIG_ZUF2_FAMILY_ID,
		       (uint32_t)ZUF2_APP_PARTITION_ADDRESS,
		       (uint32_t)ZUF2_APP_PARTITION_SIZE);

	return ret < 0 || ret >= (int)len ? -ENOMEM : ret;
}

static int index_file(uint8_t *dst, size_t len)
{
	int ret;

	ret = snprintf((char *)dst, len,
		       "<!doctype html>\n"
		       "<html><head><meta http-equiv=\"refresh\" content=\"0;url=%s\">"
		       "</head><body><a href=\"%s\">zuf2</a></body></html>\n",
		       CONFIG_ZUF2_INDEX_URL, CONFIG_ZUF2_INDEX_URL);

	return ret < 0 || ret >= (int)len ? -ENOMEM : ret;
}

static void make_root_dir_sector(uint32_t root_sector, uint8_t sector[SECTOR_SIZE])
{
	uint8_t label[11];
	uint8_t tmp[SECTOR_SIZE];
	uint8_t *entry;

	memset(sector, 0, SECTOR_SIZE);

	if (root_sector != 0U) {
		return;
	}

	entry = sector;
	volume_label(label);
	make_dir_entry(entry, (const char *)label, 0x08, 0, 0);

	entry += 32;
	make_dir_entry(entry, "INFO_UF2TXT", 0x20, INFO_CLUSTER, 0);
	memset(tmp, 0, sizeof(tmp));
	(void)info_file(tmp, sizeof(tmp));
	sys_put_le32(strlen((const char *)tmp), &entry[28]);

	entry += 32;
	make_dir_entry(entry, "INDEX   HTM", 0x20, INDEX_CLUSTER, 0);
	memset(tmp, 0, sizeof(tmp));
	(void)index_file(tmp, sizeof(tmp));
	sys_put_le32(strlen((const char *)tmp), &entry[28]);

	entry += 32;
	make_dir_entry(entry, "CURRENT UF2", 0x20, CURRENT_CLUSTER, CURRENT_UF2_SIZE);
}

static void make_info_sector(uint8_t sector[SECTOR_SIZE])
{
	memset(sector, 0, SECTOR_SIZE);
	(void)info_file(sector, SECTOR_SIZE);
}

static void make_index_sector(uint8_t sector[SECTOR_SIZE])
{
	memset(sector, 0, SECTOR_SIZE);
	(void)index_file(sector, SECTOR_SIZE);
}

static void make_current_uf2_sector(uint32_t uf2_block_no, uint8_t sector[SECTOR_SIZE])
{
	uint32_t app_offset = uf2_block_no * UF2_DEFAULT_PAYLOAD_SIZE;
	uint32_t remaining = ZUF2_APP_PARTITION_SIZE - app_offset;
	uint32_t payload = MIN(remaining, UF2_DEFAULT_PAYLOAD_SIZE);

	memset(sector, 0, SECTOR_SIZE);
	sys_put_le32(UF2_MAGIC_START0, &sector[0]);
	sys_put_le32(UF2_MAGIC_START1, &sector[4]);
	sys_put_le32(UF2_FLAG_FAMILY_ID, &sector[8]);
	sys_put_le32(ZUF2_APP_PARTITION_ADDRESS + app_offset, &sector[12]);
	sys_put_le32(payload, &sector[16]);
	sys_put_le32(uf2_block_no, &sector[20]);
	sys_put_le32(CURRENT_UF2_BLOCKS, &sector[24]);
	sys_put_le32(CONFIG_ZUF2_FAMILY_ID, &sector[28]);
	if (payload > 0U) {
		if (zuf2_writer_read_app(app_offset, &sector[32], payload) != 0) {
			memset(&sector[32], 0xff, payload);
		}
	}
	sys_put_le32(UF2_MAGIC_END, &sector[508]);
}

static void read_sector(uint32_t sector_no, uint8_t sector[SECTOR_SIZE])
{
	if (sector_no == 0U) {
		make_boot_sector(sector);
	} else if (sector_no < ROOT_DIR_START) {
		uint32_t fat_sector = sector_no - FAT0_START;

		if (fat_sector >= SECTORS_PER_FAT) {
			fat_sector -= SECTORS_PER_FAT;
		}
		make_fat_sector(fat_sector, sector);
	} else if (sector_no < DATA_START) {
		make_root_dir_sector(sector_no - ROOT_DIR_START, sector);
	} else {
		uint32_t cluster = (sector_no - DATA_START) + 2U;

		if (cluster == INFO_CLUSTER) {
			make_info_sector(sector);
		} else if (cluster == INDEX_CLUSTER) {
			make_index_sector(sector);
		} else if (cluster >= CURRENT_CLUSTER && cluster <= CURRENT_LAST_CLUSTER) {
			make_current_uf2_sector(cluster - CURRENT_CLUSTER, sector);
		} else {
			memset(sector, 0, SECTOR_SIZE);
		}
	}
}

static int uf2_disk_init(struct disk_info *disk)
{
	ARG_UNUSED(disk);

	return zuf2_writer_init();
}

static int uf2_disk_status(struct disk_info *disk)
{
	ARG_UNUSED(disk);

	return DISK_STATUS_OK;
}

static int uf2_disk_read(struct disk_info *disk, uint8_t *data_buf,
			 uint32_t start_sector, uint32_t num_sector)
{
	ARG_UNUSED(disk);

	if (start_sector + num_sector > CONFIG_ZUF2_DISK_SECTOR_COUNT) {
		return -EINVAL;
	}

	for (uint32_t i = 0; i < num_sector; i++) {
		read_sector(start_sector + i, &data_buf[i * SECTOR_SIZE]);
	}

	return 0;
}

static int uf2_disk_write(struct disk_info *disk, const uint8_t *data_buf,
			  uint32_t start_sector, uint32_t num_sector)
{
	int ret;

	ARG_UNUSED(disk);
	ARG_UNUSED(start_sector);

	for (uint32_t i = 0; i < num_sector; i++) {
		ret = zuf2_writer_process_sector(&data_buf[i * SECTOR_SIZE]);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int uf2_disk_erase(struct disk_info *disk, uint32_t start_sector,
			  uint32_t num_sector)
{
	ARG_UNUSED(disk);
	ARG_UNUSED(start_sector);
	ARG_UNUSED(num_sector);

	return 0;
}

static int uf2_disk_ioctl(struct disk_info *disk, uint8_t cmd, void *buff)
{
	ARG_UNUSED(disk);

	switch (cmd) {
	case DISK_IOCTL_CTRL_INIT:
	case DISK_IOCTL_CTRL_SYNC:
	case DISK_IOCTL_CTRL_DEINIT:
		return 0;
	case DISK_IOCTL_GET_SECTOR_COUNT:
		*(uint32_t *)buff = CONFIG_ZUF2_DISK_SECTOR_COUNT;
		return 0;
	case DISK_IOCTL_GET_SECTOR_SIZE:
		*(uint32_t *)buff = SECTOR_SIZE;
		return 0;
	case DISK_IOCTL_GET_ERASE_BLOCK_SZ:
		*(uint32_t *)buff = 1;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct disk_operations uf2_disk_ops = {
	.init = uf2_disk_init,
	.status = uf2_disk_status,
	.read = uf2_disk_read,
	.write = uf2_disk_write,
	.erase = uf2_disk_erase,
	.ioctl = uf2_disk_ioctl,
};

static struct disk_info uf2_disk = {
	.name = ZUF2_DISK_NAME,
	.ops = &uf2_disk_ops,
};

static int uf2_disk_register(void)
{
	int ret;

	ret = disk_access_register(&uf2_disk);
	if (ret != 0) {
		LOG_ERR("failed to register UF2 disk: %d", ret);
	}

	return ret;
}

SYS_INIT(uf2_disk_register, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
