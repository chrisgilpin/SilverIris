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
