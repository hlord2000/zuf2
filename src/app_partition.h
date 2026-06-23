#ifndef ZUF2_APP_PARTITION_H_
#define ZUF2_APP_PARTITION_H_

#include <zephyr/storage/flash_map.h>

#if PARTITION_EXISTS(slot0_partition)
#define ZUF2_APP_PARTITION_LABEL slot0_partition
#elif PARTITION_EXISTS(code_partition)
#define ZUF2_APP_PARTITION_LABEL code_partition
#else
#error "zuf2 requires a devicetree slot0_partition or code_partition for the application"
#endif

#define ZUF2_APP_PARTITION_ID PARTITION_ID(ZUF2_APP_PARTITION_LABEL)
#define ZUF2_APP_PARTITION_OFFSET PARTITION_OFFSET(ZUF2_APP_PARTITION_LABEL)
#define ZUF2_APP_PARTITION_ADDRESS PARTITION_ADDRESS(ZUF2_APP_PARTITION_LABEL)
#define ZUF2_APP_PARTITION_SIZE PARTITION_SIZE(ZUF2_APP_PARTITION_LABEL)

#endif
