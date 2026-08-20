#ifndef SILVERIRIS_IDO_H
#define SILVERIRIS_IDO_H

#include <stddef.h>

size_t strlen(const char *s);
void debTryAdd(void *data, const char *name);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

/*
 * Force-included for every decomp translation unit.
 * IDO 5.3-isms and host libc clashes go here, not in vendored C.
 */

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef M_PI_F
#define M_PI_F 3.1415927f
#endif
#ifndef M_MINUS_PI_F
#define M_MINUS_PI_F -3.1415927f
#endif
#ifndef M_HALF_PI
#define M_HALF_PI 1.57079632679489661923f
#endif
#ifndef M_BAD_PI_F
#define M_BAD_PI_F 3.1410928f
#endif

#ifndef M_U32_MAX_VALUE_F
#define M_U32_MAX_VALUE_F 4294967296.0f
#endif

#endif
