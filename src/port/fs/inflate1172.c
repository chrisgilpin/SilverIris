#include "inflate1172.h"

#include "puff.h"

int port_inflate1172(const uint8_t *src, size_t src_len, uint8_t *dst,
                     size_t dst_cap, size_t *out_len)
{
    unsigned long nout = 0;
    int err;

    if (out_len)
        *out_len = 0;
    if (!src || src_len <= PORT_INFLATE1172_HEADER)
        return PORT_INFLATE1172_ERR_SRC;

    err = puff(dst, (unsigned long)dst_cap, src + PORT_INFLATE1172_HEADER,
               (unsigned long)(src_len - PORT_INFLATE1172_HEADER), &nout);
    if (out_len)
        *out_len = (size_t)nout;
    if (err != 0)
        return PORT_INFLATE1172_ERR_PUFF;
    if (nout == 0 || nout > PORT_INFLATE1172_MAX)
        return PORT_INFLATE1172_ERR_SIZE;
    return PORT_INFLATE1172_OK;
}

int bgDecompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                 size_t *out_len)
{
    return port_inflate1172(src, src_len, dst, dst_cap, out_len);
}
