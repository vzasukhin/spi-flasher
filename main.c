#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "spi.h"
#include "spi-nor.h"
#include "usb.h"


enum command {
	COMMAND_READ,
	COMMAND_FLASH,
	COMMAND_ERASE,
	COMMAND_UNKNOWN  // must be last
};

struct arg {
	char *fname;
	uint32_t offset;
	uint32_t size;
	uint32_t flash_size;
	uint32_t flash_eraseblock;
	uint32_t flash_page;
	enum command command;
};

struct multiplier {
	char *name;
	long mul;
};

void progress(uint32_t pos)
{
	printf("pos: %u\n", pos);
}

void show_help(void)
{
	printf("SPI Flasher can work with CH341 converter\n" \
	       "Usage: spi-flasher [options] COMMAND [FILE]\n" \
	       " COMMAND can be one of\n" \
	       "   read  - read data from memory. Must be specified file to save data\n" \
	       "   flash - write data to memory. Must be specified file to get data\n" \
	       "   erase - erase data on memory\n" \
	       " FILE is file name to save read from flash data or to get data for writing to flash\n" \
	       "\n" \
	       " -h, --help           - show this message\n" \
	       " -o, --offset OFFSET  - offset of SPI memory to read, flash or erase (default: 0)\n" \
	       " -s, --size SIZE      - maximum size of data to read, flash or erase. If not specified,\n" \
	       "                        then will try to read/erase all contains of memory.\n" \
	       "                        For flash command will write not more than source file size\n" \
	       " --flash-size SIZE    - override size of memory\n" \
	       " --flash-eraseblock SIZE - override size of erase block\n" \
	       " --flash-page SIZE    - override size of page\n" \
	       "\n" \
	       "Example:\n" \
	       " spi-flasher read -s 1024 file.dat\n" \
	       " spi-flasher flash file.dat\n" \
	);
}

void print_size(uint32_t value, bool eol)
{
	static const char *suffixes[] = {"", "KiB", "MiB", "GiB"};
	int idx = 0;

	if (value) {
		for (int i = 1; i < ARRAY_SIZE(suffixes); i++) {
			if (!(value % 1024)) {
				value /= 1024;
				idx = i;
			} else
				break;
		}
	}
	printf("%u%s%s", value, suffixes[idx], eol ? "\n" : "");
}

bool parse_size(char *s, uint32_t *value)
{
	static const struct multiplier multipliers[] = {
		{ "B", 1 },
		{ "K", 1024 },
		{ "KiB", 1024 },
		{ "M", 1024 * 1024 },
		{ "MiB", 1024 * 1024 },
		{ "G", 1024 * 1024 * 1024 },
		{ "GiB", 1024 * 1024 * 1024 },
		{ "kB", 1000 },
		{ "MB", 1000 * 1000 },
		{ "GB", 1000 * 1000 * 1000 },
	};
	char *endptr;
	long long val = strtoll(s, &endptr, 0);

	if (*endptr) {
		long mul = 0;
		for (int i = 0; i < ARRAY_SIZE(multipliers); i++) {
			if (!strcmp(multipliers[i].name, endptr)) {
				mul = multipliers[i].mul;
				break;
			}
		}
		if (!mul) {
			fprintf(stderr, "can not parse '%s'\n", s);
			return false;
		}
		val *= mul;
	}
	if (val < 0 || val > 0xffffffff) {
		fprintf(stderr, "out of range '%s' (%lld)\n", s, val);
		return false;
	}
	*value = (uint32_t)val;

	return true;
}

int parse_arg(int argc, char *argv[], struct arg *arg)
{
	const struct option options[] = {
		{ "flash-size", required_argument, NULL, 0 },
		{ "flash-eraseblock", required_argument, NULL, 0 },
		{ "flash-page", required_argument, NULL, 0 },
		{ "help", no_argument, NULL, 'h' },
		{ "offset", required_argument, NULL, 'o' },
		{ "size", required_argument, NULL, 's' },
		{ NULL, 0, NULL, 0 },
	};
	int32_t optidx = 0;
	int c;
	int pos = 0;

	memset(arg, 0, sizeof(*arg));
	arg->size = 0xffffffff;
	while ((c = getopt_long(argc, argv, "ho:s:", options, &optidx)) != -1) {
		switch (c) {
		case 0:
			switch (optidx) {
			case 0:
				if (!parse_size(optarg, &arg->flash_size))
					return -1;
				break;
			case 1:
				if (!parse_size(optarg, &arg->flash_eraseblock))
					return -1;
				break;
			case 2:
				if (!parse_size(optarg, &arg->flash_page))
					return -1;
				break;
			default:
				break;
			}
			break;
		case 'h':
			show_help();
			return 0;
			break;
		case 'o':
			if (!parse_size(optarg, &arg->offset))
				return -1;
			break;
		case 's':
			if (!parse_size(optarg, &arg->size))
				return -1;
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
			int arguments_count;

			if (!strcmp(argv[optind], "read")) {
				arg->command = COMMAND_READ;
				arguments_count = 2;
			} else if (!strcmp(argv[optind], "flash")) {
				arg->command = COMMAND_FLASH;
				arguments_count = 2;
			} else if (!strcmp(argv[optind], "erase")) {
				arg->command = COMMAND_ERASE;
				arguments_count = 1;
			} else {
				fprintf(stderr, "unknown command '%s'\n", argv[optind]);
				return -1;
			}
			if (argc - optind != arguments_count) {
				fprintf(stderr, "for %s command expected %d arguments\n",
					argv[optind], arguments_count - 1);
				return -1;
			}
		} else if (pos == 1) {
			arg->fname = strdup(argv[optind]);
		}

		optind++;
		pos++;
	}

	return 1;
}

static bool do_read(struct usb_device *dev, struct spi_flash *flash, struct arg *arg)
{
	int fd = open(arg->fname, O_CREAT | O_WRONLY | O_TRUNC,
		      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
	bool res;

	if (fd == -1) {
		error(0, errno, "ERROR: failed to open file '%s'", arg->fname);
		return false;
	}
	printf("Reading %u bytes from offset %u...\n", arg->size, arg->offset);
	res = spi_nor_read(dev, flash, arg->offset, arg->size, NULL, fd, NULL);
	if (close(fd) || !res) {
		error(0, errno, "ERROR: failed read or save data");
		return false;
	}
	printf("Read completed\n");

	return true;
}

static bool do_erase(struct usb_device *dev, struct spi_flash *flash, struct arg *arg)
{
	uint32_t erase_size;

	printf("Erasing %u bytes", arg->size);
	erase_size = spi_nor_calc_erase_size(flash, arg->offset, arg->size);
	if (erase_size != arg->size)
		printf(", rounded to %u bytes", erase_size);

	printf(" (%u sectors, starting from %u)...\n",
		erase_size / flash->erase_block, arg->offset & ~(flash->erase_block - 1));
	if (!spi_nor_erase_smart(dev, flash, arg->offset, arg->size, NULL)) {
		error(0, errno, "ERROR: failed to erase");
		return false;
	}

	printf("Erase completed\n");

	return true;
}

static bool do_flash(struct usb_device *dev, struct spi_flash *flash, struct arg *arg)
{
	struct stat stat;
	int fd = open(arg->fname, O_RDONLY);
	bool res;

	if (fd == -1) {
		error(0, errno, "ERROR: failed to open file '%s'", arg->fname);
		return false;
	}
	if (fstat(fd, &stat)) {
		error(0, errno, "ERROR: failed to get stat of file '%s'", arg->fname);
		return false;
	}
	arg->size = min(arg->size, stat.st_size);

	if (!do_erase(dev, flash, arg))
		return false;

	printf("Flashing %u bytes from offset %u...\n", arg->size, arg->offset);
	res = spi_nor_program_smart(dev, flash, arg->offset, arg->size, NULL, fd, NULL);
	close(fd);
	if (!res) {
		error(0, errno, "ERROR: failed flash or read data from file");
		return false;
	}
	printf("Flash completed\n");

	return true;
}

int main(int argc, char *argv[])
{
	struct usb_device dev;
	struct spi_flash *flash;
	struct arg arg;
	int parse_res;
	int retcode = 0;

	parse_res = parse_arg(argc, argv, &arg);
	if (parse_res <= 0)
		return -parse_res;

	if (!usb_open(&dev))
		error(1, errno, "ERROR: failed to open USB device");

	if (!spi_set_speed(&dev, false))
		error(1, errno, "ERROR: failed set speed");

	flash = spi_nor_init(&dev);
	if (arg.flash_size)
		flash->size = arg.flash_size;

	if (arg.flash_eraseblock)
		flash->erase_block = arg.flash_eraseblock;

	if (arg.flash_page)
		flash->page = arg.flash_page;

	printf("Flash:      %s\n", flash->name);
	printf("Size:       ");
	print_size(flash->size, true);
	printf("EraseBlock: ");
	print_size(flash->erase_block, true);
	printf("Page:       ");
	print_size(flash->page, true);
	printf("ID:        ");
	for (int i = 0; i < flash->id_len; i++)
		printf(" %02x", flash->ids[i]);

	printf("\n\n");
	printf("arg.offset: ");
	print_size(arg.offset, true);
	printf("arg.size:   ");
	if (arg.size == 0xffffffff)
		printf("maximum");
	else
		print_size(arg.size, true);
	printf("\n");

	if (!flash->size) {
		fprintf(stderr, "ERROR: Unknown flash size\n");
		usb_close(&dev);
		return 1;
	}
	if ((!flash->erase_block || !flash->page) &&
	    (arg.command == COMMAND_FLASH || arg.command == COMMAND_ERASE)) {
		fprintf(stderr, "ERROR: Unknown page size or erase block size\n");
		usb_close(&dev);
		return 1;
	}
	if (arg.offset + arg.size > flash->size) {
		printf("WARNING: size is truncated to SPI memory size\n");
		arg.size = flash->size - arg.offset;
	}

	switch (arg.command) {
	case COMMAND_READ:
		if (!do_read(&dev, flash, &arg))
			retcode = 1;
		break;
	case COMMAND_FLASH:
		if (!do_flash(&dev, flash, &arg))
			retcode = 1;
		break;
	case COMMAND_ERASE:
		if (!do_erase(&dev, flash, &arg))
			retcode = 1;
		break;
	default:
		break;
	}

	usb_close(&dev);

	return retcode;
}
