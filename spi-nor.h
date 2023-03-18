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

struct spi_flash *spi_nor_get_empty_flash(void);
struct spi_flash *spi_nor_init(struct usb_device *device);
bool spi_nor_read(struct usb_device *device, struct spi_flash *flash,
		  uint32_t offset, uint32_t len, uint8_t *buf, int fd, cb_progress progress);
bool spi_nor_erase_block(struct usb_device *device, struct spi_flash *flash, uint32_t offset);
bool spi_nor_erase(struct usb_device *device, struct spi_flash *flash, uint32_t offset,
		   uint32_t len, cb_progress progress);
bool spi_nor_erase_smart(struct usb_device *device, struct spi_flash *flash, uint32_t offset,
			 uint32_t len, cb_progress progress);
bool spi_nor_program_page_single(struct usb_device *device, struct spi_flash *flash,
				 uint32_t offset, uint8_t *buf, uint32_t buf_len);
bool spi_nor_program(struct usb_device *device, struct spi_flash *flash, uint32_t offset,
		     uint32_t len, uint32_t *flashed_size, uint8_t *buf, int fd, bool need_erase,
		     cb_progress progress, uint8_t **read_data, uint32_t *read_len,
		     uint32_t read_data_buf_size);
bool spi_nor_program_smart(struct usb_device *device, struct spi_flash *flash, uint32_t offset,
			   uint32_t len, uint32_t *flashed_size, uint8_t *buf, int fd,
			   bool need_erase, cb_progress progress, uint8_t **read_data,
			   uint32_t *read_len);
bool spi_nor_custom(struct usb_device *device, uint8_t *tx, uint32_t tx_len,
		    uint8_t *rx, uint32_t rx_len, bool duplex);
uint32_t spi_nor_calc_erase_size(struct spi_flash *flash, uint32_t offset, uint32_t len);

#endif
