#ifndef SILVERIRIS_COMPAT_MATH_H
#define SILVERIRIS_COMPAT_MATH_H

/*
 * Decomp include/math.h is constants-only and would shadow libm.
 * GE also declares acos(s16)/asin(s16), which collide with libm.
 * Hide those names while pulling the host <math.h>, then restore.
 */
#include_next <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifndef M_PI_F
#define M_PI_F 3.1415927f
#endif
#ifndef M_MINUS_PI_F
#define M_MINUS_PI_F -3.1415927f
#endif
#ifndef M_HALF_PI
#define M_HALF_PI (M_PI_F / 2)
#endif
#ifndef M_BAD_PI_F
#define M_BAD_PI_F 3.1410928f
#endif
#ifndef M_TAU
#define M_TAU 6.28318530717958647692
#endif
#ifndef M_TAU_F
#define M_TAU_F 6.2831855f
#endif
#ifndef M_PI_2F
#define M_PI_2F 1.5707964f
#endif
#ifndef M_THREE_HALF_PI
#define M_THREE_HALF_PI (3 * M_HALF_PI)
#endif
#ifndef M_U16_MAX_VALUE_F
#define M_U16_MAX_VALUE_F 65536.0f
#endif
#ifndef FLT_EPSILON
#define FLT_EPSILON 1.19209290E-07F
#endif

#endif
