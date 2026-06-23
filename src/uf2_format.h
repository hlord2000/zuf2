#ifndef ZUF2_UF2_FORMAT_H_
#define ZUF2_UF2_FORMAT_H_

#include <stdint.h>
#include <zephyr/toolchain.h>

#define UF2_BLOCK_SIZE 512U
#define UF2_PAYLOAD_MAX 476U
#define UF2_DEFAULT_PAYLOAD_SIZE 256U

#define UF2_MAGIC_START0 0x0A324655UL
#define UF2_MAGIC_START1 0x9E5D5157UL
#define UF2_MAGIC_END 0x0AB16F30UL

#define UF2_FLAG_NOFLASH 0x00000001UL
#define UF2_FLAG_FILE_CONTAINER 0x00001000UL
#define UF2_FLAG_FAMILY_ID 0x00002000UL
#define UF2_FLAG_MD5_CHECKSUM 0x00004000UL
#define UF2_FLAG_EXTENSION_TAGS 0x00008000UL

struct uf2_block {
	uint32_t magic_start0;
	uint32_t magic_start1;
	uint32_t flags;
	uint32_t target_addr;
	uint32_t payload_size;
	uint32_t block_no;
	uint32_t num_blocks;
	uint32_t family_id;
	uint8_t data[UF2_PAYLOAD_MAX];
	uint32_t magic_end;
} __packed;

#endif
