#ifndef _USB_H
#define _USB_H

#include <stdbool.h>
#include <stdint.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

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
void usb_handle_events(void);

#endif
