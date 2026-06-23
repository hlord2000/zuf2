#ifndef ZUF2_UF2_WRITER_H_
#define ZUF2_UF2_WRITER_H_

#include <stddef.h>
#include <stdint.h>

int zuf2_writer_init(void);
int zuf2_writer_process_sector(const uint8_t sector[512]);
int zuf2_writer_read_app(uint32_t offset, void *buf, size_t len);

#endif
