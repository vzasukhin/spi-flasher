#include <stdbool.h>
#include <stdint.h>

#include <libusb-1.0/libusb.h>

#include "usb.h"


bool usb_open(struct usb_device *device)
{
	struct libusb_device_handle *handle;

	if (!device || libusb_init(NULL) < 0)
		return false;

	handle = libusb_open_device_with_vid_pid(NULL, 0x1a86, 0x5512);
	if (!handle)
		return false;

	if (libusb_kernel_driver_active(handle, 0)) {
		device->driver_attach = true;
		if (libusb_detach_kernel_driver(handle, 0)) {
			libusb_close(handle);
			return false;
		}
	} else
		device->driver_attach = false;

	if (libusb_claim_interface(handle, 0)) {
		if (device->driver_attach)
			libusb_attach_kernel_driver(handle, 0);

		libusb_close(handle);
	}
	device->handle = handle;

	return true;
}

void usb_close(struct usb_device *device)
{
	struct libusb_device_handle *handle;

	if (!device)
		return;

	handle = (struct libusb_device_handle *)device->handle;
	libusb_release_interface(handle, 0);
	if (device->driver_attach)
		libusb_attach_kernel_driver(handle, 0);

	libusb_close(handle);
	libusb_exit(NULL);
}

bool usb_read(struct usb_device *device, void *buf, int len)
{
	struct libusb_device_handle *handle;
	int transfered;
	int ret;

	if (!device)
		return false;

	handle = (struct libusb_device_handle *)device->handle;
	ret = libusb_bulk_transfer(handle, 0x82, (unsigned char *)buf, len, &transfered, 1000);

	return ret >= 0;
}

bool usb_write(struct usb_device *device, void *buf, int len)
{
	struct libusb_device_handle *handle;
	int transfered;
	int ret;

	if (!device)
		return false;

	handle = (struct libusb_device_handle *)device->handle;
	ret = libusb_bulk_transfer(handle, 0x2, (unsigned char *)buf, len, &transfered, 1000);

	return ret >= 0;
}
