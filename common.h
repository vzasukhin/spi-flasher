#ifndef _COMMON_H
#define _COMMON_H

#define KiB 1024
#define MiB (1024 * KiB)
#define GiB (1024 * MiB)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

#endif
