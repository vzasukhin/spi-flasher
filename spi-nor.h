#ifndef _SPI_NOR_H
#define _SPI_NOR_H

#include "usb.h"

struct spi_flash {
	char *name;
	bool (*fill_id_func)(struct spi_flash *flash, uint8_t *ids);
	uint32_t size;
	uint32_t erase_block;
	uint32_t page;
	uint32_t id_len;
	uint8_t ids[16];
};

typedef void (* cb_progress)(uint32_t);

struct spi_flash *spi_nor_init(struct usb_device *device);
bool spi_nor_read(struct usb_device *device, struct spi_flash *flash,
		  uint32_t offset, uint32_t len, uint8_t *buf, unsigned buf_len,
		  int fd, cb_progress progress);
bool spi_nor_erase_block(struct usb_device *device, struct spi_flash *flash, uint32_t offset);
bool spi_nor_program_page_single(struct usb_device *device, struct spi_flash *flash,
				 uint32_t offset, uint8_t *buf);

#endif
