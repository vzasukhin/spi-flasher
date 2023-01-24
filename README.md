# SPI Flasher

[На русском языке](README-ru.md)

Command line tool to program/erase/read SPI NOR memories.
Suported CH341A USB->SPI converter. CH341A protocol used
from https://github.com/setarcos/ch341prog

Supported chips:

- M25Pxx
- S25FLxxxS, S25FSxxxS, S25FSxxxP, S79FLxxxS, S79FSxxxS
- W25Q32BV, W25Q64FV, W25Q128JV-IN/IQ/JQ/JM/FW
- MT25Qxxx

Usage:

```
spi-flasher [options] <command> [file-name]
```

Commands list:

- `read` - read data from SPI;
- `flash` - write data to SPI;
- `erase` - erase data.

Arguments list:

- `-o`, `--offset` - specify offset (address) for read/flash/erase;
- `-s`, `--size` - specify data size for read/flash/erase;
- `--hide-progress` - don't show progress bar (can be helpfull for automatic run to reduce
  logs size);
- `--flash-size` - override SPI Flash size;
- `--flash-eraseblock` - override erase block (sector) size;
- `--flash-page` - override page size.

Sizes can be specified as integer (in bytes) or with suffix as in `dd` utility.
Numbers can be used in Dec, Hex or Oct:

- `K` or `KiB` = 1024
- `M` or `MiB` = 1024KiB = 1048576
- `G` or `GiB` = 1024MiB = 1073741824
- `kB` = 1000
- `MB` = 1000kB = 1000000
- `GB` = 1000MB = 1000000000
- 0x100 = 256
- 0100 = 64

Examples:

```bash
# read 128 kibibytes (131072 bytes) from SPI flash
spi-flasher -s 128K read file.dat

# read 1 mebibyte (1048576 bytes), starting from 3 mebibytes (3145728 bytes)
# first 3 mebibytes will not be read
spi-flasher -o 3MiB -s 1MiB read file.dat

# read 1 megabyte (1000000 bytes) and expects than
# SPI Flash size is 2 mebibytes (2097152 bytes)
# can be helpfull if SPI Flash is not autodetected or detected wrong
spi-flasher --flash-size 2M -s 1MB read file.dat

# flash file to SPI Flash (erase is not required)
spi-flasher write file.dat
```

## read command

Usage:

```
spi-flasher [options] read <file-name>
```

Read data from SPI and saves to the file. If `--size` is not specified then will be read until
SPI Flash end.

For read command SPI Flash size must be known. If SPI Flash autodetect failed
then `--flash-size` should be specified.

## flash command

Usage:

```
spi-flasher [options] flash <file-name>
```

Read data from file and write to SPI Flash. Will be written all file, but not more than `--size`.
Also flashing will be stop if end of SPI Flash will reached.
Programm will erase requered memory region before write.

For flash command SPI Flash size, erase block and page block must be known. If SPI Flash autodetect
failed then `--flash-size`, `--flash-eraseblock` and `--flash-page` should be specified.

## erase command

Usage:

```
spi-flasher [options] erase
```

Erase SPI Flash. If `--size` is not specified then will erased data to end of SPI Flash.

For flash command SPI Flash size and erase block must be known. If SPI Flash autodetect
failed then `--flash-size` and `--flash-eraseblock` should be specified.
