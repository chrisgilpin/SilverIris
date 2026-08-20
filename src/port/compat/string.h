#ifndef SILVERIRIS_COMPAT_STRING_H
#define SILVERIRIS_COMPAT_STRING_H
/* Hide glibc bcopy/bcmp/bzero so libultra's (void*,void*,int) decls in os.h win. */
#define bcopy silveriris_host_bcopy
#define bcmp silveriris_host_bcmp
#define bzero silveriris_host_bzero
#include_next <string.h>
#undef bcopy
#undef bcmp
#undef bzero
#endif
