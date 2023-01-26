#ifndef _COMMON_H
#define _COMMON_H

#include <stdint.h>

#define KiB 1024
#define MiB (1024 * KiB)
#define GiB (1024 * MiB)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))
#define BIT(x) (1UL << (x))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

typedef void (* cb_progress)(uint32_t, uint32_t);

#endif
