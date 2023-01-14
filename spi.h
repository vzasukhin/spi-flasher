#ifndef _SPI_H
#define _SPI_H

#include <stdbool.h>
#include <stdint.h>

#include "usb.h"

enum spi_width {
	SINGLE,
	DUAL,
};

bool spi_set_speed(struct usb_device *device, bool double_speed);
bool spi_cs(struct usb_device *device, bool cs_assert);
bool spi_transfer_nocs(struct usb_device *device, uint8_t *data_out, uint8_t *data_in,
		       unsigned len);
bool spi_transfer(struct usb_device *device, uint8_t *data_out, uint8_t *data_in, unsigned len);

#endif
