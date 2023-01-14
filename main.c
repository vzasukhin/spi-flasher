#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "usb.h"
#include "spi.h"
#include "spi-nor.h"


enum command {
	COMMAND_READ,
	COMMAND_UNKNOWN  // must be last
};

struct arg {
	char *fname;
	uint32_t offset;
	uint32_t size;
	enum command command;
};

void progress(uint32_t pos)
{
	printf("pos: %u\n", pos);
}

void show_help(void)
{
	printf("SPI Flasher can work with CH341 based modules\n" \
	       "Usage: spi-flasher [-o] [-s] COMMAND [FILE]\n" \
	       " COMMAND can be one of\n" \
	       "   read - read data from memory\n" \
	       " FILE is file name to save read from flash data or to get data for writing to flash\n" \
	       "\n" \
	       " -h, --help - show this message\n" \
	       " -o, --offset OFFSET - offset in bytes to read, write or erase\n" \
	       " -s, --size SIZE - size of data to read, write or erase\n" \
	       "\n" \
	       "Example:\n" \
	       " spi-flasher read -s 1024 file.dat\n" \
	);
}

int parse_arg(int argc, char *argv[], struct arg *arg)
{
	const struct option options[] = {
		{ "--help", no_argument, NULL, 'h' },
		{ "--offset", required_argument, NULL, 'o' },
		{ "--size", required_argument, NULL, 's' },
		{ NULL, 0, NULL, 0 },
	};
	int32_t optidx = 0;
	int c;
	int pos = 0;

	arg->offset = 0;
	arg->size = 0xffffffff;
	while ((c = getopt_long(argc, argv, "ho:s:", options, &optidx)) != -1) {
		switch (c) {
		case 'h':
			show_help();
			return 0;
			break;
		case 'o':
			arg->offset = strtoul(optarg, NULL, 0);
			break;
		case 's':
			arg->size = strtoul(optarg, NULL, 0);
			break;
		default:
			printf("\n");
			show_help();
			return -1;
		}
	}
	if (argc == optind) {
		fprintf(stderr, "command is not specified\n");
		printf("\n");
		show_help();
		return -1;
	}

	arg->fname = NULL;
	while (argc > optind) {
		if (pos == 0) {
			if (!strcmp(argv[optind], "read")) {
				arg->command = COMMAND_READ;
				if (argc - optind != 2) {
					fprintf(stderr, "file is not specified\n");
					return -1;
				}
			} else {
				fprintf(stderr, "unknown command '%s'\n", argv[optind]);
				return -1;
			}
		} else if (pos == 1) {
			arg->fname = (char *)malloc(strlen(argv[optind]) + 1);
			strcpy(arg->fname, argv[optind]);
		}
		optind++;
		pos++;
	}

	return 1;
}

int main(int argc, char *argv[])
{
	struct usb_device dev;
	struct spi_flash *flash;
	struct arg arg;
	int parse_res;
	int fd;
	bool res;

	parse_res = parse_arg(argc, argv, &arg);
	if (parse_res <= 0)
		return -parse_res;

	if (!usb_open(&dev))
		error(1, errno, "Can not open USB device");

	if (!spi_set_speed(&dev, false))
		error(1, errno, "Can not set speed");

	flash = spi_nor_init(&dev);
	printf("Flash: %s\n", flash->name);
	printf("Size: %u\n", flash->size);
	printf("EraseBlock: %u\n", flash->erase_block);
	printf("Page: %u\n", flash->page);
	printf("ID: ");
	for (int i = 0; i < flash->id_len; i++) {
		printf(" %02x", flash->ids[i]);
	}
	printf("\n\n");

	switch (arg.command) {
	case COMMAND_READ:
		fd = open(arg.fname, O_CREAT | O_WRONLY,
			  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
		if (fd == -1) {
			error(0, errno, "Can not open file '%s'", arg.fname);
			usb_close(&dev);
			return 1;
		}
		arg.size = min(arg.size, flash->size - arg.offset);
		printf("Read %u bytes from offset %u\n", arg.size, arg.offset);
		res = spi_nor_read(&dev, flash, arg.offset, arg.size, NULL, 0, fd, NULL);
		if (close(fd) || !res) {
			error(0, errno, "Can not read or save data");
			usb_close(&dev);
			return 1;
		}
		printf("Read complete\n");
		break;
	default:
		break;
	}

	usb_close(&dev);

	return 0;
}
