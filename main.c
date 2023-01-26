#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "spi.h"
#include "spi-nor.h"
#include "usb.h"

#define PROGRESS_WIDTH 16

#define FLAG_REQUIRE_SIZE        BIT(0)
#define FLAG_REQUIRE_ERASE_BLOCK BIT(1)
#define FLAG_REQUIRE_PAGE        BIT(2)


enum command {
	COMMAND_READ,
	COMMAND_FLASH,
	COMMAND_ERASE,
	COMMAND_UNKNOWN  // must be last
};

struct arg;
struct command_op {
	const char *command_name;
	bool (* func)(struct usb_device *, struct spi_flash *, struct arg *);
	uint32_t flags;
	enum command command;
	int arguments_count;
};

struct arg {
	char *fname;
	uint32_t offset;
	uint32_t size;
	uint32_t flash_size;
	uint32_t flash_eraseblock;
	uint32_t flash_page;
	struct command_op *command_op;
	bool hide_progress;
};

struct multiplier {
	char *name;
	long mul;
};

cb_progress progress;
uint32_t progress_last_points = (uint32_t)-1;

static void print_utf8(uint32_t c)
{
	uint8_t buf[4];
	int pos = sizeof(buf) - 1;

	if (c < 0x80) {
		printf("%c", c);
		return;
	}
	for (int i = 0; i < sizeof(buf); i++) {
		int mask = GENMASK(pos + 7 - sizeof(buf), 0);
		if (!(c & ~mask)) {
			buf[pos] = c | GENMASK(7, pos + sizeof(buf));
			break;
		} else {
			buf[pos] = (c & 0x3f) | 0x80;
			c >>= 6;
		}
		if (--pos < 0)
			return;
	}
	while (pos < sizeof(buf)) {
		putchar(buf[pos++]);
	}
}

static void _progress(uint32_t pos, uint32_t size, const uint32_t *symbols, int symbols_count)
{
	uint32_t points = (uint64_t)pos * PROGRESS_WIDTH * symbols_count / size;
	uint32_t intpoints = points / symbols_count;
	uint32_t frac = points & (symbols_count - 1);

	// visible progress is not changed
	if (points == progress_last_points)
		return;

	printf("\r[");
	for (int i = 0; i < intpoints; i++)
		print_utf8(symbols[symbols_count - 1]);
	if (!frac)
		putchar(' ');
	else
		print_utf8(symbols[frac]);

	for (int i = intpoints; i < PROGRESS_WIDTH - 1; i++)
		putchar(' ');
	putchar(']');
	fflush(stdout);
	progress_last_points = points;
}

void progress_utf8(uint32_t pos, uint32_t size)
{
	static const uint32_t progress_symbols[] = { 0x258f, 0x258e, 0x258d, 0x258c,
						     0x258b, 0x258a, 0x2589, 0x2588 };

	_progress(pos, size, progress_symbols, 8);
}

void progress_ascii(uint32_t pos, uint32_t size)
{
	uint32_t progress_symbol = '#';

	_progress(pos, size, &progress_symbol, 1);
}

void progress_close(void)
{
	printf("\r%18s\r", "");
	fflush(stdout);
	progress_last_points = (uint32_t)-1;
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
	       " --hide-progress      - do not show progress bar\n" \
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
	res = spi_nor_read(dev, flash, arg->offset, arg->size, NULL, fd, progress);
	progress_close();
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
	bool res;

	printf("Erasing %u bytes", arg->size);
	erase_size = spi_nor_calc_erase_size(flash, arg->offset, arg->size);
	if (erase_size != arg->size)
		printf(", rounded to %u bytes", erase_size);

	printf(" (%u sectors, starting from %u)...\n",
		erase_size / flash->erase_block, arg->offset & ~(flash->erase_block - 1));

	res = spi_nor_erase_smart(dev, flash, arg->offset, arg->size, progress);
	progress_close();
	if (!res) {
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
	res = spi_nor_program_smart(dev, flash, arg->offset, arg->size, NULL, fd, progress);
	progress_close();
	close(fd);
	if (!res) {
		error(0, errno, "ERROR: failed flash or read data from file");
		return false;
	}
	printf("Flash completed\n");

	return true;
}

struct command_op command_ops[] = {
	{
		.command_name = "read",
		.command = COMMAND_READ,
		.flags = FLAG_REQUIRE_SIZE,
		.func = do_read,
		.arguments_count = 2,
	},
	{
		.command_name = "flash",
		.command = COMMAND_FLASH,
		.flags = FLAG_REQUIRE_SIZE | FLAG_REQUIRE_ERASE_BLOCK | FLAG_REQUIRE_PAGE,
		.func = do_flash,
		.arguments_count = 2,
	},
	{
		.command_name = "erase",
		.command = COMMAND_ERASE,
		.flags = FLAG_REQUIRE_SIZE | FLAG_REQUIRE_ERASE_BLOCK,
		.func = do_erase,
		.arguments_count = 1,
	},
};

int parse_arg(int argc, char *argv[], struct arg *arg)
{
	const struct option options[] = {
		{ "flash-size", required_argument, NULL, 0 },
		{ "flash-eraseblock", required_argument, NULL, 0 },
		{ "flash-page", required_argument, NULL, 0 },
		{ "hide-progress", no_argument, NULL, 0 },
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
			case 3:
				arg->hide_progress = true;
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
			for (int i = 0; i < ARRAY_SIZE(command_ops); i++) {
				if (!strcmp(argv[optind], command_ops[i].command_name)) {
					arg->command_op = &command_ops[i];
					break;
				}
			}
			if (!arg->command_op) {
				fprintf(stderr, "unknown command '%s'\n", argv[optind]);
				return -1;
			}
			if (argc - optind != arg->command_op->arguments_count) {
				fprintf(stderr, "for %s command expected %d arguments\n",
					argv[optind], arg->command_op->arguments_count - 1);
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

int main(int argc, char *argv[])
{
	struct usb_device dev;
	struct spi_flash *flash;
	struct arg arg;
	int parse_res;
	int retcode = 0;
	char *locale = setlocale(LC_ALL, "");

	parse_res = parse_arg(argc, argv, &arg);
	if (parse_res <= 0)
		return -parse_res;

	if (!arg.hide_progress) {
		if (strstr(locale, ".UTF-8") || strstr(locale, ".utf-8"))
			progress = progress_utf8;
		else
			progress = progress_ascii;
	}

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

	if ((arg.command_op->flags & FLAG_REQUIRE_SIZE) && !flash->size) {
		fprintf(stderr, "ERROR: Unknown flash size\n");
		usb_close(&dev);
		return 1;
	}
	if ((arg.command_op->flags & FLAG_REQUIRE_ERASE_BLOCK) && !flash->erase_block) {
		fprintf(stderr, "ERROR: Unknown erase block size\n");
		usb_close(&dev);
		return 1;
	}
	if ((arg.command_op->flags & FLAG_REQUIRE_PAGE) && !flash->page) {
		fprintf(stderr, "ERROR: Unknown page size\n");
		usb_close(&dev);
		return 1;
	}
	if (arg.offset + arg.size > flash->size) {
		// For COMMAND_FLASH size will be ajusted in do_flash().
		// Now arg.size is maximal and this is normal.
		if (arg.command_op->command != COMMAND_FLASH)
			printf("WARNING: size is truncated to SPI memory size\n");

		arg.size = flash->size - arg.offset;
	}

	if (!arg.command_op->func(&dev, flash, &arg))
		retcode = 1;

	usb_close(&dev);

	return retcode;
}
