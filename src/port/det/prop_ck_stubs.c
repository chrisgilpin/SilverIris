/*
 * Weak empty pickup / setup-guard record for binaries that hash crc_props
 * but do not link prop.c (gun-test, tape, lockstep). prop.c is strong and
 * wins when both TUs are linked (wasm, stage, shot).
 */

#if defined(__GNUC__) || defined(__clang__)
#define PORT_CK_WEAK __attribute__((weak))
#else
#define PORT_CK_WEAK
#endif

PORT_CK_WEAK int port_prop_guard_count(void) { return 0; }

PORT_CK_WEAK int port_prop_guard_xz(int i, float *x, float *z)
{
    (void)i;
    if (x)
        *x = 0.f;
    if (z)
        *z = 0.f;
    return -1;
}

PORT_CK_WEAK int port_prop_chr_ray_hit(float ox, float oy, float oz, float dx, float dy,
                                       float dz, float *t_out)
{
    (void)ox;
    (void)oy;
    (void)oz;
    (void)dx;
    (void)dy;
    (void)dz;
    (void)t_out;
    return 0;
}

PORT_CK_WEAK int port_prop_chr_hit_xz(float *x, float *z)
{
    if (x)
        *x = 0.f;
    if (z)
        *z = 0.f;
    return -1;
}

PORT_CK_WEAK int port_prop_guard_visual_cyl(int i, float *lx, float *lz, float *radius,
                                            float *y0, float *h)
{
    (void)i;
    if (lx)
        *lx = 0.f;
    if (lz)
        *lz = 0.f;
    if (radius)
        *radius = 0.f;
    if (y0)
        *y0 = 0.f;
    if (h)
        *h = 0.f;
    return -1;
}

PORT_CK_WEAK int port_prop_guard_yaw(int i, float *yaw, int *alerted)
{
    (void)i;
    if (yaw)
        *yaw = 0.f;
    if (alerted)
        *alerted = 0;
    return -1;
}

PORT_CK_WEAK int port_prop_guard_alerted(void) { return 0; }

PORT_CK_WEAK int port_prop_pickup_pad(void) { return -1; }

PORT_CK_WEAK int port_prop_pickup_kind(void) { return 0; }

PORT_CK_WEAK int port_prop_pickup_hidden(void) { return 1; }

PORT_CK_WEAK int port_prop_pickup_xyz(float *x, float *y, float *z)
{
    if (x)
        *x = 0.f;
    if (y)
        *y = 0.f;
    if (z)
        *z = 0.f;
    return -1;
}

PORT_CK_WEAK int port_prop_drop_count(void) { return 0; }

PORT_CK_WEAK int port_prop_drop_model_at(int i)
{
    (void)i;
    return -1;
}

PORT_CK_WEAK int port_prop_drop_hidden_at(int i)
{
    (void)i;
    return 1;
}

PORT_CK_WEAK int port_prop_drop_xyz_at(int i, float *x, float *y, float *z)
{
    (void)i;
    if (x)
        *x = 0.f;
    if (y)
        *y = 0.f;
    if (z)
        *z = 0.f;
    return -1;
}

PORT_CK_WEAK int port_prop_drop_model(void) { return -1; }

PORT_CK_WEAK int port_prop_drop_hidden(void) { return 1; }

PORT_CK_WEAK int port_prop_drop_xyz(float *x, float *y, float *z)
{
    if (x)
        *x = 0.f;
    if (y)
        *y = 0.f;
    if (z)
        *z = 0.f;
    return -1;
}
