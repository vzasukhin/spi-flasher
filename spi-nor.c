#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "spi-nor.h"
#include "spi.h"
#include "usb.h"


#define CMD_READ_ID		0x9f
#define CMD_READ_STATUS		0x5

#define CMD_READ		0x3
#define CMD_FAST_READ		0xb
#define CMD_READ_4BYTE		0x13
#define CMD_FAST_READ_4BYTE	0xc

#define CMD_WRITE_ENABLE	0x6
#define CMD_WRITE_DISABLE	0x4

#define CMD_PAGE_PROGRAM	0x2
#define CMD_PAGE_PROGRAM_4BYTE	0x12
#define CMD_ERASE_SECTOR	0xd8
#define CMD_ERASE_SECTOR_4BYTE	0xdc
#define CMD_ERASE_4KSECTOR	0x20
#define CMD_ERASE_4KSECTOR_4BYTE 0x21


static uint32_t get_size_by_id2(uint8_t id2)
{
	if (id2 < 0x10 || (id2 > 0x19 && id2 < 0x20) || id2 > 0x25)
		return 0;

	if (id2 >= 0x20)
		id2 -= 6;

	return 1 << id2;
}

static bool spi_nor_id_fill_mt25q(struct spi_flash *flash, uint8_t *ids)
{
	uint32_t mark;

	if (ids[1] == 0xba)
		flash->name[5] = 'L';
	else if (ids[1] == 0xbb)
		flash->name[5] = 'U';

	mark = get_size_by_id2(ids[2]) >> 17;
	if (mark >= 1024) {
		flash->name[6] = '0';
		flash->name[7] = mark / 1024 + '0';
		flash->name[8] = 'G';
	} else {
		flash->name[6] = mark / 100 + '0';
		flash->name[7] = (mark / 10) % 10 + '0';
		flash->name[8] = mark % 10 + '0';
	}

	return true;
}

static bool spi_nor_id_fill_w25q(struct spi_flash *flash, uint8_t *ids)
{
	uint32_t mark;
	int len;
	char *endian = "";
	bool found = true;

	if (ids[1] == 0x40) {
		switch (ids[2]) {
		case 0x16:
			endian = "BV";
			break;
		case 0x17:
			endian = "FV";
			break;
		case 0x18:
			endian = "JV-IN/IQ/JQ";
			break;
		default:
			found = false;
			break;
		}
	} else if (ids[1] == 0x60 && ids[2] == 0x18)
		endian = "FW";
	else if (ids[1] == 0x70 && ids[2] == 0x18)
		endian = "JV-IM/JM";
	else
		found = false;

	mark = get_size_by_id2(ids[2]) >> 17;

	len = strlen(endian) + 10;
	flash->name = (char *)realloc(flash->name, len);
	snprintf(flash->name, len, "W25Q%u%s", mark, endian);

	return found;
}

static bool spi_nor_id_fill_m25p(struct spi_flash *flash, uint8_t *ids)
{
	uint32_t mark;

	mark = get_size_by_id2(ids[2]) >> 17;
	if (mark > 512)
		return false;

	if (mark <= 8)
		mark *= 10;

	flash->name = (char *)realloc(flash->name, 8);
	snprintf(flash->name, 8, "M25P%u", mark);

	return true;
}

static bool spi_nor_id_fill_s25fl(struct spi_flash *flash, uint8_t *ids)
{
	uint32_t mark;
	char endian;
	char medium;
	unsigned family = 25;

	if (ids[1] == 79)
		family = 79;

	mark = get_size_by_id2(ids[2]) >> 17;
	if (mark <= 8)
		mark *= 10;

	if (ids[4] == 0)
		flash->erase_block = 256 * KiB;
	else if (ids[4] == 1)
		flash->erase_block = 64 * KiB;

	if (family == 79) {
		flash->erase_block *= 2;
		flash->page = 512;
	} else {
		flash->page = 256;
	}

	switch (ids[5]) {
	case 0x80:
		medium = 'L';
		endian = 'S';
		break;
	case 0x81:
		medium = 'S';
		endian = 'S';
		break;
	default:
		medium = 'L';
		endian = 'P';
		break;
	}

	flash->name = (char *)realloc(flash->name, 10);
	if (mark <= 512)
		snprintf(flash->name, 10, "S%02uF%c%u%c", family, medium, mark, endian);
	else
		snprintf(flash->name, 10, "S%02uGF%c%u%c", family, medium, mark / 1024, endian);

	return true;
}

struct spi_flash spi_flashes[] = {
	{
		.name = "M25P",
		.erase_block = 64 * KiB,
		.page = 256,
		.id_len = 1,
		.ids = { 0x20 },
		.fill_id_func = spi_nor_id_fill_m25p,
	},
	{
		.name = "S25F",
		.id_len = 1,
		.ids = { 0x1 },
		.fill_id_func = spi_nor_id_fill_s25fl,
	},
	{
		.name = "W25Q",
		.erase_block = 64 * KiB,
		.page = 256,
		.id_len = 1,
		.ids = { 0xEF },
		.fill_id_func = spi_nor_id_fill_w25q,
	},
	{
		.name = "MT25Qxxxx",
		.erase_block = 64 * KiB,
		.page = 256,
		.id_len = 1,
		.ids = { 0x20 },
		.fill_id_func = spi_nor_id_fill_mt25q,
	},
};

// this variable will be fill and return by spi_nor_init()
static struct spi_flash flash_id;


struct spi_flash *spi_nor_init(struct usb_device *device)
{
	uint8_t buf_out[sizeof(spi_flashes[0].ids) + 1];
	uint8_t buf_in[sizeof(spi_flashes[0].ids) + 1];
	bool found = false;

	buf_out[0] = CMD_READ_ID;
	if (!spi_transfer(device, buf_out, buf_in, sizeof(buf_out)))
		return false;

	// free memory that was allocated by strdup() in previous call of spi_nor_init()
	if (flash_id.name)
		free(flash_id.name);

	flash_id.name = "Unknown";
	for (int i = 0; i < ARRAY_SIZE(spi_flashes); i++) {
		if (!memcmp(spi_flashes[i].ids, buf_in + 1, spi_flashes[i].id_len)) {
			memcpy(&flash_id, &spi_flashes[i], sizeof(flash_id));
			flash_id.name = strdup(spi_flashes[i].name);
			if (spi_flashes[i].fill_id_func) {
				found = spi_flashes[i].fill_id_func(&flash_id, buf_in + 1);
				if (found)
					break;

				free(flash_id.name);  // was allocated by strdup()
			}
		}
	}
	flash_id.id_len = sizeof(flash_id.ids);
	memcpy(flash_id.ids, buf_in + 1, sizeof(flash_id.ids));
	if (!flash_id.size)
		flash_id.size = get_size_by_id2(buf_in[3]);

	return &flash_id;
}

static bool spi_nor_cmd_send(struct usb_device *device, uint8_t cmd, uint8_t *data,
			     unsigned data_len)
{
	uint8_t buf[data_len + 1];

	buf[0] = cmd;
	memcpy(buf + 1, data, data_len);
	return spi_transfer(device, buf, NULL, data_len + 1);
}

static bool spi_nor_cmd_recv(struct usb_device *device, uint8_t cmd, uint8_t *data,
			     unsigned data_len)
{
	uint8_t buf[data_len + 1];
	bool ret;

	buf[0] = cmd;
	ret = spi_transfer(device, buf, buf, data_len + 1);
	memcpy(data, buf + 1, data_len);

	return ret;
}

static bool spi_nor_send_cmd_addr(struct usb_device *device, struct spi_flash *flash,
				  uint8_t cmd3, uint8_t cmd4, uint32_t addr, unsigned dummy_count)
{
	uint8_t cmd;
	unsigned addr_count;
	uint8_t buf[dummy_count + 5];

	if (flash->size > 16 * MiB) {
		addr_count = 4;
		cmd = cmd4;
	} else {
		addr_count = 3;
		cmd = cmd3;
	}

	buf[0] = cmd;
	for (int i = 0; i < addr_count; i++)
		buf[i + 1] = (addr >> ((addr_count - i - 1) * 8)) & 0xff;

	memset(buf + 1 + addr_count, 0xff, dummy_count);

	return spi_transfer_nocs(device, buf, NULL, 1 + addr_count + dummy_count);
}

bool spi_nor_read(struct usb_device *device, struct spi_flash *flash,
		  uint32_t offset, uint32_t len, uint8_t *buf, int fd, cb_progress progress)
{
	uint32_t pos = 0;
	uint32_t remain = len;

	if (!spi_cs(device, true))
		return false;

	if (!spi_nor_send_cmd_addr(device, flash, CMD_FAST_READ, CMD_FAST_READ_4BYTE, offset, 1))
		return false;

	if (buf) {
		if (!spi_transfer_nocs(device, NULL, buf, len))
			return false;
	} else {
		uint8_t local_buf[16 * KiB];

		while (remain) {
			uint32_t block_len = min(remain, 16 * KiB);

			if (progress) {
				progress(pos, len);
				pos += block_len;
			}

			remain -= block_len;
			if (!spi_transfer_nocs(device, NULL, local_buf, block_len))
				return false;

			if (write(fd, local_buf, block_len) == -1) {
				spi_cs(device, false);
				return false;
			}
		}
	}

	return spi_cs(device, false);
}

bool spi_nor_erase_block(struct usb_device *device, struct spi_flash *flash, uint32_t offset)
{
	uint8_t status_reg;

	if (!spi_nor_cmd_send(device, CMD_WRITE_ENABLE, NULL, 0))
		return false;

	if (!spi_cs(device, true))
		return false;

	if (!spi_nor_send_cmd_addr(device, flash, CMD_ERASE_SECTOR, CMD_ERASE_SECTOR_4BYTE,
				   offset, 0))
		return false;

	if (!spi_cs(device, false))
		return false;

	do {
		if (!spi_nor_cmd_recv(device, CMD_READ_STATUS, &status_reg, 1))
			return false;
	} while (status_reg & 0x1);

	return spi_nor_cmd_send(device, CMD_WRITE_DISABLE, NULL, 0);
}

bool spi_nor_erase(struct usb_device *device, struct spi_flash *flash, uint32_t offset,
		   uint32_t len, cb_progress progress)
{
	uint32_t pos = 0;

	while (pos < len) {
		if (progress) {
			progress(pos, len);
		}

		if (!spi_nor_erase_block(device, flash, offset + pos))
			return false;

		pos += flash->erase_block;
	}

	return true;
}

bool spi_nor_erase_smart(struct usb_device *device, struct spi_flash *flash, uint32_t offset,
			 uint32_t len, cb_progress progress)
{
	uint8_t *buf_pre = NULL, *buf_post = NULL;
	uint32_t size_pre = offset % flash->erase_block;
	uint32_t size_post = flash->erase_block - ((offset + len) % flash->erase_block);
	bool res;

	if (size_post == flash->erase_block)
		size_post = 0;

	if (size_pre) {
		buf_pre = (uint8_t *)malloc(size_pre);
		if (!spi_nor_read(device, flash, offset - size_pre, size_pre, buf_pre, 0, NULL))
			return false;
	}
	if (size_post) {
		buf_post = (uint8_t *)malloc(size_post);
		if (!spi_nor_read(device, flash, offset + len, size_post, buf_post, 0, NULL)) {
			free(buf_post);
			if (size_pre)
				free(buf_pre);

			return false;
		}
	}
	if (!spi_nor_erase(device, flash, offset - size_pre, len + size_pre + size_post, progress)) {
		if (size_pre)
			free(buf_pre);
		if (size_post)
			free(buf_post);

		return false;
	}

	if (size_pre) {
		res = spi_nor_program(device, flash, offset - size_pre, size_pre, buf_pre, 0, NULL);
		free(buf_pre);
		if (!res)
			return false;
	}

	if (size_post) {
		res = spi_nor_program_smart(device, flash, offset + len, size_post, buf_post, 0, NULL);
		free(buf_post);
		return res;
	}

	return true;
}

bool spi_nor_program_page_single(struct usb_device *device, struct spi_flash *flash,
				 uint32_t offset, uint8_t *buf, uint32_t buf_len)
{
	uint8_t status_reg;

	buf_len = min(buf_len, flash->page);
	if (!spi_nor_cmd_send(device, CMD_WRITE_ENABLE, NULL, 0))
		return false;

	if (!spi_cs(device, true))
		return false;

	if (!spi_nor_send_cmd_addr(device, flash, CMD_PAGE_PROGRAM, CMD_PAGE_PROGRAM_4BYTE,
				   offset, 0))
		return false;

	if (!spi_transfer_nocs(device, buf, NULL, buf_len))
		return false;

	if (!spi_cs(device, false))
		return false;

	do {
		if (!spi_nor_cmd_recv(device, CMD_READ_STATUS, &status_reg, 1))
			return false;
	} while (status_reg & 0x1);

	return spi_nor_cmd_send(device, CMD_WRITE_DISABLE, NULL, 0);
}

bool spi_nor_program(struct usb_device *device, struct spi_flash *flash, uint32_t offset,
		     uint32_t len, uint8_t *buf, int fd, cb_progress progress)
{
	uint32_t block_len;
	uint32_t pos = 0;

	while (pos < len) {
		if (progress) {
			progress(pos, len);
		}

		block_len = min(len - pos, flash->page);
		if (buf) {
			if (!spi_nor_program_page_single(device, flash, offset + pos, buf + pos,
							 block_len))
				return false;
		} else {
			uint8_t local_buf[block_len];

			if (read(fd, local_buf, block_len) == -1)
				return false;

			if (!spi_nor_program_page_single(device, flash, offset + pos, local_buf,
							 block_len))
				return false;
		}
		pos += block_len;
	}

	return true;
}

bool spi_nor_program_smart(struct usb_device *device, struct spi_flash *flash, uint32_t offset,
			   uint32_t len, uint8_t *buf, int fd, cb_progress progress)
{
	uint32_t size_pre = offset % flash->page;

	if (size_pre) {
		uint8_t buf_pre[flash->page];
		uint32_t len_in_first_page = min(len, flash->page - size_pre);

		if (!spi_nor_read(device, flash, offset - size_pre, flash->page, buf_pre, 0, NULL))
			return false;

		if (buf)
			memcpy(buf_pre + size_pre, buf, len_in_first_page);
		else if (read(fd, buf_pre + size_pre, len_in_first_page) != len_in_first_page)
			return false;

		if (!spi_nor_program_page_single(device, flash, offset - size_pre, buf_pre, flash->page))
			return false;

		len -= len_in_first_page;
		offset += len_in_first_page;
		if (buf)
			buf += len_in_first_page;
	}

	return spi_nor_program(device, flash, offset, len, buf, fd, progress);
}

uint32_t spi_nor_calc_erase_size(struct spi_flash *flash, uint32_t offset, uint32_t len)
{
	return ((offset + len - 1) | (flash->erase_block - 1)) -
	       (offset & ~(flash->erase_block - 1)) + 1;
}
