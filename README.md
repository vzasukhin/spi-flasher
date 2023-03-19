# SPI Flasher

**[English]** [Русский](README-ru.md)

Command line tool for Linux to program/erase/read SPI NOR memories.
Suported CH341A USB->SPI converter. CH341A protocol used
from https://github.com/setarcos/ch341prog

Libusb package is required.

Supported chips:

- M25Pxx
- S25FLxxxS, S25FSxxxS, S25FSxxxP, S79FLxxxS, S79FSxxxS
- W25Q32BV, W25Q64FV, W25Q128JV-IN/IQ/JQ/JM/FW
- MT25Qxxx

Another chips can be used if they parameters will be specified (see `--flash-size`,
`--flash-eraseblock`, `--flash-page`).

Usage:

```
spi-flasher [options] <command> [file-name]
```

Commands list:

- `read` - read data from SPI;
- `flash` - write data to SPI;
- `erase` - erase data;
- `custom` - send custom data and receive response.

Arguments list:

- `-o`, `--offset` - specify offset (address) for read/flash/erase;
- `-s`, `--size` - specify data size for read/flash/erase;
- `--verify` - check flashed data;
- `--hide-progress` - don't show progress bar (can be helpfull for automatic run to reduce
  logs size);
- `--custom-duplex` - for `custom` command will output response starting from first byte.
  Otherwise will starting response after sending data;
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
spi-flasher flash file.dat --verify

# flash data via pipe (write to stdin)
echo "Some data from command line" | spi-flasher flash -
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

If instead of file name specified "-" then data will be output to stdout and it can be used
in pipe. Example: `spi-flasher read - -s 100 > myfile.dat`.

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

If `--verify` argument is specified then flashed data will be read out and checked.

If instead of file name specified "-" then data will be input from stdin.
Example: `cat myfile.dat | spi-flasher flash -`. In this case erasing will do before writing
in each block.

## erase command

Usage:

```
spi-flasher [options] erase
```

Erase SPI Flash. If `--size` is not specified then will erased data to end of SPI Flash.

For flash command SPI Flash size and erase block must be known. If SPI Flash autodetect
failed then `--flash-size` and `--flash-eraseblock` should be specified.

## custom command

Usage:

```
spi-flasher [options] custom <bytes_to_send> <response_length>
```

Send custom data to device and receive `<response_length>` bytes of response.
For `custom` command utility doesn't transfer any additianal data via SPI (for other commands
utility requests ID registers from SPI Flash) and this command can be used for any devices
with SPI interface (not only for flashes).
Without `--custom-duplex` will receive data after send:

```
MOSI  <send_byte1> <send_byte2> ... <send_byteN>
MISO                                             <recv_byte1> <recv_byte2> ...
```

If `--custom-duplex` is specified then will start receiving data simultaneously with sending data.
This mode useless for flashes but can be helpfull for another type of devices:

```
MOSI  <send_byte1> <send_byte2> ... <send_byteN>
MISO  <recv_byte1> <recv_byte2> ... <recv_byteN>
```

Examples:

`spi-flasher custom '0x3 0 0 0' 10` - send command 3 (read from SPI Flash) beginning from
address 0 and receive 10 bytes.

`spi-flasher custom '0x9f' 4` for W25Q64FV will output:
```
Data to send:
9f
Received data:
ef 40 17 00
```

`spi-flasher custom '0x9f' 4 --custom-duplex` for W25Q64FV will output:
```
Data to send:
9f
Received data:
ff ef 40 17
```
