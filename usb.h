#ifndef _USB_H
#define _USB_H

#include <stdbool.h>
#include <stdint.h>

#include "common.h"

struct usb_device {
	uint16_t vid;
	uint16_t pid;
	void *handle;
	bool driver_attach;
};

bool usb_open(struct usb_device *device);
void usb_close(struct usb_device *device);
bool usb_read(struct usb_device *device, void *buf, int len);
bool usb_write(struct usb_device *device, void *buf, int len);

#endif
