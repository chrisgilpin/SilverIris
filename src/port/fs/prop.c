#include "prop.h"

#include "c0pack.h"
#include "inflate1172.h"
#include "pack_dma.h"
#include "player/stan_walk.h"
#include "player/move.h"
#include "player/gun.h"
#include "stage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_MAX_PROPS 256
#define PORT_MAX_MODELS 128
#define PORT_MODEL_PARTS 32
#define PORT_MODEL_SEG 5
#define PORT_MODEL_BASE 0x05000000u
#define PORT_PAD_BYTES 44
#define PORT_BOUND_BYTES 68
#define PORT_BOUND_BASE 10000
#define PORT_SETUP_PTRS 10
#define INTRO_SPAWN 0
#define INTRO_END 9
#define INTRO_MAX 10
#define PORT_PROP_NEAR 4000.f
#define PORT_NODE_MAX 128
#define PORT_GDL_MAX 512
#define PORT_SKEL_GUARD_N 16
#define PORT_ANIM_IDLE_OFF 0x1Cu
#define PORT_ANIM_WALK_OFF 0x4018u /* PTR_ANIM_walking */
#define PORT_ANIM_WALK_FRAME 8
#define PORT_WALK_ID_BASE 3000
#define PORT_DIE_ID_BASE 4000
/* Last-frame rest. PTR_ANIM_die is not a table name; these are. */
#define PORT_ANIM_DIE_OFF 0x30B8u /* PTR_ANIM_death_forward_face_down */
#define PORT_ANIM_DIE_OFF2 0x32C8u /* PTR_ANIM_death_backward_fall_face_up1 */
#define PORT_ANIM_DIE_OFF3 0x3AF0u /* PTR_ANIM_death_fetal_position_right */
#define PORT_ANIM_AIM_OFF 0x144u /* PTR_ANIM_fire_standing */
#define PORT_AIM_ID_BASE 5000
/* NTSC: PORT_CHR_WALK (1) * g_GlobalTimerDelta (3). Not clock-coupled so
 * the shot harness can step without a sim tick. */
#define PORT_MOVER_WALK_STEP 3.0f
#define PORT_MOVER_TILE 80.0f
#define PORT_MOVER_ARRIVE 24.0f
#define PORT_MOVER_MAX_TILES 3
/* Same room or portal-adjacent + short xz. Walk cam ~190u; spawn |dz|>200. */
#define PORT_GUARD_FIRE_RANGE 400.0f
#define PORT_GUARD_FIRE_ALONG 200.0f
#define PORT_GUARD_FIRE_COOLDOWN 20
#define PORT_GUARD_FIRE_DAMAGE 1
/* Unalerted: must face the player (~70°) and notice for 0.5s before the
 * first shot. Alerted keep shooting on LOS. */
#define PORT_GUARD_FACE_DOT 0.35f
#define PORT_GUARD_NOTICE_TICKS 10
#define PORT_GUARD_AIM_Y 160.0f
#define PORT_GUARD_AIM_OFFSET 35.0f
/* Player shot hear: same/adj room, xz<=800. Wider than the fire box,
 * not facility-wide — a 2000u sniper stays asleep. Alerted living
 * guards outside the fire box then walk toward the player (chase).
 * Z-floor: they must not enter the spawn stall fire box. */
#define PORT_GUARD_HEAR_RANGE 800.0f
#define PORT_RST_MAGIC 0x52535431u
#define PORT_ANIM_ENTRIES_ROM_U 1198784u
#define PORT_ANIM_DATA_PATH "assets/animationtable_data.bin"
#define PORT_ANIM_ENTRY_PATH "assets/animationtable_entries.bin"
#define TEXREC_BYTES 12
#define NODE_BYTES 24

#define PDEF_DOOR 1
#define PDEF_PROP 3
#define PDEF_KEY 4
#define PDEF_ALARM 5
#define PDEF_MAGAZINE 7
#define PDEF_COLLECTABLE 8
#define PDEF_GUARD 9
#define PDEF_AMMO 20
#define PDEF_ARMOUR 21
#define PDEF_RACK 12
#define PDEF_GAS 36
#define PDEF_GLASS 42
#define PDEF_TINTED 47
#define PDEF_END 48
#define PDEF_MAX 49
#define DOORTYPE_SLIDING 0
#define DOORTYPE_VERTICAL 4
#define DOORTYPE_SWINGING 5
#define PORT_DOOR_OPEN_YAW 90.f
#define PORT_DOOR_SLIDE 180.f
#define PORT_DOOR_HALF_W 90.f
/* Posed C*Z idle AABB is ~1510 (480 spine + ~900 legs), not 480. Fit to 185. */
#define PORT_CHR_STAND 185.f
/* GwppkZ MODELFILEHEADER: NUMSWITCHES=0x24 NUMTEXTURES=0xC. */
#define PORT_GUN_WPPK_NSW 0x24
#define PORT_GUN_WPPK_NTEX 0x0C
/* Pack FP KF7 is Gak47Z (filelist + pack dump). No Gkalash.
 * MODELFILEHEADER: NUMSWITCHES=0x24 NUMTEXTURES=0x12.
 * PchrkalashZ is third-person only — never a first-person hold. */
#define PORT_GUN_AK47_NSW 0x24
#define PORT_GUN_AK47_NTEX 0x12
/* Gmp5kZ MODELFILEHEADER: NUMSWITCHES=0x24 NUMTEXTURES=9. */
#define PORT_GUN_MP5K_NSW 0x24
#define PORT_GUN_MP5K_NTEX 9
#define PORT_PROP_CHRKALASH 184
#define PORT_PROP_CHRMP5K 189

#define PI_F 3.14159265f

typedef struct {
    uint16_t id;
    uint16_t nswitch;
    uint16_t ntex;
    float scale;
    const char *name;
} PortPropCat;

#include "prop_catalog.inc.c"
#include "chr_catalog.inc.c"

typedef struct {
    const uint8_t *pri;
    uint32_t pri_n;
    const uint8_t *sec;
    uint32_t sec_n;
    float ox, oy, oz;
    float rx, ry, rz;
    uintptr_t vtx4; /* DLCOLLISION vertex bank for G_VTX 0x04 */
    uint32_t nvtx;
} PortPart;

typedef struct {
    int id;
    uint16_t nswitch;
    uint16_t ntex;
    uint8_t *file;
    size_t file_len;
    PortPart part[PORT_MODEL_PARTS];
    int npart;
    float head_off[3];
    float head_rx, head_ry, head_rz;
    int have_head;
    float fit_scale;
    float fit_ymin;
} PortModel;

typedef struct {
    int type;
    int model;
    int pad;
    float pos[3];
    float look[3];
    float scale;
    float yaw;
    int alerted;
    int notice;
    int door_type;
    float max_frac;
    int hidden;
    int pickup_kind;
    int pickup_amount;
    int chrnum;
    int held_model;
    int dropped;
    PortModel *mdl;
    PortModel *head;
    float head_off[3];
    float head_rx, head_ry, head_rz;
} PortProp;

static uint8_t *g_setup;
static size_t g_setup_len;
static PortProp g_prop[PORT_MAX_PROPS];
static int g_nprop;
static PortModel g_mdl[PORT_MAX_MODELS];
static int g_nmdl;
static int g_drawn;
static PortModel g_retail_slab;
static int g_retail_slab_ok;
static int g_intro_pad = -1;
static int g_have_intro;
static float g_intro_pos[3];
static float g_intro_look[3];
#define PORT_INTRO_MAX 4
static int g_nintro;
static int g_intro_pads[PORT_INTRO_MAX];
static float g_intro_xyz[PORT_INTRO_MAX][3];
static float g_intro_looks[PORT_INTRO_MAX][3];
static float g_idle_rest[PORT_SKEL_GUARD_N][3];
static float g_walk_rest[PORT_SKEL_GUARD_N][3];
static float g_aim_rest[PORT_SKEL_GUARD_N][3];
static float g_die_rest[PORT_SKEL_GUARD_N][3];
static int g_have_idle;
static int g_have_walk;
static int g_have_aim;
static int g_have_die;
static uint32_t g_die_off;
static uint32_t g_die_nframes;
static int g_die_frame;
static int g_die_started;
static int g_die_tick_ok;
static float g_die_hold[PORT_SKEL_GUARD_N][3];
static int g_walkers;
static int g_walk_prop;
static int g_idle_prop;
static int g_walk_frame;
static uint32_t g_walk_nframes;
static float g_walk_fit_scale;
static float g_walk_fit_ymin;
static int g_walk_path_ok;
static int g_walk_going_b;
static int g_walk_blocked;
static int g_fire_cd;
static int g_guard_shots;
static int g_guard_los;
static float g_walk_path_ax, g_walk_path_az;
static float g_walk_path_bx, g_walk_path_bz;
static float g_walk_spawn_x, g_walk_spawn_z;
static int g_have_spawn_xz;
static char g_idle_info[96];
static char g_walk_info[96];

#define PORT_PICKUP_MAX_CAND 96
#define PORT_PICKUP_DRAW 800.0f
#define PORT_PICKUP_SLAB 80.0f
#define PORT_PICKUP_AMMO_ADD 7
#define PORT_PICKUP_GROUND_EYE 86.8f
#define PORT_PICKUP_GROUND_SLACK 50.0f

typedef struct {
    int type;
    int model;
    int pad;
    int orig_pad;
    unsigned flags;
    float pos[3];
    float look[3];
    int amount_raw;
} PortPickupCand;

static PortPickupCand g_pcand[PORT_PICKUP_MAX_CAND];
static int g_npcand;
static int g_pickup_prop = -1;
static int g_pickup_drawn;
#define PORT_CHR_GUN_MAX 256
static int g_chr_gun[PORT_CHR_GUN_MAX];
#define PORT_DROP_MAX 32
static int g_drops[PORT_DROP_MAX];
static int g_ndrop;
static int g_drop_drawn;

static int drop_index_of(int pi)
{
    int i;
    for (i = 0; i < g_ndrop; i++) {
        if (g_drops[i] == pi)
            return i;
    }
    return -1;
}
static void maybe_spawn_death_drops(void);
static char g_aim_info[96];
static char g_die_info[96];
static char g_pose_info[400];
static const float (*g_pose_rest)[3];
static int g_viewgun_parts;
static int g_viewgun_id = PORT_GUN_WPPK_ID;
static PortModel *load_wppk(void);
static PortModel *load_ak47(void);
static PortModel *load_mp5k(void);
static PortModel *load_chr(int body);
static const PortPropCat k_wppk_gun = { PORT_GUN_WPPK_ID, PORT_GUN_WPPK_NSW,
                                       PORT_GUN_WPPK_NTEX, 1.f, "wppk" };
static const PortPropCat k_ak47_gun = { PORT_GUN_AK47_ID, PORT_GUN_AK47_NSW,
                                       PORT_GUN_AK47_NTEX, 1.f, "ak47" };
static const PortPropCat k_mp5k_gun = { PORT_GUN_MP5K_ID, PORT_GUN_MP5K_NSW,
                                       PORT_GUN_MP5K_NTEX, 1.f, "mp5k" };

/* SKELETON(guard) JOINTLIST mtxA — bitstream channel base per JointID. */
static const uint16_t k_guard_mtxa[PORT_SKEL_GUARD_N] = {
    0x00, 0x00, 0x03, 0x06, 0x09, 0x0C, 0x0F, 0x12,
    0x15, 0x18, 0x1B, 0x1E, 0x21, 0x24, 0x27, 0x2A,
};

static const uint8_t k_intro_words[INTRO_MAX] = {
    3, 4, 4, 8, 2, 2, 10, 3, 2, 1,
};

static const uint8_t k_pdef_words[PDEF_MAX] = {
    1,  64, 2,  32, 33, 32, 0x3b, 0x21, 0x22, 7,  0x40, 0x95, 32, 0x36, 3,
    32, 1,  32, 3,  4,  0x2d, 0x22, 4,  4,    1,  2,    2,    2,  2,    2,
    4,  1,  4,  5,  1,  4,    32,  10,  4,    0x2c, 0x2d, 1,  32, 32,   5,
    0x38, 7, 37, 1,
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static float be_f32(const uint8_t *p)
{
    uint32_t u = be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void mtx_ident4(float m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.f : 0.f;
}

static void mtx_mul4(float out[4][4], const float a[4][4], const float b[4][4])
{
    float t[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            float s = 0.f;
            for (k = 0; k < 4; k++)
                s += a[i][k] * b[k][j];
            t[i][j] = s;
        }
    }
    memcpy(out, t, sizeof t);
}

/* G1 column-vector XYZ Euler + translation in last column. */
static void mtx_local(float m[4][4], float x, float y, float z, float rx, float ry, float rz)
{
    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);
    mtx_ident4(m);
    m[0][0] = cy * cz;
    m[0][1] = (sx * cz * sy) - (cx * sz);
    m[0][2] = (cx * cz * sy) + (sx * sz);
    m[1][0] = cy * sz;
    m[1][1] = (sx * sz * sy) + (cx * cz);
    m[1][2] = (cx * sz * sy) - (sx * cz);
    m[2][0] = -sy;
    m[2][1] = sx * cy;
    m[2][2] = cx * cy;
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
}

static void mtx_euler(const float m[4][4], float *rx, float *ry, float *rz)
{
    *ry = atan2f(-m[2][0], sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0]));
    *rx = atan2f(m[2][1], m[2][2]);
    *rz = atan2f(m[1][0], m[0][0]);
}

/* modelAnimReadBitsAsU16Angle — width bits at bitOffset, left-justified to u16. */
static uint16_t anim_u16(const uint8_t *bits, size_t n, uint8_t width, uint32_t bitoff)
{
    uint32_t value = 0, mask;
    uint8_t remain = width, here;
    size_t byte = (size_t)(bitoff / 8u);
    uint32_t skip = bitoff % 8u;
    if (!bits || width == 0 || width > 16)
        return 0;
    if (byte >= n)
        return 0;
    bits += byte;
    n -= byte;
    here = (uint8_t)(8u - skip);
    while (remain >= here) {
        if (!n)
            break;
        remain = (uint8_t)(remain - here);
        mask = (1u << here) - 1u;
        value |= ((uint32_t)(*bits) & mask) << remain;
        value &= 0xffffu;
        bits++;
        n--;
        here = 8;
    }
    if (remain > 0 && n) {
        mask = (1u << remain) - 1u;
        value |= ((uint32_t)(*bits) >> (here - remain)) & mask;
        value &= 0xffffu;
    }
    value <<= (16u - width);
    return (uint16_t)(value & 0xffffu);
}

static int decode_anim_frame(uint32_t data_off, int want_frame, float rest[PORT_SKEL_GUARD_N][3],
                             uint32_t *addr_out, uint32_t *off_out, uint32_t *frames_out,
                             uint8_t *width_out, char *err, size_t errn)
{
    const C0Pack *pack;
    const C0PackEntry *data, *ent;
    const uint8_t *hdr, *frame;
    uint32_t addr, frames, frame_bits, frame_bytes, off, take;
    uint8_t width;
    int j;

    memset(rest, 0, sizeof(float) * PORT_SKEL_GUARD_N * 3);
    pack = port_pack();
    if (!pack) {
        snprintf(err, errn, "skip=no_pack");
        return 0;
    }
    data = c0pack_find(pack, PORT_ANIM_DATA_PATH);
    if (!data)
        data = c0pack_find_tail(pack, PORT_ANIM_DATA_PATH);
    ent = c0pack_find(pack, PORT_ANIM_ENTRY_PATH);
    if (!ent)
        ent = c0pack_find_tail(pack, PORT_ANIM_ENTRY_PATH);
    if (!data || !ent || data->size < data_off + 16u || ent->size == 0) {
        snprintf(err, errn, "skip=no_bins data=%d ent=%d", data ? (int)data->size : -1,
                 ent ? (int)ent->size : -1);
        return 0;
    }
    hdr = data->bytes + data_off;
    addr = be32(hdr);
    frames = be16(hdr + 4);
    width = hdr[6];
    frame_bits = be16(hdr + 14);
    if (frames < 1 || width < 8 || width > 16 || frame_bits < (uint32_t)width) {
        snprintf(err, errn, "skip=hdr addr=0x%x fr=%u w=%u bits=%u", addr, frames, width,
                 frame_bits);
        return 0;
    }
    frame_bytes = frame_bits >> 3;
    if (frame_bytes == 0 || frame_bytes * 8u < 45u * (uint32_t)width) {
        snprintf(err, errn, "skip=frame addr=0x%x bytes=%u w=%u", addr, frame_bytes, width);
        return 0;
    }
    take = (uint32_t)want_frame;
    if (take >= frames)
        take = frames / 4u;
    off = 0xFFFFFFFFu;
    /* PTR_ANIM_ENTRY_idle is 0: relative offset into entries.bin, not a skip. */
    if (addr >= PORT_ANIM_ENTRIES_ROM_U &&
        addr + (take + 1u) * frame_bytes <= PORT_ANIM_ENTRIES_ROM_U + (uint32_t)ent->size)
        off = addr - PORT_ANIM_ENTRIES_ROM_U;
    else if ((addr & 0x00FFFFFFu) >= PORT_ANIM_ENTRIES_ROM_U &&
             (addr & 0x00FFFFFFu) + (take + 1u) * frame_bytes <=
                 PORT_ANIM_ENTRIES_ROM_U + (uint32_t)ent->size)
        off = (addr & 0x00FFFFFFu) - PORT_ANIM_ENTRIES_ROM_U;
    else if (addr + (take + 1u) * frame_bytes <= (uint32_t)ent->size)
        off = addr;
    if (off == 0xFFFFFFFFu || off + (take + 1u) * frame_bytes > (uint32_t)ent->size) {
        snprintf(err, errn, "skip=region addr=0x%x ent=%u need=%u", addr, (unsigned)ent->size,
                 (take + 1u) * frame_bytes);
        return 0;
    }
    frame = ent->bytes + off + take * frame_bytes;
    for (j = 0; j < PORT_SKEL_GUARD_N; j++) {
        uint32_t bo = (uint32_t)k_guard_mtxa[j] * (uint32_t)width;
        uint16_t ax = anim_u16(frame, frame_bytes, width, bo);
        uint16_t ay = anim_u16(frame, frame_bytes, width, bo + width);
        uint16_t az = anim_u16(frame, frame_bytes, width, bo + 2u * width);
        rest[j][0] = (float)ax * (2.f * PI_F) / 65536.f;
        rest[j][1] = (float)ay * (2.f * PI_F) / 65536.f;
        rest[j][2] = (float)az * (2.f * PI_F) / 65536.f;
    }
    if (addr_out)
        *addr_out = addr;
    if (off_out)
        *off_out = off;
    if (frames_out)
        *frames_out = frames;
    if (width_out)
        *width_out = width;
    snprintf(err, errn, "addr=0x%x off=%u fr=%u w=%u f=%u", addr, off, frames, width, take);
    return 1;
}

static void load_idle_rest(void)
{
    char err[80];
    uint32_t addr = 0, off = 0, frames = 0;
    uint8_t width = 0;

    g_have_idle = 0;
    g_have_walk = 0;
    g_have_aim = 0;
    g_have_die = 0;
    g_die_off = 0;
    g_die_nframes = 0;
    g_die_frame = 0;
    g_die_started = 0;
    g_die_tick_ok = 0;
    g_walkers = 0;
    g_walk_prop = -1;
    g_idle_prop = -1;
    g_walk_frame = 0;
    g_walk_nframes = 0;
    g_walk_fit_scale = 0.f;
    g_walk_fit_ymin = 0.f;
    g_walk_path_ok = 0;
    g_walk_going_b = 1;
    g_walk_blocked = 0;
    g_have_spawn_xz = 0;
    g_walk_spawn_x = 0.f;
    g_walk_spawn_z = 0.f;
    g_fire_cd = 0;
    g_guard_shots = 0;
    g_guard_los = 0;
    g_viewgun_parts = 0;
    g_viewgun_id = PORT_GUN_WPPK_ID;
    g_pose_rest = NULL;
    memset(g_idle_rest, 0, sizeof g_idle_rest);
    memset(g_walk_rest, 0, sizeof g_walk_rest);
    memset(g_aim_rest, 0, sizeof g_aim_rest);
    memset(g_die_rest, 0, sizeof g_die_rest);
    memset(g_die_hold, 0, sizeof g_die_hold);
    g_idle_info[0] = 0;
    g_walk_info[0] = 0;
    g_aim_info[0] = 0;
    g_die_info[0] = 0;
    snprintf(g_idle_info, sizeof g_idle_info, "idle=0 skip=no_pack");
    snprintf(g_walk_info, sizeof g_walk_info, "walk=0 skip=no_pack");
    snprintf(g_aim_info, sizeof g_aim_info, "aim=0 skip=no_pack");
    snprintf(g_die_info, sizeof g_die_info, "die=0 skip=no_pack");
    if (decode_anim_frame(PORT_ANIM_IDLE_OFF, 0, g_idle_rest, &addr, &off, &frames, &width, err,
                          sizeof err)) {
        g_have_idle = 1;
        snprintf(g_idle_info, sizeof g_idle_info, "idle=1 addr=0x%x off=%u fr=%u w=%u", addr, off,
                 frames, width);
    } else {
        snprintf(g_idle_info, sizeof g_idle_info, "idle=0 %s", err);
    }
    addr = off = frames = 0;
    width = 0;
    if (decode_anim_frame(PORT_ANIM_WALK_OFF, PORT_ANIM_WALK_FRAME, g_walk_rest, &addr, &off,
                          &frames, &width, err, sizeof err)) {
        g_have_walk = 1;
        g_walk_frame = PORT_ANIM_WALK_FRAME;
        g_walk_nframes = frames;
        snprintf(g_walk_info, sizeof g_walk_info, "walk=1 addr=0x%x off=%u fr=%u w=%u f=%u n=0",
                 addr, off, frames, width, (unsigned)PORT_ANIM_WALK_FRAME);
    } else {
        snprintf(g_walk_info, sizeof g_walk_info, "walk=0 %s", err);
    }
    addr = off = frames = 0;
    width = 0;
    if (decode_anim_frame(PORT_ANIM_AIM_OFF, 0, g_aim_rest, &addr, &off, &frames, &width, err,
                          sizeof err)) {
        int j, ident = 1, same_idle = 1;
        for (j = 0; j < PORT_SKEL_GUARD_N; j++) {
            if (g_aim_rest[j][0] != 0.f || g_aim_rest[j][1] != 0.f || g_aim_rest[j][2] != 0.f)
                ident = 0;
            if (g_aim_rest[j][0] != g_idle_rest[j][0] || g_aim_rest[j][1] != g_idle_rest[j][1] ||
                g_aim_rest[j][2] != g_idle_rest[j][2])
                same_idle = 0;
        }
        /* Header decodes (same path as idle/walk/death) and is not
         * identity / idle. The 16-joint Euler bind still explodes the
         * mesh (stretched head, collapsed torso) for fire_standing and
         * aim_one_handed_weapon_left_right. Keep idle — do not fake an
         * arm raise. */
        if (ident) {
            snprintf(g_aim_info, sizeof g_aim_info, "aim=0 skip=tpose addr=0x%x", addr);
        } else if (same_idle) {
            snprintf(g_aim_info, sizeof g_aim_info, "aim=0 skip=same_idle addr=0x%x", addr);
        } else {
            g_have_aim = 0;
            snprintf(g_aim_info, sizeof g_aim_info,
                     "aim=0 skip=pose addr=0x%x off=%u fr=%u w=%u", addr, off, frames, width);
        }
    } else {
        snprintf(g_aim_info, sizeof g_aim_info, "aim=0 %s", err);
    }
    {
        static const uint32_t k_die_off[] = {
            PORT_ANIM_DIE_OFF, PORT_ANIM_DIE_OFF2, PORT_ANIM_DIE_OFF3
        };
        int di;
        g_have_die = 0;
        snprintf(g_die_info, sizeof g_die_info, "die=0 skip=hdr");
        for (di = 0; di < 3 && !g_have_die; di++) {
            uint32_t daddr = 0, doff = 0, dfr = 0;
            uint8_t dw = 0;
            int last;
            if (!decode_anim_frame(k_die_off[di], 0, g_die_rest, &daddr, &doff, &dfr, &dw,
                                   err, sizeof err)) {
                snprintf(g_die_info, sizeof g_die_info, "die=0 %s", err);
                continue;
            }
            last = (dfr > 1u) ? (int)dfr - 1 : 0;
            if (last > 0 &&
                !decode_anim_frame(k_die_off[di], last, g_die_rest, &daddr, &doff, &dfr, &dw,
                                   err, sizeof err)) {
                snprintf(g_die_info, sizeof g_die_info, "die=0 last %s", err);
                continue;
            }
            g_have_die = 1;
            g_die_off = k_die_off[di];
            g_die_nframes = dfr;
            g_die_frame = last;
            g_die_started = 0;
            g_die_tick_ok = 1;
            memcpy(g_die_hold, g_die_rest, sizeof g_die_hold);
            snprintf(g_die_info, sizeof g_die_info, "die=1 addr=0x%x off=%u fr=%u w=%u f=%d",
                     daddr, doff, dfr, dw, last);
        }
    }
}

static void rest_for_group(const uint8_t *base, size_t n, uint32_t data, int use_guard,
                           float *rx, float *ry, float *rz)
{
    uint16_t joint;
    *rx = *ry = *rz = 0.f;
    if (!data || data + 14 > n)
        return;
    joint = be16(base + data + 12);
    if (data + 0x1C + 16 <= n && be32(base + data + 0x1C) == PORT_RST_MAGIC) {
        *rx = be_f32(base + data + 0x20);
        *ry = be_f32(base + data + 0x24);
        *rz = be_f32(base + data + 0x28);
        return;
    }
    /* ANIM_idle / ANIM_walking via SKELETON(guard) JointID → mtxA.
     * Aim stays skip=pose (16-joint Euler explodes). An exploded idle
     * AABB is rejected in bind_model_gdl and rebound without rest. */
    if (use_guard && g_pose_rest && joint < PORT_SKEL_GUARD_N) {
        *rx = g_pose_rest[joint][0];
        *ry = g_pose_rest[joint][1];
        *rz = g_pose_rest[joint][2];
    }
}

static int scenery_type(int t)
{
    return t == PDEF_DOOR || t == PDEF_PROP || t == PDEF_ALARM || t == PDEF_RACK ||
           t == PDEF_GAS || t == PDEF_GLASS || t == PDEF_TINTED;
}

static const PortPropCat *cat_by_id(int id)
{
    if (id < 0 || id >= PORT_PROP_CAT_N)
        return NULL;
    return &k_prop_cat[id];
}

static const PortPropCat *chr_by_id(int id)
{
    if (id < 0 || id >= PORT_CHR_CAT_N)
        return NULL;
    return &k_chr_cat[id];
}

/* expand_09_characters: hasHead==0 then HeadID>=0 else Jim/Sally.
 * HeadID in 0..41 is not a Chead*Z (synthetic tests use 0). */
static int resolve_head_id(int body, int head)
{
    if (body < 0 || body >= PORT_CHR_HEAD_START)
        return -1;
    if (k_chr_has_head[body])
        return -1;
    if (head >= PORT_CHR_HEAD_START && head < PORT_CHR_CAT_N)
        return head;
    if (head >= 0 && head < PORT_CHR_HEAD_START)
        return -1;
    return k_chr_is_male[body] ? PORT_CHR_HEAD_JIM : PORT_CHR_HEAD_SALLY;
}

static void bind_head_off(PortProp *pr, const PortModel *body)
{
    if (!pr || !body || !body->have_head)
        return;
    pr->head_off[0] = body->head_off[0];
    pr->head_off[1] = body->head_off[1];
    pr->head_off[2] = body->head_off[2];
    pr->head_rx = body->head_rx;
    pr->head_ry = body->head_ry;
    pr->head_rz = body->head_rz;
    if (pr->head) {
        pr->head->fit_scale = body->fit_scale;
        pr->head->fit_ymin = body->fit_ymin;
    }
}

static void attach_chr_head(PortProp *pr, int body, int head)
{
    int hid;
    PortModel *h;
    if (!pr)
        return;
    hid = resolve_head_id(body, head);
    if (hid < 0)
        return;
    h = load_chr(hid);
    if (!h || !h->npart)
        return;
    pr->head = h;
    if (pr->mdl)
        bind_head_off(pr, pr->mdl);
}

static uint32_t file_off(uint32_t p, size_t n)
{
    uint32_t off;
    if (!p)
        return 0;
    if ((p & 0xFF000000u) == PORT_MODEL_BASE)
        off = p & 0x00FFFFFFu;
    else
        off = p;
    if (off >= n)
        return 0;
    return off;
}

static uint32_t gdl_count(const uint8_t *base, size_t n, uint32_t off)
{
    uint32_t i;
    if (!base || off + 8 > n)
        return 0;
    for (i = 0; i < PORT_GDL_MAX && off + (i + 1u) * 8u <= n; i++) {
        /* F3D G_ENDDL is 0xB8; F3DEX2 is 0xDF. Retail P*Z uses B8. */
        if (base[off + i * 8u] == (uint8_t)0xDF || base[off + i * 8u] == (uint8_t)0xB8)
            return i + 1u;
    }
    return i;
}

static int add_part_gdl(PortModel *m, const uint8_t *base, size_t n, uint8_t op,
                        uint32_t data, float ox, float oy, float oz, float rx, float ry,
                        float rz)
{
    uint32_t p = 0, s = 0;
    PortPart *pt;

    if (!data || m->npart >= PORT_MODEL_PARTS)
        return 0;
    if (op == 24 && data + 8 <= n) {
        p = file_off(be32(base + data), n);
        s = file_off(be32(base + data + 4), n);
    } else if (op == 4 && data + 8 <= n) {
        p = file_off(be32(base + data), n);
        s = file_off(be32(base + data + 4), n);
    } else if (op == 22 && data + 12 <= n) {
        p = file_off(be32(base + data + 8), n);
    }
    if (!p)
        return 0;
    pt = &m->part[m->npart];
    memset(pt, 0, sizeof *pt);
    pt->pri = base + p;
    pt->pri_n = gdl_count(base, n, p);
    if (!pt->pri_n)
        return 0;
    if (s) {
        pt->sec = base + s;
        pt->sec_n = gdl_count(base, n, s);
    }
    pt->ox = ox;
    pt->oy = oy;
    pt->oz = oz;
    pt->rx = rx;
    pt->ry = ry;
    pt->rz = rz;
    if ((op == 24 || op == 4) && data + 12 <= n) {
        uint32_t v = file_off(be32(base + data + 8), n);
        if (v) {
            pt->vtx4 = (uintptr_t)(base + v);
            pt->nvtx = (uint32_t)be16(base + data + 12);
            if (pt->nvtx > 256u)
                pt->nvtx = 256u;
        }
    }
    m->npart++;
    return 1;
}

/* Walk Rare nodes. GROUP / GROUPSIMPLE Origin is T; guard bodies also
 * apply rest-pose R from RST1 (synthetic) or ANIM_idle frame 0 via
 * SKELETON(guard) JointID → mtxA. Identity rest = old bind-pose. */
static int walk_parts(PortModel *m, uint32_t root, int use_guard)
{
    uint32_t q[PORT_NODE_MAX];
    float qm[PORT_NODE_MAX][4][4];
    int qh = 0, qt = 0, i;
    const uint8_t *base = m->file;
    size_t n = m->file_len;
    uint32_t sw4 = 0;

    if (root + NODE_BYTES > n)
        return -1;
    /* MODELFILEHEADER Switches[4] is the HeadPlaceholder (opcode 23). */
    if (m->nswitch > 4 && n >= 20)
        sw4 = file_off(be32(base + 16), n);
    q[qt] = root;
    mtx_ident4(qm[qt]);
    qt++;
    while (qh < qt) {
        uint32_t off = q[qh];
        float parent[4][4], childm[4][4], local[4][4];
        float ox, oy, oz, rx, ry, rz;
        uint8_t op;
        uint32_t data, child, next;
        memcpy(parent, qm[qh], sizeof parent);
        qh++;
        if (off + NODE_BYTES > n)
            continue;
        op = base[off + 1];
        data = file_off(be32(base + off + 4), n);
        child = file_off(be32(base + off + 20), n);
        next = file_off(be32(base + off + 12), n);
        memcpy(childm, parent, sizeof childm);
        if ((op == 2 || op == 21) && data && data + 12 <= n) {
            ox = be_f32(base + data);
            oy = be_f32(base + data + 4);
            oz = be_f32(base + data + 8);
            rx = ry = rz = 0.f;
            if (op == 2)
                rest_for_group(base, n, data, use_guard, &rx, &ry, &rz);
            mtx_local(local, ox, oy, oz, rx, ry, rz);
            mtx_mul4(childm, parent, local);
        }
        ox = childm[0][3];
        oy = childm[1][3];
        oz = childm[2][3];
        mtx_euler(childm, &rx, &ry, &rz);
        if ((op == 4 || op == 22 || op == 24) && data)
            add_part_gdl(m, base, n, op, data, ox, oy, oz, rx, ry, rz);
        if (!m->have_head && (op == 23 || (sw4 && off == sw4))) {
            m->head_off[0] = ox;
            m->head_off[1] = oy;
            m->head_off[2] = oz;
            m->head_rx = rx;
            m->head_ry = ry;
            m->head_rz = rz;
            m->have_head = 1;
        }
        for (i = 0; i < 2; i++) {
            uint32_t c = i ? next : child;
            int j, have = 0;
            if (!c)
                continue;
            for (j = 0; j < qt; j++) {
                if (q[j] == c) {
                    have = 1;
                    break;
                }
            }
            if (!have && qt < PORT_NODE_MAX) {
                q[qt] = c;
                memcpy(qm[qt], i ? parent : childm, sizeof qm[qt]);
                qt++;
            }
        }
    }
    return m->npart ? 0 : -1;
}

static int chr_part_span(const PortModel *m, float *ymin, float *ymax)
{
    int dp;
    float lo = 1e9f, hi = -1e9f;
    if (!m || m->npart < 1)
        return 0;
    for (dp = 0; dp < m->npart; dp++) {
        if (m->part[dp].oy < lo)
            lo = m->part[dp].oy;
        if (m->part[dp].oy > hi)
            hi = m->part[dp].oy;
    }
    if (hi <= lo + 1.f)
        return 0;
    *ymin = lo;
    *ymax = hi;
    return 1;
}

static void fit_chr_stand(PortModel *m, float ymin, float ymax, const char *how)
{
    float h = ymax - ymin;
    m->fit_ymin = ymin;
    /* Idle bind is ~1510u (480 spine + ~900 legs). Fit to 185. */
    if (h > 2500.f || h < 40.f)
        m->fit_scale = PORT_CHR_STAND / 1510.f;
    else
        m->fit_scale = PORT_CHR_STAND / h;
    if (!strstr(g_idle_info, " fit=")) {
        char base[96];
        snprintf(base, sizeof base, "%s", g_idle_info);
        snprintf(g_idle_info, sizeof g_idle_info, "%s fit=%.3f h=%.0f %s",
                 base, m->fit_scale, h, how ? how : "rest=skel");
    }
}

static int bind_model_gdl(PortModel *m, int use_guard)
{
    uint32_t root, used;

    m->npart = 0;
    m->have_head = 0;
    m->head_rx = m->head_ry = m->head_rz = 0.f;
    m->fit_scale = 1.f;
    m->fit_ymin = 0.f;
    if (!m->file || m->file_len < 8)
        return -1;
    if (be32(m->file) == PORT_BG_MAGIC_G1DL) {
        m->part[0].pri = m->file + 4;
        m->part[0].pri_n = gdl_count(m->file, m->file_len, 4);
        m->npart = m->part[0].pri_n ? 1 : 0;
        return m->npart ? 0 : -1;
    }
    root = 0;
    /* Synthetic node trees start with a DL opcode; retail files start with
     * a texture table or 0x05 switch pointers. */
    if (m->file_len >= NODE_BYTES &&
        (m->file[1] == 2 || m->file[1] == 4 || m->file[1] == 22 || m->file[1] == 24))
        root = 0;
    else
        root = (uint32_t)m->nswitch * 4u + (uint32_t)m->ntex * TEXREC_BYTES;
    used = root;
    if (walk_parts(m, root, use_guard) != 0 && root != 0) {
        m->npart = 0;
        m->have_head = 0;
        m->head_rx = m->head_ry = m->head_rz = 0.f;
        walk_parts(m, 0, use_guard);
        used = 0;
    }
    if (use_guard && m->npart) {
        float ymin = 0.f, ymax = 0.f;
        if (chr_part_span(m, &ymin, &ymax)) {
            float h = ymax - ymin;
            /* Exploded 16-joint Euler (stretched head / collapsed torso):
             * drop rest and keep RST1 / identity so the C*Z mesh still
             * stands at 185u. Do not invent a capsule. Aim stays skip=pose. */
            if (g_pose_rest && (h > 2500.f || h < 40.f)) {
                const float (*save)[3] = g_pose_rest;
                g_pose_rest = NULL;
                m->npart = 0;
                m->have_head = 0;
                m->head_rx = m->head_ry = m->head_rz = 0.f;
                walk_parts(m, used, use_guard);
                g_pose_rest = save;
                if (chr_part_span(m, &ymin, &ymax))
                    fit_chr_stand(m, ymin, ymax, "skip=aabb");
            } else {
                fit_chr_stand(m, ymin, ymax, g_pose_rest ? "rest=skel" : "rest=rst1");
            }
        }
    }
    return m->npart ? 0 : -1;
}

static int inflate_or_copy(const uint8_t *src, size_t n, uint8_t **out, size_t *out_len)
{
    size_t need = 0;
    uint8_t *exp;
    int rc;

    if (n >= PORT_INFLATE1172_HEADER && src[0] == 0x11 && src[1] == 0x72) {
        rc = bgDecompress(src, n, NULL, 0, &need);
        if (rc != PORT_INFLATE1172_OK || need == 0)
            return -1;
        exp = (uint8_t *)malloc(need);
        if (!exp)
            return -2;
        rc = bgDecompress(src, n, exp, need, &need);
        if (rc != PORT_INFLATE1172_OK) {
            free(exp);
            return -1;
        }
        *out = exp;
        *out_len = need;
        return 0;
    }
    exp = (uint8_t *)malloc(n);
    if (!exp)
        return -2;
    memcpy(exp, src, n);
    *out = exp;
    *out_len = n;
    return 0;
}

static PortModel *load_named(int id, const char *dir, const char *prefix, const PortPropCat *cat,
                             int use_guard, const float (*rest)[3])
{
    const C0Pack *pack;
    const C0PackEntry *e;
    const float (*save)[3];
    char path[160];
    PortModel *m;
    uint8_t *file = NULL;
    size_t flen = 0;
    int i, rc;

    save = g_pose_rest;
    g_pose_rest = rest;

    for (i = 0; i < g_nmdl; i++) {
        if (g_mdl[i].id == id) {
            g_pose_rest = save;
            return g_mdl[i].npart ? &g_mdl[i] : NULL;
        }
    }
    if (g_nmdl >= PORT_MAX_MODELS || !cat) {
        g_pose_rest = save;
        return NULL;
    }
    pack = port_pack();
    if (!pack) {
        g_pose_rest = save;
        return NULL;
    }
    snprintf(path, sizeof path, "assets/obseg/%s/%s%sZ.bin", dir, prefix, cat->name);
    e = c0pack_find(pack, path);
    if (!e)
        e = c0pack_find_tail(pack, path);
    if (!e || e->size == 0) {
        g_pose_rest = save;
        return NULL;
    }
    rc = inflate_or_copy(e->bytes, e->size, &file, &flen);
    if (rc != 0) {
        g_pose_rest = save;
        return NULL;
    }
    m = &g_mdl[g_nmdl];
    memset(m, 0, sizeof *m);
    m->id = id;
    m->nswitch = cat->nswitch;
    m->ntex = cat->ntex;
    m->file = file;
    m->file_len = flen;
    if (bind_model_gdl(m, use_guard) != 0) {
        free(file);
        memset(m, 0, sizeof *m);
        g_pose_rest = save;
        return NULL;
    }
    g_nmdl++;
    g_pose_rest = save;
    return m;
}

static PortModel *load_model(int id)
{
    return load_named(id, "prop", "P", cat_by_id(id), 0, NULL);
}

/* BodyID is the BODIES / c_item_entries index. Missing pack blob → NULL. */
static PortModel *load_chr(int body)
{
    int use_guard = body >= 0 && body < PORT_CHR_HEAD_START;
    return load_named(1000 + body, "chr", "C", chr_by_id(body), use_guard,
                      use_guard && g_have_idle ? g_idle_rest : NULL);
}

static PortModel *load_chr_walk(int body)
{
    int use_guard = body >= 0 && body < PORT_CHR_HEAD_START;
    if (!g_have_walk)
        return NULL;
    return load_named(PORT_WALK_ID_BASE + body, "chr", "C", chr_by_id(body), use_guard,
                      g_walk_rest);
}

/* Separate model id so a death rest does not flatten every living clone. */
static PortModel *load_chr_die(int body)
{
    PortModel *m, *live;
    int use_guard = body >= 0 && body < PORT_CHR_HEAD_START;
    int i;
    if (!g_have_die || !use_guard)
        return NULL;
    m = load_named(PORT_DIE_ID_BASE + body, "chr", "C", chr_by_id(body), 1, g_die_rest);
    if (!m)
        return NULL;
    live = NULL;
    for (i = 0; i < g_nmdl; i++) {
        if (g_mdl[i].id == PORT_WALK_ID_BASE + body || g_mdl[i].id == 1000 + body) {
            live = &g_mdl[i];
            if (g_mdl[i].id == PORT_WALK_ID_BASE + body)
                break;
        }
    }
    if (live && live->fit_scale != 0.f) {
        m->fit_scale = live->fit_scale;
        m->fit_ymin = live->fit_ymin;
    } else if (g_walk_fit_scale != 0.f) {
        m->fit_scale = g_walk_fit_scale;
        m->fit_ymin = g_walk_fit_ymin;
    }
    return m;
}

/* Unique-per-body fire/aim rest. Keep this rest's own 185u fit when the
 * posed part-Y span is standing-ish (idle is ~1510). An exploded rest
 * (T-pose leftover or bad Euler) is rejected — idle, no fake arm. */
static int aim_height_ok(const PortModel *m, float *h_out)
{
    float h;
    if (!m || m->fit_scale < 1e-6f)
        return 0;
    h = PORT_CHR_STAND / m->fit_scale;
    if (h_out)
        *h_out = h;
    return h >= 600.f && h <= 2200.f;
}

static PortModel *load_chr_aim(int body)
{
    PortModel *m;
    float h = 0.f;
    int use_guard = body >= 0 && body < PORT_CHR_HEAD_START;
    if (!g_have_aim || !use_guard)
        return NULL;
    m = load_named(PORT_AIM_ID_BASE + body, "chr", "C", chr_by_id(body), 1, g_aim_rest);
    if (!m)
        return NULL;
    if (!aim_height_ok(m, &h)) {
        snprintf(g_aim_info, sizeof g_aim_info, "aim=0 skip=aabb h=%.0f", h);
        g_have_aim = 0;
        return NULL;
    }
    if (!strstr(g_aim_info, " fit=")) {
        char base[96];
        snprintf(base, sizeof base, "%s", g_aim_info);
        snprintf(g_aim_info, sizeof g_aim_info, "%s fit=%.3f h=%.0f", base, m->fit_scale, h);
    }
    return m;
}

/* Closest-to-spawn setup guard stays idle rest (start around the corner)
 * until they chase. Next-closest is the posed-walk test mover. Extra
 * stepping chasers borrow the same per-body walk model. */
static void assign_walkers(void)
{
    int i, best = -1, next = -1, prev_walk;
    float best_d = 1e18f, next_d = 1e18f;
    float r1[3];
    float sx, sz;
    PortModel *wm;
    const PortPropCat *cat;
    size_t wn;

    /* Rank from snapped Bond (local), not raw intro pad 167. That pad
     * sits 144u past the last walkway tile; ranking from it can leave
     * the look-slab idle on the spawn 270 cone. */
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    if (g_have_spawn_xz) {
        sx = g_walk_spawn_x;
        sz = g_walk_spawn_z;
    } else if (g_have_intro) {
        sx = g_intro_pos[0] - r1[0];
        sz = g_intro_pos[2] - r1[2];
    } else {
        sx = 0.f;
        sz = 0.f;
    }

    prev_walk = g_walk_prop;
    g_walkers = 0;
    g_walk_prop = -1;
    g_idle_prop = -1;
    for (i = 0; i < g_nprop; i++) {
        float lx, lz, dx, dz, d;
        if (g_prop[i].type != PDEF_GUARD || !g_prop[i].mdl)
            continue;
        lx = g_prop[i].pos[0] - r1[0];
        lz = g_prop[i].pos[2] - r1[2];
        dx = lx - sx;
        dz = lz - sz;
        d = dx * dx + dz * dz;
        if (d < best_d) {
            next = best;
            next_d = best_d;
            best = i;
            best_d = d;
        } else if (d < next_d) {
            next = i;
            next_d = d;
        }
    }
    g_idle_prop = best;
    /* Re-rank after the stall snap may pick a different mover. */
    if (prev_walk >= 0 && prev_walk < g_nprop && prev_walk != next) {
        PortModel *im = load_chr(g_prop[prev_walk].model);
        const PortPropCat *icat = chr_by_id(g_prop[prev_walk].model);
        if (im) {
            g_prop[prev_walk].mdl = im;
            g_prop[prev_walk].scale =
                (icat ? icat->scale : 1.f) *
                (im->fit_scale != 0.f ? im->fit_scale : 1.f);
            bind_head_off(&g_prop[prev_walk], im);
        }
    }
    if (g_have_spawn_xz && (best >= 0 || next >= 0))
        printf("walker_rank snapped=1 sx=%.1f,%.1f idle=%d walk=%d\n",
               (double)sx, (double)sz, best, next);
    if (!g_have_walk || next < 0)
        return;
    wm = load_chr_walk(g_prop[next].model);
    if (!wm)
        return;
    cat = chr_by_id(g_prop[next].model);
    g_prop[next].mdl = wm;
    g_prop[next].scale =
        (cat ? cat->scale : 1.f) * (wm->fit_scale != 0.f ? wm->fit_scale : 1.f);
    bind_head_off(&g_prop[next], wm);
    g_walkers = 1;
    g_walk_prop = next;
    if (wm) {
        g_walk_fit_scale = wm->fit_scale;
        g_walk_fit_ymin = wm->fit_ymin;
    }
    wn = strlen(g_walk_info);
    if (wn >= 3 && g_walk_info[wn - 3] == 'n' && g_walk_info[wn - 1] == '0')
        g_walk_info[wn - 1] = '1';
    (void)next_d;
}

/* Spawn looks 270 (-X). Player-space corridor is a west slab around
 * spawn z. Stay off that so spawn.png stays a tiled corridor, not a
 * 1510u blob or a walker in the lens. */
static int spawn_look_slab(float x, float z, float sx, float sz)
{
    float dx = x - sx, dz = z - sz, fwd;
    /* Spawn looks 270 (-X). Keep the test mover out of that cone so
     * spawn.png stays a tiled corridor, not a walker in the lens. */
    if (dx > 40.f)
        return 0;
    fwd = -dx;
    if (fwd < 80.f && dx * dx + dz * dz < 180.f * 180.f)
        return 1;
    if (fwd > 0.f && fwd < 700.f && dz * dz < (0.65f * fwd) * (0.65f * fwd))
        return 1;
    return 0;
}

static int floor_open(float lx, float lz)
{
    /* Cubicles have G1 walls on adjacent tiles. Open room-71 floor
     * has walkable neighbors ~half a pad step away. 55u catches the
     * stall cubicle at -220,-2640 whose G1 walls sit between tiles. */
    return port_stan_on_tile(lx + 55.f, lz) && port_stan_on_tile(lx - 55.f, lz) &&
           port_stan_on_tile(lx, lz + 55.f) && port_stan_on_tile(lx, lz - 55.f);
}

/* Dump-noted stall cubicle: G1 walls on every adjacent tile. */
static int in_stall_cubicle(float lx, float lz)
{
    float dx = lx + 220.f, dz = lz + 2640.f;
    return dx * dx + dz * dz < 90.f * 90.f;
}

static int walk_tile_ok(float lx, float lz, float sx, float sz)
{
    float ey = 0.f;
    if (!port_stan_on_tile(lx, lz))
        return 0;
    if (!floor_open(lx, lz))
        return 0;
    if (in_stall_cubicle(lx, lz))
        return 0;
    if (port_stan_eye_y(lx, lz, &ey) != 0)
        return 0;
    if (!(ey == ey) || ey < 50.f || ey > 160.f)
        return 0;
    if (spawn_look_slab(lx, lz, sx, sz))
        return 0;
    return 1;
}

static int try_sit_walker(float lx, float lz, float sx, float sz, const float r1[3]);

/* Sit using the already-captured spawn cone. 0 if NaN / off-tile / cone. */
static int sit_walker_local(float lx, float lz)
{
    float r1[3];
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    return try_sit_walker(lx, lz, g_walk_spawn_x, g_walk_spawn_z, r1);
}

static void face_heading_prop(int pi, float dx, float dz)
{
    float yaw;
    if (pi < 0 || pi >= g_nprop)
        return;
    if (dx * dx + dz * dz < 1e-8f)
        return;
    /* Model +Z onto look; yaw 0 faces +Z. */
    yaw = atan2f(dx, dz) * (180.f / PI_F);
    g_prop[pi].yaw = yaw;
}

static void face_heading(float dx, float dz)
{
    face_heading_prop(g_walk_prop, dx, dz);
}

static void face_player_prop(int pi)
{
    float r1[3], lx, lz;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    lx = g_prop[pi].pos[0] - r1[0];
    lz = g_prop[pi].pos[2] - r1[2];
    face_heading_prop(pi, port_player_x() - lx, port_player_z() - lz);
}

/* 2-4 tile ping-pong on open ground-floor neighbors. Prefer X (room 71
 * is wide). Stay out of the spawn 270 cone. No doors, no chase. */
static void setup_walk_path(float lx, float lz, float sx, float sz)
{
    float ax = lx, az = lz, bx = lx, bz = lz;
    float xlen, zlen;
    int i;

    g_walk_path_ok = 0;
    g_walk_blocked = 0;
    g_fire_cd = 0;
    g_guard_shots = 0;
    g_guard_los = 0;
    g_walk_going_b = 1;
    g_walk_spawn_x = sx;
    g_walk_spawn_z = sz;
    for (i = 1; i <= PORT_MOVER_MAX_TILES; i++) {
        float nx = lx - PORT_MOVER_TILE * (float)i;
        if (!walk_tile_ok(nx, lz, sx, sz))
            break;
        ax = nx;
        az = lz;
    }
    for (i = 1; i <= PORT_MOVER_MAX_TILES; i++) {
        float nx = lx + PORT_MOVER_TILE * (float)i;
        if (!walk_tile_ok(nx, lz, sx, sz))
            break;
        bx = nx;
        bz = lz;
    }
    xlen = fabsf(bx - ax);
    if (xlen < PORT_MOVER_TILE * 1.5f) {
        ax = bx = lx;
        az = bz = lz;
        for (i = 1; i <= PORT_MOVER_MAX_TILES; i++) {
            float nz = lz - PORT_MOVER_TILE * (float)i;
            if (!walk_tile_ok(lx, nz, sx, sz))
                break;
            ax = lx;
            az = nz;
        }
        for (i = 1; i <= PORT_MOVER_MAX_TILES; i++) {
            float nz = lz + PORT_MOVER_TILE * (float)i;
            if (!walk_tile_ok(lx, nz, sx, sz))
                break;
            bx = lx;
            bz = nz;
        }
        zlen = fabsf(bz - az);
        if (zlen < PORT_MOVER_TILE * 1.5f)
            return;
    }
    g_walk_path_ax = ax;
    g_walk_path_az = az;
    g_walk_path_bx = bx;
    g_walk_path_bz = bz;
    g_walk_path_ok = 1;
    face_heading(bx - ax, bz - az);
}

/* Sit on a ground-floor stan tile. Y from floor. Refuse off-tile / NaN /
 * catwalk (eye 50..160). Does not consult the spawn cone. */
static int sit_guard_tile(int pi, float lx, float lz, const float r1[3])
{
    float ey = 0.f, floor_y, wy;
    if (pi < 0 || pi >= g_nprop)
        return 0;
    if (!port_stan_on_tile(lx, lz))
        return 0;
    if (port_stan_eye_y(lx, lz, &ey) != 0)
        return 0;
    if (!(ey == ey) || ey < 50.f || ey > 160.f)
        return 0;
    floor_y = ey - PORT_EYE_HEIGHT;
    wy = floor_y + r1[1];
    if (!(wy == wy) || wy > 1.0e20f || wy < -1.0e20f)
        return 0;
    {
        float ox = g_prop[pi].pos[0];
        float oz = g_prop[pi].pos[2];
        float nx = lx + r1[0];
        float nz = lz + r1[2];
        g_prop[pi].pos[0] = nx;
        g_prop[pi].pos[1] = wy;
        g_prop[pi].pos[2] = nz;
        /* Hitscan / hide-body / pad-kill use the stan cylinder. Keep it
         * on this body. Walker and the start-corner idle both sit. */
        if (ox != nx || oz != nz)
            port_stan_move_guard(ox, oz, nx, nz);
    }
    return 1;
}

static int try_sit_guard(int pi, float lx, float lz, float sx, float sz, const float r1[3])
{
    if (spawn_look_slab(lx, lz, sx, sz))
        return 0;
    return sit_guard_tile(pi, lx, lz, r1);
}

static int try_sit_walker(float lx, float lz, float sx, float sz, const float r1[3])
{
    return try_sit_guard(g_walk_prop, lx, lz, sx, sz, r1);
}

/* After intro snap: origin/tiles match the player. Sit on a ground-floor
 * tile around the spawn corner (hallway turn). Do not retouch stan origin.
 * If every candidate clips / NaN Y, leave them on the setup pad. */
static int tile_taken(float lx, float lz, const float r1[3], int skip)
{
    int i;
    float wx = lx + r1[0], wz = lz + r1[2];
    for (i = 0; i < g_nprop; i++) {
        float dx, dz;
        if (i == skip || g_prop[i].type != PDEF_GUARD)
            continue;
        dx = g_prop[i].pos[0] - wx;
        dz = g_prop[i].pos[2] - wz;
        if (dx * dx + dz * dz < 40.f * 40.f)
            return 1;
    }
    return 0;
}

/* Sit the start-corner idle off the spawn 270 cone so a spawn Z_TRIG
 * hits the facing door/floor, not a pre-existing pad. Walker stays. */
static int in_walk_kill_lane(float lx, float lz)
{
    /* Walk cam is ~160u south of the z=-2480 strip (x ~ -380..-60).
     * An idle in that lane steals walk_kill_body. */
    return lz > -2680.f && lz < -2460.f && lx > -360.f && lx < -80.f;
}

static void place_idle_off_spawn_look(float sx, float sz, const float r1[3])
{
    /* West of the walk strip, on the north open band, or further north.
     * Not the south-looking lane the walk cam uses. */
    static const float pref[][2] = {
        { -420.f, -2480.f },
        { -500.f, -2480.f },
        { -420.f, -2400.f },
        { -500.f, -2400.f },
        { -380.f, -2400.f },
        { -460.f, -2400.f },
        { -540.f, -2480.f },
        { -420.f, -2320.f },
    };
    float lx, lz, dx, dz, fwd;
    int i, pass;

    if (g_idle_prop < 0 || g_idle_prop >= g_nprop)
        return;
    if (g_idle_prop == g_walk_prop)
        return;
    lx = g_prop[g_idle_prop].pos[0] - r1[0];
    lz = g_prop[g_idle_prop].pos[2] - r1[2];
    dx = lx - sx;
    dz = lz - sz;
    fwd = -dx;
    /* Already off the look ray (and not overlapping Bond). */
    if (!spawn_look_slab(lx, lz, sx, sz) &&
        !(fwd > 0.f && dz * dz < (PORT_GUARD_RADIUS + 8.f) * (PORT_GUARD_RADIUS + 8.f)))
        return;
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < (int)(sizeof pref / sizeof pref[0]); i++) {
            if (tile_taken(pref[i][0], pref[i][1], r1, g_idle_prop))
                continue;
            if (in_walk_kill_lane(pref[i][0], pref[i][1]))
                continue;
            if (pass == 0 && !floor_open(pref[i][0], pref[i][1]))
                continue;
            if (try_sit_guard(g_idle_prop, pref[i][0], pref[i][1], sx, sz, r1))
                return;
        }
    }
}

int port_prop_place_walker_near_spawn(void)
{
    float sx, sz, r1[3];
    int i, pass;
    /* Stall cubicle at -220,-2640 has G1 walls on every adjacent tile.
     * Sit a few tiles into open room-71 floor north of the hallway turn
     * so walk.png is a full figure, not a stall clip. */
    /* Ground-floor open band is z=-2640..-2480, x=-620..-60.
     * Sit on the north edge (z=-2480) west enough for a 240u east look
     * from x~-620, but north of the spawn 270 cone. */
    static const float pref[][2] = {
        { -300.f, -2480.f },
        { -380.f, -2480.f },
        { -300.f, -2560.f },
        { -380.f, -2560.f },
        { -460.f, -2480.f },
        { -220.f, -2480.f },
        { -460.f, -2560.f },
        { -300.f, -2640.f },
        { -380.f, -2640.f },
        { -220.f, -2560.f },
    };

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    /* Snapped Bond pose, not raw pad 167 (that sits 144u past the tile). */
    sx = port_player_x();
    sz = port_player_z();
    /* Stall fire-box Z-floor is measured from this first-frame spawn. */
    g_walk_spawn_x = sx;
    g_walk_spawn_z = sz;
    g_have_spawn_xz = 1;
    assign_walkers();
    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return 0;
    /* Pass 0: open floor (neighbors walkable). Pass 1: any ground tile. */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < (int)(sizeof pref / sizeof pref[0]); i++) {
            if (pass == 0 && !floor_open(pref[i][0], pref[i][1]))
                continue;
            if (try_sit_walker(pref[i][0], pref[i][1], sx, sz, r1)) {
                setup_walk_path(pref[i][0], pref[i][1], sx, sz);
                /* Closest idle stays around the corner, not in the spawn
                 * 270 look ray — spawn flash is a door/wall shot. */
                place_idle_off_spawn_look(sx, sz, r1);
                return 1;
            }
        }
    }
    place_idle_off_spawn_look(sx, sz, r1);
    return 0;
}

int port_prop_place_walker_at(float local_x, float local_z)
{
    float r1[3];
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return 0;
    return sit_guard_tile(g_walk_prop, local_x, local_z, r1);
}

int port_prop_alert_walker(void)
{
    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return 0;
    if (port_stan_guard_dead_at(g_prop[g_walk_prop].pos[0],
                                g_prop[g_walk_prop].pos[2]))
        return 0;
    g_prop[g_walk_prop].alerted = 1;
    face_player_prop(g_walk_prop);
    return 1;
}

static int mdl_is_walk(const PortModel *m)
{
    return m && m->id >= PORT_WALK_ID_BASE && m->id < PORT_DIE_ID_BASE;
}

static int mdl_is_aim(const PortModel *m)
{
    return m && m->id >= PORT_AIM_ID_BASE && m->id < PORT_AIM_ID_BASE + 256;
}

static void recount_walkers(void)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD || !mdl_is_walk(g_prop[i].mdl))
            continue;
        n++;
    }
    g_walkers = n;
}

static void apply_chr_fit(int pi, PortModel *m)
{
    const PortPropCat *cat;
    if (pi < 0 || pi >= g_nprop || !m)
        return;
    cat = chr_by_id(g_prop[pi].model);
    g_prop[pi].mdl = m;
    g_prop[pi].scale =
        (cat ? cat->scale : 1.f) * (m->fit_scale != 0.f ? m->fit_scale : 1.f);
    bind_head_off(&g_prop[pi], m);
}

/* Walk models are unique-per-body (PORT_WALK_ID_BASE + body), shared by every
 * walker of that body. Same global g_walk_rest / frame. Idle clones stay on
 * 1000+body so a walk rebind cannot flatten them. Same 185u idle fit — a
 * fresh walk AABB must not become a 1510u blob. */
static int bind_prop_walk(int pi)
{
    PortModel *wm;
    if (pi < 0 || pi >= g_nprop || !g_have_walk)
        return 0;
    if (g_prop[pi].type != PDEF_GUARD)
        return 0;
    if (port_stan_guard_dead_at(g_prop[pi].pos[0], g_prop[pi].pos[2]))
        return 0;
    if (mdl_is_walk(g_prop[pi].mdl))
        return 1;
    wm = load_chr_walk(g_prop[pi].model);
    if (!wm)
        return 0;
    if (g_walk_fit_scale != 0.f) {
        wm->fit_scale = g_walk_fit_scale;
        wm->fit_ymin = g_walk_fit_ymin;
    } else {
        g_walk_fit_scale = wm->fit_scale;
        g_walk_fit_ymin = wm->fit_ymin;
    }
    apply_chr_fit(pi, wm);
    recount_walkers();
    return 1;
}

/* Standing idle rest. Unalerted test mover stays on the walk bind so
 * ping-pong / walk.png keep a stride. */
static void bind_prop_idle(int pi)
{
    PortModel *im;
    if (pi < 0 || pi >= g_nprop || g_prop[pi].type != PDEF_GUARD)
        return;
    if (!mdl_is_walk(g_prop[pi].mdl) && !mdl_is_aim(g_prop[pi].mdl))
        return;
    if (pi == g_walk_prop && !g_prop[pi].alerted)
        return;
    im = load_chr(g_prop[pi].model);
    if (!im)
        return;
    apply_chr_fit(pi, im);
    recount_walkers();
}

/* Pack PTR_ANIM_aim_* rest for in-box living shooters. Unique
 * model id so idle clones and the walk bind stay put. If the pack aim
 * does not decode, keep idle — do not fake an arm raise. */
static int bind_prop_aim(int pi)
{
    PortModel *am;
    if (pi < 0 || pi >= g_nprop || g_prop[pi].type != PDEF_GUARD)
        return 0;
    if (port_stan_guard_dead_at(g_prop[pi].pos[0], g_prop[pi].pos[2]))
        return 0;
    if (mdl_is_aim(g_prop[pi].mdl))
        return 1;
    if (pi == g_walk_prop && !g_prop[pi].alerted)
        return 0;
    if (!g_have_aim) {
        bind_prop_idle(pi);
        return 0;
    }
    am = load_chr_aim(g_prop[pi].model);
    if (!am) {
        bind_prop_idle(pi);
        return 0;
    }
    apply_chr_fit(pi, am);
    recount_walkers();
    return 1;
}

static void apply_walk_bind(void)
{
    int i;
    const float (*save)[3];

    save = g_pose_rest;
    g_pose_rest = g_walk_rest;
    for (i = 0; i < g_nmdl; i++) {
        if (!g_mdl[i].file || !mdl_is_walk(&g_mdl[i]))
            continue;
        bind_model_gdl(&g_mdl[i], 1);
        if (g_walk_fit_scale != 0.f) {
            g_mdl[i].fit_scale = g_walk_fit_scale;
            g_mdl[i].fit_ymin = g_walk_fit_ymin;
        } else {
            g_walk_fit_scale = g_mdl[i].fit_scale;
            g_walk_fit_ymin = g_mdl[i].fit_ymin;
        }
    }
    g_pose_rest = save;
    /* Test mover tracks the walk rest while living and not holding fire-box
     * idle. Other walk-bound chasers share the same rebind. */
    if (g_walk_prop >= 0 && g_walk_prop < g_nprop &&
        !port_stan_guard_dead_at(g_prop[g_walk_prop].pos[0],
                                 g_prop[g_walk_prop].pos[2]) &&
        (mdl_is_walk(g_prop[g_walk_prop].mdl) || !g_prop[g_walk_prop].alerted))
        (void)bind_prop_walk(g_walk_prop);
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD || !mdl_is_walk(g_prop[i].mdl))
            continue;
        apply_chr_fit(i, g_prop[i].mdl);
    }
    recount_walkers();
}

static int set_walk_frame(int frame)
{
    char err[80];
    uint32_t addr = 0, off = 0, frames = 0;
    uint8_t width = 0;
    unsigned take;

    if (!g_have_walk)
        return -1;
    if (g_walk_nframes > 0) {
        int n = (int)g_walk_nframes;
        frame %= n;
        if (frame < 0)
            frame += n;
    }
    if (!decode_anim_frame(PORT_ANIM_WALK_OFF, frame, g_walk_rest, &addr, &off,
                           &frames, &width, err, sizeof err))
        return -1;
    if (frames > 0)
        g_walk_nframes = frames;
    take = (unsigned)frame;
    if (g_walk_nframes > 0 && take >= g_walk_nframes)
        take %= g_walk_nframes;
    g_walk_frame = (int)take;
    snprintf(g_walk_info, sizeof g_walk_info, "walk=1 addr=0x%x off=%u fr=%u w=%u f=%u n=%d",
             addr, off, frames, width, take, g_walkers);
    if (g_walk_prop >= 0)
        apply_walk_bind();
    return 0;
}

void port_prop_tick_walk(void)
{
    float r1[3], lx, lz, tx, tz, dx, dz, dist, step, nx, nz;

    if (!g_have_walk || g_walkers < 1 || g_walk_nframes < 1)
        return;
    if (g_walk_prop >= 0 && g_walk_prop < g_nprop &&
        port_stan_guard_dead_at(g_prop[g_walk_prop].pos[0], g_prop[g_walk_prop].pos[2]))
        return;
    set_walk_frame(g_walk_frame + 1);
    if (!g_walk_path_ok || g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    lx = g_prop[g_walk_prop].pos[0] - r1[0];
    lz = g_prop[g_walk_prop].pos[2] - r1[2];
    tx = g_walk_going_b ? g_walk_path_bx : g_walk_path_ax;
    tz = g_walk_going_b ? g_walk_path_bz : g_walk_path_az;
    dx = tx - lx;
    dz = tz - lz;
    dist = sqrtf(dx * dx + dz * dz);
    if (dist <= PORT_MOVER_ARRIVE) {
        g_walk_going_b = !g_walk_going_b;
        tx = g_walk_going_b ? g_walk_path_bx : g_walk_path_ax;
        tz = g_walk_going_b ? g_walk_path_bz : g_walk_path_az;
        dx = tx - lx;
        dz = tz - lz;
        dist = sqrtf(dx * dx + dz * dz);
    }
    if (dist < 0.001f)
        return;
    step = PORT_MOVER_WALK_STEP;
    if (step > dist)
        step = dist;
    nx = lx + dx / dist * step;
    nz = lz + dz / dist * step;
    {
        float ny = 0.f;
        port_stan_clip_step_ground(lx, lz, &nx, &nz, &ny);
    }
    if ((nx == lx && nz == lz) || !sit_walker_local(nx, nz)) {
        /* Reverse once. If the other end is also illegal, stop — do not
         * clip through a G1 wall or write a NaN Y. */
        g_walk_going_b = !g_walk_going_b;
        g_walk_blocked = 1;
        fprintf(stderr,
                "walk_step blocked local=%.1f,%.1f -> %.1f,%.1f (path %.1f,%.1f-%.1f,%.1f)\n",
                (double)lx, (double)lz, (double)nx, (double)nz,
                (double)g_walk_path_ax, (double)g_walk_path_az,
                (double)g_walk_path_bx, (double)g_walk_path_bz);
        return;
    }
    g_walk_blocked = 0;
    face_heading(dx, dz);
}

static int rooms_fire_ok(int pr, int wr)
{
    if (pr < 1 || wr < 1)
        return 0;
    if (pr == wr)
        return 1;
    return port_stage_rooms_adjacent(pr, wr);
}

/* yaw 0 faces +Z; same atan2(dx,dz) as face_heading_prop. */
static int guard_facing_player(int pi, float dx, float dz, float dist)
{
    float yaw, fx, fz, dot;
    if (pi < 0 || pi >= g_nprop)
        return 0;
    if (dist < 1e-6f)
        return 1;
    yaw = g_prop[pi].yaw * (PI_F / 180.f);
    fx = sinf(yaw);
    fz = cosf(yaw);
    dot = (fx * dx + fz * dz) / dist;
    return dot >= PORT_GUARD_FACE_DOT;
}

static int guard_prop_in_los(int pi, float *dx_out, float *dz_out, float *dist_out)
{
    float r1[3], lx, ly, lz, px, py, pz, dx, dz, dist;
    int pr, wr;

    if (pi < 0 || pi >= g_nprop || g_prop[pi].type != PDEF_GUARD)
        return 0;
    if (port_stan_guard_dead_at(g_prop[pi].pos[0], g_prop[pi].pos[2]))
        return 0;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    lx = g_prop[pi].pos[0] - r1[0];
    ly = g_prop[pi].pos[1] - r1[1];
    lz = g_prop[pi].pos[2] - r1[2];
    px = port_player_x();
    py = port_player_y();
    pz = port_player_z();
    dx = px - lx;
    dz = pz - lz;
    dist = sqrtf(dx * dx + dz * dz);
    if (dist < 40.f || dist > PORT_GUARD_FIRE_RANGE)
        return 0;
    if (fabsf(pz - lz) > PORT_GUARD_FIRE_ALONG)
        return 0;
    pr = port_stage_room_at_local(px, py, pz);
    wr = port_stage_room_at_local(lx, ly, lz);
    if (!rooms_fire_ok(pr, wr))
        return 0;
    {
        float inv, ox, oy, oz, ddx, ddy, ddz, tblk, len;
        inv = 1.f / dist;
        ddx = dx * inv;
        ddz = dz * inv;
        ddy = (py - (ly + PORT_GUARD_AIM_Y)) / dist;
        ox = lx + ddx * PORT_GUARD_AIM_OFFSET;
        oy = ly + PORT_GUARD_AIM_Y;
        oz = lz + ddz * PORT_GUARD_AIM_OFFSET;
        len = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
        if (len < 1e-6f)
            return 0;
        ddx /= len;
        ddy /= len;
        ddz /= len;
        if (port_stan_ray_block(ox, oy, oz, ddx, ddy, ddz, &tblk) &&
            tblk < dist - PORT_GUARD_AIM_OFFSET)
            return 0;
    }
    /* Unalerted: only the front cone. Alerted already turned to chase. */
    if (!g_prop[pi].alerted && !guard_facing_player(pi, dx, dz, dist))
        return 0;
    if (dx_out)
        *dx_out = dx;
    if (dz_out)
        *dz_out = dz;
    if (dist_out)
        *dist_out = dist;
    return 1;
}

static int fire_guard_hitscan(int pi, float dist)
{
    float r1[3], lx, ly, lz, px, py, pz, ox, oy, oz, ddx, ddy, ddz, inv, len, t;

    if (dist < 1e-6f)
        return 0;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    lx = g_prop[pi].pos[0] - r1[0];
    ly = g_prop[pi].pos[1] - r1[1];
    lz = g_prop[pi].pos[2] - r1[2];
    px = port_player_x();
    py = port_player_y();
    pz = port_player_z();
    inv = 1.f / dist;
    ddx = (px - lx) * inv;
    ddz = (pz - lz) * inv;
    ddy = (py - (ly + PORT_GUARD_AIM_Y)) / dist;
    ox = lx + ddx * PORT_GUARD_AIM_OFFSET;
    oy = ly + PORT_GUARD_AIM_Y;
    oz = lz + ddz * PORT_GUARD_AIM_OFFSET;
    len = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
    if (len < 1e-6f)
        return 0;
    ddx /= len;
    ddy /= len;
    ddz /= len;
    if (port_player_ray_hit(ox, oy, oz, ddx, ddy, ddz, &t))
        port_player_damage(PORT_GUARD_FIRE_DAMAGE);
    return 1;
}

/* Spawn stall fire box: xz 40-400 from first-frame spawn, |Δz|<=200.
 * Chasers may close toward the hallway corner but must not come down
 * the 270 hall into this box (that would drop spawn hp). */
static int in_spawn_fire_box(float lx, float lz)
{
    float dx, dz, dist;

    if (!g_have_spawn_xz)
        return 0;
    dx = g_walk_spawn_x - lx;
    dz = g_walk_spawn_z - lz;
    dist = sqrtf(dx * dx + dz * dz);
    if (dist < 40.f || dist > PORT_GUARD_FIRE_RANGE)
        return 0;
    if (fabsf(dz) > PORT_GUARD_FIRE_ALONG)
        return 0;
    return 1;
}

/* Clip a proposed chase dest with the same rules as the player
 * (Rare point.link walls, lowest overlapping floor, closed doors).
 * Refuse the spawn stall fire box (Z-floor). */
static int try_chase_sit(int pi, float ox, float oz, float nx, float nz,
                         const float r1[3])
{
    float cx = nx, cz = nz, ny = 0.f;
    port_stan_clip_step_ground(ox, oz, &cx, &cz, &ny);
    if (in_spawn_fire_box(cx, cz))
        return 0;
    if (cx == ox && cz == oz)
        return 0;
    if (!sit_guard_tile(pi, cx, cz, r1))
        return 0;
    face_heading_prop(pi, cx - ox, cz - oz);
    return 1;
}

/* One 3.0u step toward (px,pz) on ground-floor tiles. Face the step.
 * Honor Rare point.link walls (Facility stall G1 sits between tiles).
 * Refuse the spawn stall fire box (Z-floor). Slide on X then Z if the
 * diagonal is a wall so a G1 edge does not freeze them. Already inside
 * a wall: clip_step snaps onto the nearest walkable floor tile. */
static int chase_step(int pi, float lx, float lz, float px, float pz, const float r1[3])
{
    float dx, dz, dist, step, nx, nz;

    dx = px - lx;
    dz = pz - lz;
    dist = sqrtf(dx * dx + dz * dz);
    /* Already inside min fire range — do not walk through the player. */
    if (dist < 40.f)
        return 0;
    step = PORT_MOVER_WALK_STEP;
    if (step > dist)
        step = dist;
    nx = lx + dx / dist * step;
    nz = lz + dz / dist * step;
    if (try_chase_sit(pi, lx, lz, nx, nz, r1))
        return 1;
    if (try_chase_sit(pi, lx, lz, nx, lz, r1))
        return 1;
    if (try_chase_sit(pi, lx, lz, lx, nz, r1))
        return 1;
    /* Closed door is the only block: same Z-use, never a G1 wall.
     * Collision drops on the use tick so the next sit can pass. */
    if (port_stan_door_blocks_only(lx, lz, nx, nz) &&
        port_stan_unlatch_closed(lx, lz, dx, dz)) {
        if (try_chase_sit(pi, lx, lz, nx, nz, r1))
            return 1;
        if (try_chase_sit(pi, lx, lz, nx, lz, r1))
            return 1;
        if (try_chase_sit(pi, lx, lz, lx, nz, r1))
            return 1;
    }
    return 0;
}

/* Alerted living guards that cannot shoot yet walk toward the player.
 * Unalerted stay put. In-box guards use return-fire (no step). Stepping
 * (or already walk-bound) chasers play PTR_ANIM_walking; sim_tick skips
 * the test-mover ping-pong once the walker is alerted, so the pose tick
 * lives here. */
static void chase_alerted(void)
{
    int i, stepped = 0, walking = 0;
    float r1[3], px, pz;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    px = port_player_x();
    pz = port_player_z();
    for (i = 0; i < g_nprop; i++) {
        float lx, lz;
        if (g_prop[i].type != PDEF_GUARD || !g_prop[i].alerted)
            continue;
        if (port_stan_guard_dead_at(g_prop[i].pos[0], g_prop[i].pos[2]))
            continue;
        if (guard_prop_in_los(i, NULL, NULL, NULL))
            continue;
        lx = g_prop[i].pos[0] - r1[0];
        lz = g_prop[i].pos[2] - r1[2];
        if (chase_step(i, lx, lz, px, pz, r1)) {
            (void)bind_prop_walk(i);
            stepped = 1;
        }
        if (mdl_is_walk(g_prop[i].mdl))
            walking = 1;
    }
    if ((stepped || walking) && g_have_walk && g_walk_nframes > 0)
        set_walk_frame(g_walk_frame + 1);
}

int port_prop_tick_guard_fire(void)
{
    int i, combat = 0, fired = 0;

    if (g_fire_cd > 0)
        g_fire_cd--;
    g_guard_los = 0;
    for (i = 0; i < g_nprop; i++) {
        float dx, dz, dist;
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        if (!guard_prop_in_los(i, &dx, &dz, &dist)) {
            g_prop[i].notice = 0;
            /* Alerted: face the player even outside the fire box.
             * Do not set combat — they cannot shoot yet. */
            if (g_prop[i].alerted &&
                !port_stan_guard_dead_at(g_prop[i].pos[0], g_prop[i].pos[2]))
                face_player_prop(i);
            continue;
        }
        /* In the fire box: idle rest. Pack aim/fire decodes but the
         * 16-joint Euler bind is a blob — do not apply it. */
        bind_prop_idle(i);
        g_guard_los++;
        combat = 1;
        face_heading_prop(i, dx, dz);
        if (!g_prop[i].alerted) {
            g_prop[i].notice += 1;
            if (g_prop[i].notice < PORT_GUARD_NOTICE_TICKS)
                continue;
            g_prop[i].alerted = 1;
        }
        if (g_fire_cd > 0)
            continue;
        if (fire_guard_hitscan(i, dist)) {
            g_guard_shots += 1;
            fired = 1;
        }
    }
    if (fired)
        g_fire_cd = PORT_GUARD_FIRE_COOLDOWN;
    chase_alerted();
    return combat;
}

void port_prop_hear_player_shot(void)
{
    int i;
    float r1[3], px, py, pz;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    px = port_player_x();
    py = port_player_y();
    pz = port_player_z();
    for (i = 0; i < g_nprop; i++) {
        float lx, ly, lz, dx, dz, dist;
        int pr, wr;
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        if (port_stan_guard_dead_at(g_prop[i].pos[0], g_prop[i].pos[2]))
            continue;
        lx = g_prop[i].pos[0] - r1[0];
        ly = g_prop[i].pos[1] - r1[1];
        lz = g_prop[i].pos[2] - r1[2];
        dx = px - lx;
        dz = pz - lz;
        dist = sqrtf(dx * dx + dz * dz);
        if (dist > PORT_GUARD_HEAR_RANGE)
            continue;
        pr = port_stage_room_at_local(px, py, pz);
        wr = port_stage_room_at_local(lx, ly, lz);
        if (!rooms_fire_ok(pr, wr))
            continue;
        g_prop[i].alerted = 1;
        face_heading_prop(i, dx, dz);
    }
}

int port_prop_guard_alerted(void)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD || !g_prop[i].alerted)
            continue;
        if (port_stan_guard_dead_at(g_prop[i].pos[0], g_prop[i].pos[2]))
            continue;
        n++;
    }
    return n;
}

int port_prop_walker_alerted(void)
{
    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return 0;
    if (!g_prop[g_walk_prop].alerted)
        return 0;
    if (port_stan_guard_dead_at(g_prop[g_walk_prop].pos[0], g_prop[g_walk_prop].pos[2]))
        return 0;
    return 1;
}

int port_prop_guard_yaw(int want, float *yaw, int *alerted)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        if (n == want) {
            if (yaw)
                *yaw = g_prop[i].yaw;
            if (alerted)
                *alerted = g_prop[i].alerted;
            return 0;
        }
        n++;
    }
    return -1;
}

int port_prop_guard_shots(void) { return g_guard_shots; }

int port_prop_guard_los(void) { return g_guard_los; }

int port_prop_walk_path(float *ax, float *az, float *bx, float *bz)
{
    if (!g_walk_path_ok)
        return -1;
    if (ax)
        *ax = g_walk_path_ax;
    if (az)
        *az = g_walk_path_az;
    if (bx)
        *bx = g_walk_path_bx;
    if (bz)
        *bz = g_walk_path_bz;
    return 0;
}

float port_prop_walk_speed(void)
{
    return g_walk_path_ok ? PORT_MOVER_WALK_STEP : 0.f;
}

void port_prop_set_walk_frame(int frame)
{
    if (!g_have_walk)
        return;
    set_walk_frame(frame);
}

int port_prop_walk_frame(void)
{
    if (!g_have_walk || g_walkers < 1)
        return -1;
    return g_walk_frame;
}

static int guard_prop_at(int want)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        if (n == want)
            return i;
        n++;
    }
    return -1;
}

int port_prop_idle_guard(void)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        if (i == g_idle_prop)
            return n;
        n++;
    }
    return -1;
}

int port_prop_guard_walk_bound(int want)
{
    int pi = guard_prop_at(want);
    if (pi < 0)
        return 0;
    return mdl_is_walk(g_prop[pi].mdl);
}

int port_prop_guard_aim_bound(int want)
{
    int pi = guard_prop_at(want);
    if (pi < 0)
        return 0;
    return mdl_is_aim(g_prop[pi].mdl);
}

float port_prop_guard_fit_scale(int want)
{
    int pi = guard_prop_at(want);
    if (pi < 0 || !g_prop[pi].mdl)
        return 0.f;
    return g_prop[pi].mdl->fit_scale;
}

int port_prop_guard_have_head(int want)
{
    int pi = guard_prop_at(want);
    if (pi < 0)
        return 0;
    return g_prop[pi].head && g_prop[pi].head->npart &&
           (g_prop[pi].mdl && g_prop[pi].mdl->have_head);
}

int port_prop_guard_head_off(int want, float *x, float *y, float *z)
{
    int pi = guard_prop_at(want);
    if (pi < 0 || !g_prop[pi].mdl || !g_prop[pi].mdl->have_head)
        return -1;
    if (x)
        *x = g_prop[pi].head_off[0];
    if (y)
        *y = g_prop[pi].head_off[1];
    if (z)
        *z = g_prop[pi].head_off[2];
    return 0;
}

static uint32_t rest_crc32(const float rest[PORT_SKEL_GUARD_N][3])
{
    const uint8_t *p = (const uint8_t *)rest;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i, n = sizeof(float) * (size_t)PORT_SKEL_GUARD_N * 3u;
    for (i = 0; i < n; i++)
        crc = (crc >> 8) ^ ((crc ^ p[i]) * 16777619u);
    return crc ^ 0xFFFFFFFFu;
}

uint32_t port_prop_walk_rest_crc(void)
{
    return rest_crc32(g_walk_rest);
}

uint32_t port_prop_idle_rest_crc(void)
{
    return rest_crc32(g_idle_rest);
}

uint32_t port_prop_aim_rest_crc(void)
{
    return rest_crc32(g_aim_rest);
}

static int any_guard_dead(void)
{
    int i;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        if (port_stan_guard_dead_at(g_prop[i].pos[0], g_prop[i].pos[2]))
            return 1;
    }
    return 0;
}

/* Rebind an already-loaded unique death model. Restore the standing fit so a
 * lying AABB cannot become a 1510u blob. Head follows the death neck. */
static int apply_die_bind(PortModel *m)
{
    const float (*save)[3];
    float fs, fy;

    if (!m || !m->file)
        return -1;
    fs = m->fit_scale;
    fy = m->fit_ymin;
    save = g_pose_rest;
    g_pose_rest = g_die_rest;
    bind_model_gdl(m, 1);
    if (fs != 0.f) {
        m->fit_scale = fs;
        m->fit_ymin = fy;
    } else if (g_walk_fit_scale != 0.f) {
        m->fit_scale = g_walk_fit_scale;
        m->fit_ymin = g_walk_fit_ymin;
    }
    g_pose_rest = save;
    return m->npart ? 0 : -1;
}

static int rebind_die_models(void)
{
    int i, rc = 0;
    for (i = 0; i < g_nmdl; i++) {
        if (g_mdl[i].id < PORT_DIE_ID_BASE ||
            g_mdl[i].id >= PORT_DIE_ID_BASE + 256 || !g_mdl[i].file)
            continue;
        if (apply_die_bind(&g_mdl[i]) != 0)
            rc = -1;
    }
    return rc;
}

static int set_die_frame(int frame)
{
    char err[80];
    uint32_t addr = 0, off = 0, frames = 0;
    uint8_t width = 0;
    int last;
    float prev[PORT_SKEL_GUARD_N][3];

    if (!g_have_die || g_die_off == 0)
        return -1;
    last = (g_die_nframes > 1u) ? (int)g_die_nframes - 1 : 0;
    if (frame < 0)
        frame = 0;
    if (frame > last)
        frame = last;
    memcpy(prev, g_die_rest, sizeof prev);
    if (!decode_anim_frame(g_die_off, frame, g_die_rest, &addr, &off, &frames, &width,
                           err, sizeof err)) {
        memcpy(g_die_rest, prev, sizeof g_die_rest);
        return -1;
    }
    if (frames > 0)
        g_die_nframes = frames;
    g_die_frame = frame;
    snprintf(g_die_info, sizeof g_die_info, "die=1 addr=0x%x off=%u fr=%u w=%u f=%d",
             addr, off, frames, width, frame);
    if (rebind_die_models() != 0) {
        /* Ticking wrecked the unique-model bind: freeze on the last-frame hold. */
        memcpy(g_die_rest, g_die_hold, sizeof g_die_rest);
        g_die_frame = last;
        g_die_tick_ok = 0;
        snprintf(g_die_info, sizeof g_die_info, "die=1 addr=0x%x off=%u fr=%u w=%u f=%d hold",
                 addr, off, frames, width, last);
        (void)rebind_die_models();
        return -1;
    }
    return 0;
}

static void start_die_if_needed(void)
{
    if (!g_have_die || g_die_started || !g_die_tick_ok)
        return;
    if (!any_guard_dead())
        return;
    maybe_spawn_death_drops();
    g_die_started = 1;
    if (set_die_frame(0) != 0) {
        int last = (g_die_nframes > 1u) ? (int)g_die_nframes - 1 : 0;
        memcpy(g_die_rest, g_die_hold, sizeof g_die_rest);
        g_die_frame = last;
        g_die_tick_ok = 0;
        (void)rebind_die_models();
    }
}

void port_prop_tick_die(void)
{
    int last;
    int next;

    maybe_spawn_death_drops();
    if (!g_have_die || !g_die_tick_ok)
        return;
    start_die_if_needed();
    if (!g_die_started)
        return;
    last = (g_die_nframes > 1u) ? (int)g_die_nframes - 1 : 0;
    if (g_die_frame < last) {
        next = g_die_frame + PORT_DIE_FRAMES_PER_TICK;
        if (next > last)
            next = last;
        (void)set_die_frame(next);
    }
}

void port_prop_set_die_frame(int frame)
{
    maybe_spawn_death_drops();
    if (!g_have_die)
        return;
    g_die_started = 1;
    (void)set_die_frame(frame);
}

int port_prop_die_frame(void)
{
    if (!g_have_die)
        return -1;
    return g_die_frame;
}

int port_prop_die_last_frame(void)
{
    if (!g_have_die)
        return -1;
    return (g_die_nframes > 1u) ? (int)g_die_nframes - 1 : 0;
}

uint32_t port_prop_die_rest_crc(void)
{
    const uint8_t *p = (const uint8_t *)g_die_rest;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i, n = sizeof g_die_rest;
    for (i = 0; i < n; i++)
        crc = (crc >> 8) ^ ((crc ^ p[i]) * 16777619u);
    return crc ^ 0xFFFFFFFFu;
}

int port_prop_walk_xyz(float *x, float *y, float *z)
{
    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return -1;
    if (x)
        *x = g_prop[g_walk_prop].pos[0];
    if (y)
        *y = g_prop[g_walk_prop].pos[1];
    if (z)
        *z = g_prop[g_walk_prop].pos[2];
    return 0;
}

static const char *setup_name(int level_id)
{
    if (level_id == PORT_LEVEL_FACILITY)
        return "assets/obseg/setup/UsetuparkZ.bin";
    if (level_id == PORT_LEVEL_FACILITY_MP)
        return "assets/obseg/setup/Ump_setuparkZ.bin";
    return NULL;
}

static float yaw_from_look(float lx, float lz)
{
    if (lx * lx + lz * lz < 1e-8f)
        return 0.f;
    return atan2f(lx, lz) * (180.f / PI_F);
}

static const uint8_t *pad_bytes(const uint8_t *st, size_t n, uint32_t pad_off,
                                uint32_t bound_off, int npad, int nbound, int pad)
{
    size_t off;
    if (pad >= PORT_BOUND_BASE) {
        pad -= PORT_BOUND_BASE;
        if (pad < 0 || pad >= nbound || !bound_off)
            return NULL;
        off = (size_t)bound_off + (size_t)pad * PORT_BOUND_BYTES;
        if (off + PORT_PAD_BYTES > n)
            return NULL;
        return st + off;
    }
    /* Negative / attached: no object-parent walk. Facility intro is pad 167. */
    if (pad < 0 || pad >= npad || !pad_off)
        return NULL;
    off = (size_t)pad_off + (size_t)pad * PORT_PAD_BYTES;
    if (off + PORT_PAD_BYTES > n)
        return NULL;
    return st + off;
}

static void parse_intro(const uint8_t *st, size_t n, uint32_t pad_off, uint32_t bound_off,
                        int npad, int nbound)
{
    uint32_t ioff;
    const uint8_t *p;
    int chosen = -1, first = -1;

    g_have_intro = 0;
    g_intro_pad = -1;
    g_nintro = 0;
    if (n < PORT_SETUP_PTRS * 4)
        return;
    ioff = be32(st + 8);
    if (!ioff || ioff >= n)
        return;
    p = st + ioff;
    while (p + 4 <= st + n) {
        int type = (int)be32(p);
        uint16_t words;
        size_t bytes;
        if (type == INTRO_END)
            break;
        if (type < 0 || type >= INTRO_END)
            break;
        words = k_intro_words[type];
        bytes = (size_t)words * 4u;
        if (p + bytes > st + n)
            break;
        if (type == INTRO_SPAWN && bytes >= 12) {
            int pad = (int)be32(p + 4);
            int demo = (int)be32(p + 8);
            if (first < 0)
                first = pad;
            if (demo == 0 && chosen < 0)
                chosen = pad;
            if (demo == 0 && g_nintro < PORT_INTRO_MAX)
                g_intro_pads[g_nintro++] = pad;
        }
        p += bytes;
    }
    if (chosen < 0)
        chosen = first;
    if (g_nintro <= 0 && chosen >= 0) {
        g_intro_pads[0] = chosen;
        g_nintro = 1;
    }
    {
        int i, kept = 0;
        for (i = 0; i < g_nintro; i++) {
            const uint8_t *pd = pad_bytes(st, n, pad_off, bound_off, npad, nbound,
                                          g_intro_pads[i]);
            if (!pd)
                continue;
            g_intro_pads[kept] = g_intro_pads[i];
            g_intro_xyz[kept][0] = be_f32(pd);
            g_intro_xyz[kept][1] = be_f32(pd + 4);
            g_intro_xyz[kept][2] = be_f32(pd + 8);
            g_intro_looks[kept][0] = be_f32(pd + 24);
            g_intro_looks[kept][1] = be_f32(pd + 28);
            g_intro_looks[kept][2] = be_f32(pd + 32);
            kept++;
        }
        g_nintro = kept;
    }
    if (g_nintro > 0) {
        g_intro_pad = g_intro_pads[0];
        g_intro_pos[0] = g_intro_xyz[0][0];
        g_intro_pos[1] = g_intro_xyz[0][1];
        g_intro_pos[2] = g_intro_xyz[0][2];
        g_intro_look[0] = g_intro_looks[0][0];
        g_intro_look[1] = g_intro_looks[0][1];
        g_intro_look[2] = g_intro_looks[0][2];
        g_have_intro = 1;
    }
}

static int pickup_skip_model(int model)
{
    return model == 86 || model == 234 || model == 243 || model == 244;
}

static int pickup_kind_of(int type, int model)
{
    if (type == PDEF_ARMOUR || model == 115 || model == 116)
        return PORT_PICKUP_ARMOUR;
    if (type == PDEF_MAGAZINE || (model >= 121 && model <= 134) ||
        (model >= 3 && model <= 7))
        return PORT_PICKUP_AMMO;
    if (type == PDEF_COLLECTABLE && !pickup_skip_model(model))
        return PORT_PICKUP_AMMO;
    return 0;
}

static int pickup_amount_of(int kind, int amount_raw)
{
    if (kind == PORT_PICKUP_ARMOUR) {
        if (amount_raw >= 65536)
            return PORT_PLAYER_ARMOUR_MAX;
        return PORT_PLAYER_ARMOUR_MAX / 2;
    }
    return PORT_PICKUP_AMMO_ADD;
}

static void record_pickup_cand(int type, int model, int pad, const uint8_t *pd, int amount_raw,
                               int orig_pad, unsigned flags)
{
    PortPickupCand *c;
    if (g_npcand >= PORT_PICKUP_MAX_CAND || !pd || pad < 0)
        return;
    if (pickup_skip_model(model) || type == PDEF_KEY)
        return;
    if (!pickup_kind_of(type, model))
        return;
    c = &g_pcand[g_npcand++];
    memset(c, 0, sizeof *c);
    c->type = type;
    c->model = model;
    c->pad = pad;
    c->orig_pad = orig_pad;
    c->flags = flags;
    c->pos[0] = be_f32(pd);
    c->pos[1] = be_f32(pd + 4);
    c->pos[2] = be_f32(pd + 8);
    c->look[0] = be_f32(pd + 24);
    c->look[1] = be_f32(pd + 28);
    c->look[2] = be_f32(pd + 32);
    c->amount_raw = amount_raw;
}

static int load_one_pickup(const PortPickupCand *c)
{
    PortProp *pr;
    PortModel *mdl;
    const PortPropCat *cat;
    int kind;
    if (!c || g_nprop >= PORT_MAX_PROPS)
        return -1;
    kind = pickup_kind_of(c->type, c->model);
    if (!kind)
        return -1;
    cat = cat_by_id(c->model);
    mdl = load_model(c->model);
    if (!mdl)
        return -1;
    pr = &g_prop[g_nprop];
    memset(pr, 0, sizeof *pr);
    pr->type = c->type;
    pr->model = c->model;
    pr->pad = c->pad;
    pr->pos[0] = c->pos[0];
    pr->pos[1] = c->pos[1];
    pr->pos[2] = c->pos[2];
    pr->look[0] = c->look[0];
    pr->look[1] = c->look[1];
    pr->look[2] = c->look[2];
    pr->scale = cat ? cat->scale : 0.1f;
    pr->yaw = yaw_from_look(pr->look[0], pr->look[2]);
    pr->mdl = mdl;
    pr->hidden = 0;
    pr->pickup_kind = kind;
    pr->pickup_amount = pickup_amount_of(kind, c->amount_raw);
    {
        float r1[3], ey = 0.f, lx, lz;
        r1[0] = r1[1] = r1[2] = 0.f;
        (void)port_stage_room1(r1);
        lx = pr->pos[0] - r1[0];
        lz = pr->pos[2] - r1[2];
        if (port_stan_eye_y(lx, lz, &ey) == 0 ||
            port_stan_nearest_eye_y(lx, lz, PORT_STAN_NEAR_XZ, &ey) == 0)
            pr->pos[1] = r1[1] + (ey - PORT_EYE_HEIGHT);
    }
    g_pickup_prop = g_nprop;
    g_nprop++;
    return 0;
}

static int cand_rank(const PortPickupCand *c, int ground, int on, int near)
{
    int rank;
    if (c->type == PDEF_ARMOUR)
        rank = 0;
    else if (c->type == PDEF_MAGAZINE)
        rank = 1;
    else
        rank = 2;
    if (!ground)
        rank += 10;
    if (!on && !near)
        rank += 10;
    return rank;
}

static void choose_pickup(void)
{
    float r1[3], sx, sz;
    int i, best = -1, best_rank = 99;
    float best_d = 1e18f;

    g_pickup_prop = -1;
    g_pickup_drawn = 0;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    sx = port_player_x();
    sz = port_player_z();
    if (g_npcand > 0)
        printf("pickup_dump n=%d spawn_local=%.1f,%.1f intro_pad=%d\n", g_npcand,
               (double)sx, (double)sz, g_intro_pad);
    for (i = 0; i < g_npcand; i++) {
        const PortPickupCand *c = &g_pcand[i];
        float lx = c->pos[0] - r1[0];
        float ly = c->pos[1] - r1[1];
        float lz = c->pos[2] - r1[2];
        float dx = lx - sx, dz = lz - sz;
        float dist = sqrtf(dx * dx + dz * dz);
        float ey = 0.f;
        int on, near, ground, skip, kind, rank;
        const char *why = "ok";

        on = port_stan_on_tile(lx, lz);
        near = (port_stan_nearest_eye_y(lx, lz, PORT_STAN_NEAR_XZ, &ey) == 0);
        if (!near)
            ey = 0.f;
        ground = near && fabsf(ey - PORT_PICKUP_GROUND_EYE) <= PORT_PICKUP_GROUND_SLACK;
        kind = pickup_kind_of(c->type, c->model);
        skip = 0;
        /* 0x8000 INSIDEANOTHEROBJ / orig pad -1: magazines and desk KF7s.
         * 0x4000 ASSIGNEDTOCHR: guard-held KF7 / MP5K / swipe cards. */
        if (c->orig_pad < 0 || (c->flags & 0x00008000u)) {
            skip = 1;
            why = "embedded";
        } else if (c->flags & 0x00004000u) {
            skip = 1;
            why = "assigned";
        } else if (dist < PORT_PICKUP_SLAB) {
            skip = 1;
            why = "slab";
        } else if (dist <= PORT_GUARD_FIRE_RANGE && fabsf(dz) <= PORT_GUARD_FIRE_ALONG) {
            skip = 1;
            why = "firebox";
        }
        if (skip || !kind)
            continue;
        rank = cand_rank(c, ground, on, near);
        if (rank < best_rank || (rank == best_rank && dist < best_d)) {
            best_rank = rank;
            best_d = dist;
            best = i;
        }
    }
    if (best < 0) {
        printf("pickup_pick NONE\n");
        return;
    }
    if (load_one_pickup(&g_pcand[best]) != 0) {
        int saved = best;
        best = -1;
        for (i = 0; i < g_npcand; i++) {
            const PortPickupCand *c = &g_pcand[i];
            if (i == saved)
                continue;
            if (c->orig_pad < 0 || (c->flags & 0x0000c000u))
                continue;
            if (load_one_pickup(c) == 0) {
                best = i;
                break;
            }
        }
        if (best < 0) {
            printf("pickup_pick NONE model\n");
            return;
        }
    }
    {
        const PortPickupCand *c = &g_pcand[best];
        PortProp *pr = &g_prop[g_pickup_prop];
        printf("pickup_pick pad=%d type=%d model=%d kind=%d amount=%d "
               "world=%.1f,%.1f,%.1f local=%.1f,%.1f rank=%d dist=%.1f\n",
               c->pad, c->type, c->model, pr->pickup_kind, pr->pickup_amount,
               (double)c->pos[0], (double)c->pos[1], (double)c->pos[2],
               (double)(c->pos[0] - r1[0]), (double)(c->pos[2] - r1[2]),
               best_rank, (double)best_d);
    }
}

static int on_spawn_look_slab(float lx, float lz)
{
    float sx, sz, dx, dz, fwd, dist;

    if (g_have_spawn_xz) {
        sx = g_walk_spawn_x;
        sz = g_walk_spawn_z;
    } else {
        sx = port_player_x();
        sz = port_player_z();
    }
    dx = lx - sx;
    dz = lz - sz;
    dist = sqrtf(dx * dx + dz * dz);
    if (dist < PORT_PICKUP_SLAB)
        return 1;
    /* Same cone as walker_offslab / spawn look 270 (-X). */
    fwd = -dx;
    if (dx <= 40.f) {
        if (fwd < 80.f && dist < 180.f)
            return 1;
        if (fwd > 0.f && fwd < 700.f &&
            dz * dz < (0.65f * fwd) * (0.65f * fwd))
            return 1;
    }
    return 0;
}

/* Floor collectable at world xz. Does not touch g_pickup_prop (vest 215). */
static int spawn_floor_collectable(int model, int kind, float world_x, float world_z)
{
    PortProp *pr;
    PortModel *mdl;
    const PortPropCat *cat;
    float r1[3], ey = 0.f, lx, lz;

    if (g_nprop >= PORT_MAX_PROPS || kind == 0)
        return -1;
    cat = cat_by_id(model);
    mdl = load_model(model);
    if (!mdl)
        return -1;
    pr = &g_prop[g_nprop];
    memset(pr, 0, sizeof *pr);
    pr->type = PDEF_COLLECTABLE;
    pr->model = model;
    pr->pad = -1;
    pr->chrnum = -1;
    pr->pos[0] = world_x;
    pr->pos[2] = world_z;
    pr->look[2] = -1.f;
    pr->scale = cat ? cat->scale : 0.1f;
    pr->yaw = yaw_from_look(pr->look[0], pr->look[2]);
    pr->mdl = mdl;
    pr->pickup_kind = kind;
    pr->pickup_amount = pickup_amount_of(kind, 0);
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    lx = pr->pos[0] - r1[0];
    lz = pr->pos[2] - r1[2];
    if (port_stan_eye_y(lx, lz, &ey) == 0 ||
        port_stan_nearest_eye_y(lx, lz, PORT_STAN_NEAR_XZ, &ey) == 0)
        pr->pos[1] = r1[1] + (ey - PORT_EYE_HEIGHT);
    return g_nprop++;
}

static int spawn_death_drop_at(PortProp *guard)
{
    float r1[3], lx, lz;
    int pi;

    if (!guard || guard->held_model <= 0 || guard->dropped)
        return -1;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    lx = guard->pos[0] - r1[0];
    lz = guard->pos[2] - r1[2];
    if (on_spawn_look_slab(lx, lz)) {
        printf("drop_skip chr=%d model=%d slab local=%.1f,%.1f\n",
               guard->chrnum, guard->held_model, (double)lx, (double)lz);
        guard->dropped = 1;
        return -1;
    }
    if (!port_stan_on_tile(lx, lz)) {
        float ny = 0.f;
        if (port_stan_snap_walkable(&lx, &lz, 0.f, 1.f, PORT_STAN_NEAR_XZ, &ny) != 0) {
            printf("drop_skip chr=%d model=%d off_tile\n",
                   guard->chrnum, guard->held_model);
            guard->dropped = 1;
            return -1;
        }
    }
    pi = spawn_floor_collectable(guard->held_model,
                                 pickup_kind_of(PDEF_COLLECTABLE, guard->held_model),
                                 r1[0] + lx, r1[2] + lz);
    guard->dropped = 1;
    if (pi < 0) {
        printf("drop_skip chr=%d model=%d bind\n",
               guard->chrnum, guard->held_model);
        return -1;
    }
    if (g_ndrop < PORT_DROP_MAX)
        g_drops[g_ndrop++] = pi;
    {
        PortProp *pr = &g_prop[pi];
        printf("drop_spawn chr=%d model=%d world=%.1f,%.1f,%.1f local=%.1f,%.1f\n",
               guard->chrnum, pr->model,
               (double)pr->pos[0], (double)pr->pos[1], (double)pr->pos[2],
               (double)(pr->pos[0] - r1[0]), (double)(pr->pos[2] - r1[2]));
    }
    return 0;
}

static void maybe_spawn_death_drops(void)
{
    int i;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD || !g_prop[i].mdl)
            continue;
        if (g_prop[i].dropped || g_prop[i].held_model <= 0)
            continue;
        if (!port_stan_guard_dead_at(g_prop[i].pos[0], g_prop[i].pos[2]))
            continue;
        (void)spawn_death_drop_at(&g_prop[i]);
    }
}

static void bind_assigned_guns(void)
{
    int i, nheld = 0;
    for (i = 0; i < g_nprop; i++) {
        int cn;
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        cn = g_prop[i].chrnum;
        if (cn >= 0 && cn < PORT_CHR_GUN_MAX && g_chr_gun[cn] > 0)
            g_prop[i].held_model = g_chr_gun[cn];
        if (g_prop[i].held_model > 0)
            nheld++;
    }
    printf("held_gun n=%d models=%d\n", nheld, g_nmdl);
}

static void fill_pad_prop(PortProp *pr, int type, int model, int pad, const uint8_t *pd,
                          float scale)
{
    memset(pr, 0, sizeof *pr);
    pr->type = type;
    pr->model = model;
    pr->pad = pad;
    pr->chrnum = -1;
    pr->pos[0] = be_f32(pd);
    pr->pos[1] = be_f32(pd + 4);
    pr->pos[2] = be_f32(pd + 8);
    pr->look[0] = be_f32(pd + 24);
    pr->look[1] = be_f32(pd + 28);
    pr->look[2] = be_f32(pd + 32);
    pr->scale = scale;
    pr->yaw = yaw_from_look(pr->look[0], pr->look[2]);
}

static int parse_setup(const uint8_t *st, size_t n)
{
    uint32_t poff, pad_off, pad3_off, names_off;
    const uint8_t *p;
    int npad, nbound;

    g_nprop = 0;
    g_have_intro = 0;
    g_intro_pad = -1;
    g_nintro = 0;
    g_npcand = 0;
    g_pickup_prop = -1;
    g_pickup_drawn = 0;
    g_ndrop = 0;
    memset(g_drops, 0, sizeof g_drops);
    g_drop_drawn = 0;
    memset(g_pcand, 0, sizeof g_pcand);
    memset(g_chr_gun, 0, sizeof g_chr_gun);
    if (n < PORT_SETUP_PTRS * 4)
        return -1;
    poff = be32(st + 12);
    pad_off = be32(st + 24);
    pad3_off = be32(st + 28);
    names_off = be32(st + 32);
    if (!poff || poff >= n)
        return -1;
    npad = 0;
    nbound = 0;
    if (pad_off && pad_off < n) {
        size_t end = pad3_off && pad3_off > pad_off && pad3_off <= n ? pad3_off : n;
        npad = (int)((end - pad_off) / PORT_PAD_BYTES);
    }
    if (pad3_off && pad3_off < n) {
        size_t end = names_off && names_off > pad3_off && names_off <= n ? names_off : n;
        nbound = (int)((end - pad3_off) / PORT_BOUND_BYTES);
    }
    parse_intro(st, n, pad_off, pad3_off, npad, nbound);
    p = st + poff;
    {
        int last_scenery_pad = -1;
        const uint8_t *last_scenery_pd = NULL;
        while (p + 4 <= st + n) {
        uint8_t type = p[3];
        uint16_t words;
        size_t bytes;
        if (type == PDEF_END)
            break;
        words = type < PDEF_MAX ? k_pdef_words[type] : 1;
        if (words < 1)
            words = 1;
        bytes = (size_t)words * 4u;
        if (p + bytes > st + n)
            break;
        if ((type == PDEF_MAGAZINE || type == PDEF_COLLECTABLE ||
             type == PDEF_ARMOUR || type == PDEF_KEY || type == PDEF_AMMO) &&
            bytes >= 8) {
            int16_t model = (int16_t)be16(p + 4);
            int16_t pad = (int16_t)be16(p + 6);
            int16_t orig_pad = pad;
            unsigned flags = bytes >= 12 ? be32(p + 8) : 0u;
            const uint8_t *pd = NULL;
            int amount_raw = 0;
            if (bytes >= 132)
                amount_raw = (int)be32(p + 128);
            /* ASSIGNEDTOCHR: pad field is chrnum, not a floor pad. */
            if ((flags & 0x00004000u) && orig_pad >= 0 && orig_pad < PORT_CHR_GUN_MAX &&
                pickup_kind_of(type, model) == PORT_PICKUP_AMMO)
                g_chr_gun[orig_pad] = model;
            if (pad < 0 && last_scenery_pd)
                pad = (int16_t)last_scenery_pad, pd = last_scenery_pd;
            else if (pad >= 0 && pad < npad)
                pd = st + pad_off + (size_t)pad * PORT_PAD_BYTES;
            if (pd && model >= 0)
                record_pickup_cand(type, model, pad, pd, amount_raw, orig_pad, flags);
        }
        if (scenery_type(type) && bytes >= 8 && g_nprop < PORT_MAX_PROPS) {
            int16_t model = (int16_t)be16(p + 4);
            int16_t pad = (int16_t)be16(p + 6);
            uint16_t extra = be16(p);
            if (model >= 0 && pad >= 0 && pad < npad) {
                const uint8_t *pd = st + pad_off + (size_t)pad * PORT_PAD_BYTES;
                PortProp *pr = &g_prop[g_nprop];
                const PortPropCat *cat = cat_by_id(model);
                last_scenery_pad = pad;
                last_scenery_pd = pd;
                fill_pad_prop(pr, type, model, pad, pd,
                              (extra ? (float)extra / 256.f : 1.f) * (cat ? cat->scale : 1.f));
                if (type == PDEF_DOOR && bytes >= 0x9c) {
                    /* Rare 16.16 at 0x84; doorType u16 at 0x9a. */
                    pr->max_frac = (float)(int32_t)be32(p + 0x84) / 65536.f;
                    pr->door_type = (int)be16(p + 0x9a);
                }
                pr->mdl = load_model(model);
                if (pr->mdl)
                    g_nprop++;
            }
        } else if (type == PDEF_GUARD && bytes >= 28 && g_nprop < PORT_MAX_PROPS) {
            /* GuardRecord: chrnum@4 pad@6 BodyID@8 HeadID@16 (s16). */
            int16_t chrnum = (int16_t)be16(p + 4);
            int16_t pad = (int16_t)be16(p + 6);
            int16_t body = (int16_t)be16(p + 8);
            int16_t head = (int16_t)be16(p + 0x16);
            const uint8_t *pd = pad_bytes(st, n, pad_off, pad3_off, npad, nbound, pad);
            const PortPropCat *cat = chr_by_id(body);
            PortModel *mdl;
            if (pd && body >= 0 && (mdl = load_chr(body)) != NULL) {
                PortProp *pr = &g_prop[g_nprop];
                fill_pad_prop(pr, type, body, pad, pd,
                              (cat ? cat->scale : 1.f) *
                                  (mdl->fit_scale != 0.f ? mdl->fit_scale : 1.f));
                pr->mdl = mdl;
                bind_head_off(pr, mdl);
                attach_chr_head(pr, body, head);
                pr->chrnum = chrnum;
                g_nprop++;
            }
        }
        p += bytes;
        }
    }
    bind_assigned_guns();
    return 0;
}

void port_prop_unload(void)
{
    int i;
    for (i = 0; i < g_nmdl; i++)
        free(g_mdl[i].file);
    memset(g_mdl, 0, sizeof g_mdl);
    memset(g_prop, 0, sizeof g_prop);
    free(g_setup);
    g_setup = NULL;
    g_setup_len = 0;
    g_nprop = 0;
    g_npcand = 0;
    g_pickup_prop = -1;
    g_pickup_drawn = 0;
    g_ndrop = 0;
    memset(g_drops, 0, sizeof g_drops);
    g_drop_drawn = 0;
    memset(g_pcand, 0, sizeof g_pcand);
    memset(g_chr_gun, 0, sizeof g_chr_gun);
    g_nmdl = 0;
    g_viewgun_parts = 0;
    g_viewgun_id = PORT_GUN_WPPK_ID;
    g_drawn = 0;
    g_have_intro = 0;
    g_intro_pad = -1;
    g_nintro = 0;
    g_have_idle = 0;
    g_have_walk = 0;
    g_have_aim = 0;
    g_have_die = 0;
    g_die_off = 0;
    g_die_nframes = 0;
    g_die_frame = 0;
    g_die_started = 0;
    g_die_tick_ok = 0;
    g_walkers = 0;
    g_walk_prop = -1;
    g_idle_prop = -1;
    g_walk_frame = 0;
    g_walk_nframes = 0;
    g_walk_fit_scale = 0.f;
    g_walk_fit_ymin = 0.f;
    g_walk_path_ok = 0;
    g_walk_going_b = 1;
    g_walk_blocked = 0;
    g_have_spawn_xz = 0;
    g_walk_spawn_x = 0.f;
    g_walk_spawn_z = 0.f;
    g_fire_cd = 0;
    g_guard_shots = 0;
    g_pose_rest = NULL;
    memset(g_idle_rest, 0, sizeof g_idle_rest);
    memset(g_walk_rest, 0, sizeof g_walk_rest);
    memset(g_aim_rest, 0, sizeof g_aim_rest);
    memset(g_die_rest, 0, sizeof g_die_rest);
    memset(g_die_hold, 0, sizeof g_die_hold);
    g_walk_info[0] = 0;
    g_aim_info[0] = 0;
    g_die_info[0] = 0;
    g_retail_slab_ok = 0;
    memset(&g_retail_slab, 0, sizeof g_retail_slab);
}

int port_prop_load(int level_id)
{
    const C0Pack *pack;
    const C0PackEntry *e;
    const char *name = setup_name(level_id);
    uint8_t *copy = NULL;
    size_t clen = 0;
    int rc;

    port_prop_unload();
    if (!name)
        return PORT_PROP_OK;
    pack = port_pack();
    if (!pack)
        return PORT_PROP_OK;
    e = c0pack_find(pack, name);
    if (!e)
        e = c0pack_find_tail(pack, name);
    if (!e || e->size == 0)
        return PORT_PROP_OK;
    rc = inflate_or_copy(e->bytes, e->size, &copy, &clen);
    if (rc != 0)
        return PORT_PROP_OK;
    g_setup = copy;
    g_setup_len = clen;
    load_idle_rest();
    parse_setup(g_setup, g_setup_len);
    assign_walkers();
    (void)load_wppk();
    (void)load_ak47(); /* bind Gak47Z if present; viewgun stays PP7 */
    (void)load_mp5k(); /* bind Gmp5kZ if present; viewgun stays PP7 */
    return PORT_PROP_OK;
}

int port_prop_count(void) { return g_nprop; }

int port_prop_models(void) { return g_nmdl; }

int port_prop_drawn(void) { return g_drawn; }

int port_prop_intro_pad(void) { return g_have_intro ? g_intro_pad : -1; }

int port_prop_intro_count(void) { return g_nintro; }

int port_prop_intro_at(int i, float pos[3], float look[3], int *pad_out)
{
    if (i < 0 || i >= g_nintro)
        return -1;
    if (pos) {
        pos[0] = g_intro_xyz[i][0];
        pos[1] = g_intro_xyz[i][1];
        pos[2] = g_intro_xyz[i][2];
    }
    if (look) {
        look[0] = g_intro_looks[i][0];
        look[1] = g_intro_looks[i][1];
        look[2] = g_intro_looks[i][2];
    }
    if (pad_out)
        *pad_out = g_intro_pads[i];
    return 0;
}

int port_prop_intro(float pos[3], float look[3], int *pad_out)
{
    if (!g_have_intro)
        return -1;
    if (pos) {
        pos[0] = g_intro_pos[0];
        pos[1] = g_intro_pos[1];
        pos[2] = g_intro_pos[2];
    }
    if (look) {
        look[0] = g_intro_look[0];
        look[1] = g_intro_look[1];
        look[2] = g_intro_look[2];
    }
    if (pad_out)
        *pad_out = g_intro_pad;
    return 0;
}

int port_prop_guard_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type == PDEF_GUARD)
            n++;
    }
    return n;
}

int port_prop_have_idle(void) { return g_have_idle; }

int port_prop_have_walk(void) { return g_have_walk; }

int port_prop_have_aim(void) { return g_have_aim; }

int port_prop_walk_count(void) { return g_walkers; }

int port_prop_guard_parts(void)
{
    int i;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type == PDEF_GUARD && g_prop[i].mdl)
            return g_prop[i].mdl->npart;
    }
    return 0;
}

const char *port_prop_idle_info(void)
{
    snprintf(g_pose_info, sizeof g_pose_info, "%s%s%s%s%s%s%s",
             g_idle_info[0] ? g_idle_info : "idle=0",
             g_walk_info[0] ? " " : "",
             g_walk_info[0] ? g_walk_info : "",
             g_aim_info[0] ? " " : "",
             g_aim_info[0] ? g_aim_info : "",
             g_die_info[0] ? " " : "",
             g_die_info[0] ? g_die_info : "");
    return g_pose_info;
}

int port_prop_have_die(void) { return g_have_die; }

int port_prop_walk_xz(float *x, float *z)
{
    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return -1;
    if (x)
        *x = g_prop[g_walk_prop].pos[0];
    if (z)
        *z = g_prop[g_walk_prop].pos[2];
    return 0;
}

int port_prop_guard_xyz(int want, float *x, float *y, float *z)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        if (n == want) {
            if (x)
                *x = g_prop[i].pos[0];
            if (y)
                *y = g_prop[i].pos[1];
            if (z)
                *z = g_prop[i].pos[2];
            return 0;
        }
        n++;
    }
    return -1;
}

int port_prop_guard_xz(int want, float *x, float *z)
{
    return port_prop_guard_xyz(want, x, NULL, z);
}


static PortModel *load_wppk(void)
{
    PortModel *m = load_named(PORT_GUN_WPPK_ID, "gun", "G", &k_wppk_gun, 0, NULL);
    if (m && m->npart)
        return m;
    return NULL;
}

static PortModel *load_ak47(void)
{
    PortModel *m = load_named(PORT_GUN_AK47_ID, "gun", "G", &k_ak47_gun, 0, NULL);
    if (m && m->npart)
        return m;
    return NULL;
}

static PortModel *load_mp5k(void)
{
    PortModel *m = load_named(PORT_GUN_MP5K_ID, "gun", "G", &k_mp5k_gun, 0, NULL);
    if (m && m->npart)
        return m;
    return NULL;
}

void port_prop_viewgun_sync(void)
{
    int w = port_gun_weapon();
    if (w == PORT_WEAPON_KF7 && load_ak47())
        g_viewgun_id = PORT_GUN_AK47_ID;
    else if (w == PORT_WEAPON_MP5K && load_mp5k())
        g_viewgun_id = PORT_GUN_MP5K_ID;
    else
        g_viewgun_id = PORT_GUN_WPPK_ID;
}

static PortModel *load_viewgun(void)
{
    port_prop_viewgun_sync();
    if (g_viewgun_id == PORT_GUN_AK47_ID) {
        PortModel *m = load_ak47();
        if (m)
            return m;
    }
    if (g_viewgun_id == PORT_GUN_MP5K_ID) {
        PortModel *m = load_mp5k();
        if (m)
            return m;
    }
    return load_wppk();
}

int port_prop_viewgun_parts(void) { return g_viewgun_parts; }

int port_prop_viewgun_id(void) { return g_viewgun_id; }

/*
 * Static first-person pack gun (GwppkZ, Gak47Z after KF7, Gmp5kZ after MP5K).
 * Walk Rare nodes (no recoil/reload). Model +Z is Rare forward; G1 looks
 * -Z so hold * R180 * S(0.1) * part. Camera-space via .view. Hold is
 * Rare PosXYZ; scale is IDO_POINT_ONE (gunfire.c gunmtx).
 */
static int viewgun_is_flash(int p, const PortPart *pt)
{
    /* SKEL_FLASH: two ~20-cmd z=0 cards walked first. Idle hides them. */
    return p < 2 && pt && pt->pri_n <= 22u;
}

int port_prop_fill_viewgun(G1RoomDl *out, int cap)
{
    PortModel *m;
    int p, k = 0;
    float hold[4][4], r180[4][4];
    int show_flash;

    g_viewgun_parts = 0;
    if (!out || cap < 1)
        return 0;
    m = load_viewgun();
    if (!m || m->npart == 0)
        return 0;
    show_flash = port_gun_flash_frames() > 0;
    {
        float hx, hy, hz;
        port_gun_hold(&hx, &hy, &hz);
        mtx_local(hold, hx, hy, hz, 0.f, 0.f, 0.f);
    }
    mtx_local(r180, 0.f, 0.f, 0.f, 0.f, PI_F, 0.f);
    for (p = 0; p < m->npart && k < cap; p++) {
        const PortPart *pt = &m->part[p];
        float part[4][4], tmp[4][4], world[4][4];
        float rx, ry, rz;
        if (!pt->pri || pt->pri_n == 0)
            continue;
        if (viewgun_is_flash(p, pt) && !show_flash)
            continue;
        /* Rare: gunmtx = T(Pos) * R * S(0.1) * node. Scale node
         * translation here so G1's T*R*S matches that product. */
        mtx_local(part, pt->ox * PORT_GUN_MODEL_SCALE, pt->oy * PORT_GUN_MODEL_SCALE,
                  pt->oz * PORT_GUN_MODEL_SCALE, pt->rx, pt->ry, pt->rz);
        mtx_mul4(tmp, r180, part);
        mtx_mul4(world, hold, tmp);
        mtx_euler(world, &rx, &ry, &rz);
        if (rx * rx + ry * ry + rz * rz < 1e-8f)
            rx = ry = rz = 0.f;
        memset(&out[k], 0, sizeof out[k]);
        out[k].pri = pt->pri;
        out[k].pri_n = pt->pri_n;
        out[k].sec = pt->sec;
        out[k].sec_n = pt->sec_n;
        out[k].ox = world[0][3];
        out[k].oy = world[1][3];
        out[k].oz = world[2][3];
        out[k].rx = rx;
        out[k].ry = ry;
        out[k].rz = rz;
        out[k].scale = PORT_GUN_MODEL_SCALE;
        out[k].seg5 = (uintptr_t)m->file;
        out[k].seg4 = pt->vtx4;
        out[k].view = 1;
        k++;
    }
    g_viewgun_parts = k;
    return k;
}

static int near_room(const PortProp *pr, const float room1[3], const float *room_xyz,
                     int nrooms, const uint8_t *room_ids)
{
    float dx, dy, dz, best, d;
    int r, near = 0;

    best = PORT_PROP_NEAR * PORT_PROP_NEAR;
    for (r = 0; r < nrooms; r++) {
        const float *rp;
        int id = room_ids ? room_ids[r] : r + 1;
        if (id < 1)
            continue;
        rp = room_xyz + (size_t)id * 3;
        dx = pr->pos[0] - rp[0];
        dy = pr->pos[1] - rp[1];
        dz = pr->pos[2] - rp[2];
        d = dx * dx + dy * dy + dz * dz;
        if (d <= best) {
            best = d;
            near = 1;
        }
    }
    /* Player is room-1 local; pads/portals are world. Same 4000 cap. */
    {
        float px = port_player_x() + room1[0];
        float py = port_player_y() + room1[1];
        float pz = port_player_z() + room1[2];
        dx = pr->pos[0] - px;
        dy = pr->pos[1] - py;
        dz = pr->pos[2] - pz;
        if (dx * dx + dy * dy + dz * dz <= PORT_PROP_NEAR * PORT_PROP_NEAR)
            return 1;
    }
    if (!near && nrooms > 0)
        return 0;
    if (nrooms <= 0) {
        dx = pr->pos[0] - room1[0];
        dy = pr->pos[1] - room1[1];
        dz = pr->pos[2] - room1[2];
        if (dx * dx + dy * dy + dz * dz > PORT_PROP_NEAR * PORT_PROP_NEAR)
            return 0;
    }
    return 1;
}


/* Closed-door slab. Prefer retail Pgas_plant_met1_do1 (96-vert mesh,
 * SETTEX 685-688,706). G_VTX is 0x04xxxxxx — bind the node vertex bank
 * at file+0xC0 as seg 4. Do not bind seg 3 (G_MTX LOAD would replace
 * the camera). Root GROUP origin is gas-plant world; zero it and use
 * the fitted portal pose. Fallback: fitted G1DL quad, SETTEX 685. */
static uint8_t g_slab_file[192];
static PortModel g_slab_mdl;
static int g_slab_ok;
#define SLAB_DOOR_TEX 685u /* Pgas_plant_met1_do1 tile 0; imagelist "685" */
#define SLAB_RETAIL_ID 158
#define SLAB_RETAIL_HALF_W 350.f
#define SLAB_RETAIL_BOTTOM -787.f

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr_vtx(uint8_t *v, int16_t x, int16_t y, int16_t z, int16_t s, int16_t t)
{
    wr16(v + 0, (uint16_t)x);
    wr16(v + 2, (uint16_t)y);
    wr16(v + 4, (uint16_t)z);
    wr16(v + 8, (uint16_t)s);
    wr16(v + 10, (uint16_t)t);
    v[12] = 118;
    v[13] = 112;
    v[14] = 98;
    v[15] = 255;
}

static int gdl_has_seg4_vtx(const uint8_t *pri, uint32_t n)
{
    uint32_t i;
    if (!pri || n == 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (pri[i * 8u] == (uint8_t)G_VTX && pri[i * 8u + 4] == 4)
            return 1;
    }
    return 0;
}

static int slab_is_retail(const PortModel *m)
{
    return m && m->npart > 0 && m->part[0].vtx4 != 0 &&
           gdl_has_seg4_vtx(m->part[0].pri, m->part[0].pri_n);
}

static void slab_bounds(const PortModel *m, float *half_w, float *bottom)
{
    const uint8_t *v;
    uint32_t i, n;
    int16_t minx, maxx, miny;

    *half_w = SLAB_RETAIL_HALF_W;
    *bottom = SLAB_RETAIL_BOTTOM;
    if (!m || !m->part[0].vtx4 || m->part[0].nvtx < 3)
        return;
    v = (const uint8_t *)m->part[0].vtx4;
    n = m->part[0].nvtx;
    minx = maxx = (int16_t)((v[0] << 8) | v[1]);
    miny = (int16_t)((v[2] << 8) | v[3]);
    for (i = 1; i < n; i++) {
        const uint8_t *p = v + i * 16u;
        int16_t x = (int16_t)((p[0] << 8) | p[1]);
        int16_t y = (int16_t)((p[2] << 8) | p[3]);
        if (x < minx)
            minx = x;
        if (x > maxx)
            maxx = x;
        if (y < miny)
            miny = y;
    }
    *half_w = 0.5f * (float)(maxx - minx);
    if (*half_w < 1.f)
        *half_w = SLAB_RETAIL_HALF_W;
    *bottom = (float)miny;
}

static PortModel *slab_quad(void)
{
    /* Two proven 3-vert G_VTX+G_TRI4 packets (same encoding as the magenta
     * door test). A 4-vert packet left stale room verts on one triangle. */
    uint32_t vtx = ((uint32_t)(uint8_t)G_VTX << 24) | (0x20u << 16);
    uint32_t end = (uint32_t)(uint8_t)G_ENDDL << 24;
    const uint32_t v1 = 4 + 7 * 8, v2 = 4 + 7 * 8 + 48;
    const int16_t us = 32 * 32, vt = 33 * 32; /* tile 0 is 32x33 */

    if (g_slab_ok)
        return &g_slab_mdl;
    memset(g_slab_file, 0, sizeof g_slab_file);
    wr32(g_slab_file, PORT_BG_MAGIC_G1DL);
    wr32(g_slab_file + 4, 0xBB002801u); /* G_TEXTURE scale 1,1 */
    wr32(g_slab_file + 8, 0xFFFFFFFFu);
    wr32(g_slab_file + 12, 0xC0000003u); /* G_SETTEX TILE */
    wr32(g_slab_file + 16, SLAB_DOOR_TEX);
    wr32(g_slab_file + 20, vtx);
    wr32(g_slab_file + 24, 0x05000000u | v1);
    wr32(g_slab_file + 28, 0xB1000002u);
    wr32(g_slab_file + 32, 0x00000010u);
    wr32(g_slab_file + 36, vtx);
    wr32(g_slab_file + 40, 0x05000000u | v2);
    wr32(g_slab_file + 44, 0xB1000002u);
    wr32(g_slab_file + 48, 0x00000010u);
    wr32(g_slab_file + 52, end);
    wr_vtx(g_slab_file + v1 + 0, -90, 0, 0, 0, vt);
    wr_vtx(g_slab_file + v1 + 16, 90, 0, 0, us, vt);
    wr_vtx(g_slab_file + v1 + 32, 90, 260, 0, us, 0);
    wr_vtx(g_slab_file + v2 + 0, -90, 0, 0, 0, vt);
    wr_vtx(g_slab_file + v2 + 16, 90, 260, 0, us, 0);
    wr_vtx(g_slab_file + v2 + 32, -90, 260, 0, 0, 0);
    memset(&g_slab_mdl, 0, sizeof g_slab_mdl);
    g_slab_mdl.file = g_slab_file;
    g_slab_mdl.file_len = v2 + 48;
    g_slab_mdl.part[0].pri = g_slab_file + 4;
    g_slab_mdl.part[0].pri_n = 7;
    g_slab_mdl.npart = 1;
    g_slab_ok = 1;
    return &g_slab_mdl;
}

static PortModel *slab_door(void)
{
    PortModel *m;
    int p;

    if (g_retail_slab_ok)
        return &g_retail_slab;
    m = load_model(SLAB_RETAIL_ID);
    if (m && be32(m->file) != PORT_BG_MAGIC_G1DL && slab_is_retail(m)) {
        g_retail_slab = *m;
        for (p = 0; p < g_retail_slab.npart; p++) {
            /* Replace gas-plant GROUP origin with the fitted portal pose. */
            g_retail_slab.part[p].ox = 0.f;
            g_retail_slab.part[p].oy = 0.f;
            g_retail_slab.part[p].oz = 0.f;
            g_retail_slab.part[p].rx = 0.f;
            g_retail_slab.part[p].ry = 0.f;
            g_retail_slab.part[p].rz = 0.f;
        }
        g_retail_slab_ok = 1;
        return &g_retail_slab;
    }
    return slab_quad();
}

static int emit_parts(G1RoomDl *out, int cap, int k, const PortProp *pr, const PortModel *mdl,
                      const float room1[3], float extra_x, float extra_y, float extra_z,
                      float extra_rx, float extra_ry, float extra_rz, float add_yaw,
                      float wdx, float wdy, float wdz)
{
    int p;
    float extra[4][4];

    if (!mdl)
        return k;
    mtx_local(extra, extra_x, extra_y, extra_z, extra_rx, extra_ry, extra_rz);
    /* Bake pad yaw into the part matrix (T * R_yaw * R_pose). G1 used to
     * do T * R_pose * R_yaw, which tore idle/walk limbs into wall smears
     * while GROUP-origin AABB still looked like a 1510u stand. Identity
     * pose (doors, G1DL) is unchanged: R_pose=I. */
    {
        float yawm[4][4];
        mtx_local(yawm, 0.f, 0.f, 0.f, 0.f, (pr->yaw + add_yaw) * (PI_F / 180.f), 0.f);
        mtx_mul4(extra, yawm, extra);
    }
    for (p = 0; p < mdl->npart && k < cap; p++) {
        const PortPart *pt = &mdl->part[p];
        float loc[4][4], world[4][4];
        float lx, ly, lz, rx, ry, rz;
        if (!pt->pri || pt->pri_n == 0)
            continue;
        mtx_local(loc, pt->ox, pt->oy, pt->oz, pt->rx, pt->ry, pt->rz);
        mtx_mul4(world, extra, loc);
        lx = world[0][3];
        ly = world[1][3];
        lz = world[2][3];
        mtx_euler(world, &rx, &ry, &rz);
        if (rx * rx + ry * ry + rz * rz < 1e-8f)
            rx = ry = rz = 0.f;
        if (mdl->fit_scale != 1.f || mdl->fit_ymin != 0.f) {
            float sc = (pr->scale != 0.f) ? pr->scale : 1.f;
            lx *= sc;
            ly = (ly - mdl->fit_ymin) * sc;
            lz *= sc;
        }
        memset(&out[k], 0, sizeof out[k]);
        out[k].pri = pt->pri;
        out[k].pri_n = pt->pri_n;
        out[k].sec = pt->sec;
        out[k].sec_n = pt->sec_n;
        out[k].ox = pr->pos[0] - room1[0] + lx + wdx;
        out[k].oy = pr->pos[1] - room1[1] + ly + wdy;
        out[k].oz = pr->pos[2] - room1[2] + lz + wdz;

        out[k].yaw = 0.f;
        out[k].scale = pr->scale;
        out[k].seg5 = (uintptr_t)mdl->file;
        out[k].seg4 = pt->vtx4;
        out[k].rx = rx;
        out[k].ry = ry;
        out[k].rz = rz;
        k++;
    }
    return k;
}

/*
 * Park-open pose from pad + Rare doorType. Facility start doors
 * (UsetuparkZ index 32+, pads 66+) are DOORTYPE_SWINGING / maxFrac=90.
 * Hinge is pad + fitted / Rare-quad half-w along look-tangent
 * (pad doors keep the 90 default; not boundpad bbox). A 90° swing
 * then parks the slab fully off a wide opening. Swing sign is away
 * from the player recorded at use. Sliding parks along that tangent
 * by 2*HALF_W. Vertical lifts. No Rare lock/key.
 * frac 0..1 (PORT_DOOR_OPEN_TICKS) swings the angle / slides the
 * offset so Z-unlatch is not a teleport. Close reverse-swings the
 * same ticks. Collision stays off until frac hits 0.
 */
static void door_open_pose(const PortProp *pr, float frac, float *add_yaw, float *dx,
                           float *dy, float *dz)
{
    float lx = pr->look[0], lz = pr->look[2];
    float len = sqrtf(lx * lx + lz * lz);
    float nx, nz, tx, tz, hw;
    int side = port_stan_door_side_at(pr->pos[0], pr->pos[2]);
    int dtype = pr->door_type;

    *add_yaw = *dx = *dy = *dz = 0.f;
    if (frac <= 0.f)
        return;
    if (frac > 1.f)
        frac = 1.f;
    if (len < 1e-4f) {
        nx = 0.f;
        nz = 1.f;
    } else {
        nx = lx / len;
        nz = lz / len;
    }
    tx = -nz;
    tz = nx;
    hw = port_stan_door_half_w_at(pr->pos[0], pr->pos[2]);
    if (hw < 1.f)
        hw = PORT_DOOR_HALF_W;

    if (dtype == DOORTYPE_SLIDING) {
        float s = (side >= 0) ? 1.f : -1.f;
        *dx = tx * PORT_DOOR_SLIDE * s * frac;
        *dz = tz * PORT_DOOR_SLIDE * s * frac;
        return;
    }
    if (dtype == DOORTYPE_VERTICAL) {
        *dy = PORT_DOOR_SLIDE * frac;
        return;
    }
    /* SWINGING and every other type: frac of 90° around the +T hinge. */
    {
        float ang = (side > 0) ? PORT_DOOR_OPEN_YAW : -PORT_DOOR_OPEN_YAW;
        float th, c, s, relx, relz, rx, rz;
        if (pr->max_frac > 1.f && pr->max_frac <= 180.f)
            ang = (ang < 0.f) ? -pr->max_frac : pr->max_frac;
        ang *= frac;
        th = ang * (PI_F / 180.f);
        c = cosf(th);
        s = sinf(th);
        /* H = P + hw T; new = H + R*(P-H); delta = hw T - R*(hw T). */
        relx = hw * tx;
        relz = hw * tz;
        rx = c * relx + s * relz;
        rz = -s * relx + c * relz;
        *dx = relx - rx;
        *dz = relz - rz;
        *add_yaw = ang;
    }
}

int port_prop_door_park_offset(float world_x, float world_z, float portal_yaw,
                               float *dx, float *dz, float *add_yaw)
{
    PortProp pose;
    float frac, ay = 0.f, odx = 0.f, ody = 0.f, odz = 0.f;

    if (dx)
        *dx = 0.f;
    if (dz)
        *dz = 0.f;
    if (add_yaw)
        *add_yaw = 0.f;
    frac = port_stan_door_frac_at(world_x, world_z);
    if (frac <= 0.f)
        return 0;
    memset(&pose, 0, sizeof pose);
    pose.pos[0] = world_x;
    pose.pos[2] = world_z;
    pose.look[0] = (portal_yaw == 90.f) ? 1.f : 0.f;
    pose.look[2] = (portal_yaw == 90.f) ? 0.f : -1.f;
    pose.door_type = DOORTYPE_SWINGING;
    pose.max_frac = 90.f;
    door_open_pose(&pose, frac, &ay, &odx, &ody, &odz);
    if (dx)
        *dx = odx;
    if (dz)
        *dz = odz;
    if (add_yaw)
        *add_yaw = ay;
    return 1;
}

static int emit_guard_body(G1RoomDl *out, int cap, int k, PortProp *pr, const float room1[3])
{
    PortModel *mdl = pr->mdl;
    float hx, hy, hz, hrx, hry, hrz;
    int dead;

    if (!mdl || !mdl->npart)
        return k;
    dead = port_stan_guard_dead_at(pr->pos[0], pr->pos[2]);
    hx = pr->head_off[0];
    hy = pr->head_off[1];
    hz = pr->head_off[2];
    hrx = pr->head_rx;
    hry = pr->head_ry;
    hrz = pr->head_rz;
    if (dead) {
        PortModel *dm;
        maybe_spawn_death_drops();
        /* No pack death rest: keep skip-draw. No fake ragdoll. */
        if (!g_have_die)
            return k;
        start_die_if_needed();
        dm = load_chr_die(pr->model);
        if (!dm || !dm->npart)
            return k;
        mdl = dm;
        if (dm->have_head) {
            hx = dm->head_off[0];
            hy = dm->head_off[1];
            hz = dm->head_off[2];
            hrx = dm->head_rx;
            hry = dm->head_ry;
            hrz = dm->head_rz;
            if (pr->head) {
                pr->head->fit_scale = dm->fit_scale;
                pr->head->fit_ymin = dm->fit_ymin;
            }
        }
    }
    k = emit_parts(out, cap, k, pr, mdl, room1, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                   0.f, 0.f);
    if (pr->head && pr->head->npart)
        k = emit_parts(out, cap, k, pr, pr->head, room1, hx, hy, hz, hrx, hry, hrz, 0.f,
                       0.f, 0.f, 0.f);
    return k;
}

int port_prop_fill_rooms(G1RoomDl *out, int cap, const float room1[3],
                         const float *room_xyz, int nrooms, const uint8_t *room_ids)
{
    int i, k = 0;
    g_drawn = 0;
    g_pickup_drawn = 0;
    g_drop_drawn = 0;
    maybe_spawn_death_drops();
    if (!out || cap < 1 || !room1)
        return 0;
    /* Pad doors first so Facility openings are not eaten by guard parts. */
    for (i = 0; i < g_nprop && k < cap; i++) {
        PortProp *pr = &g_prop[i];
        float add_yaw = 0.f, odx = 0.f, ody = 0.f, odz = 0.f;
        if (pr->type != PDEF_DOOR || !pr->mdl || pr->mdl->npart == 0)
            continue;
        if (!near_room(pr, room1, room_xyz, nrooms, room_ids))
            continue;
        {
            float frac = port_stan_door_frac_at(pr->pos[0], pr->pos[2]);
            if (frac > 0.f)
                door_open_pose(pr, frac, &add_yaw, &odx, &ody, &odz);
        }
        k = emit_parts(out, cap, k, pr, pr->mdl, room1, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                       add_yaw, odx, ody, odz);
    }
    /*
     * Start-hallway openings have no PROPDEF_DOOR (those pads sit in the
     * gas-plant cluster). Fit a closed G1DL slab on door-sized portals.
     */
    {
        PortModel *slab = slab_door();
        int retail = slab_is_retail(slab);
        int o, no = port_stage_opening_count();
        float half_w = PORT_DOOR_HALF_W, bottom = 0.f;
        float pwx = room1[0] + port_player_x();
        float pwz = room1[2] + port_player_z();
        float floor_y = room1[1] + (port_player_y() - 175.f);
        float th = port_player_theta() * (PI_F / 180.f);
        float lookx = sinf(th), lookz = -cosf(th);
        if (retail)
            slab_bounds(slab, &half_w, &bottom);
        for (o = 0; o < no && k < cap; o++) {
            float pos[3], yaw = 0.f, width = 0.f;
            PortProp tmp, probe;
            int j, covered = 0;
            float lx, lz, tox, toz;
            int ra = 0, rb = 0, in_walk = 0, r;
            if (port_stage_opening(o, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            for (r = 0; r < nrooms; r++) {
                if (room_ids && (room_ids[r] == ra || room_ids[r] == rb)) {
                    in_walk = 1;
                    break;
                }
            }
            if (!in_walk)
                continue;
            memset(&probe, 0, sizeof probe);
            probe.pos[0] = pos[0];
            probe.pos[1] = pos[1];
            probe.pos[2] = pos[2];
            if (!near_room(&probe, room1, room_xyz, nrooms, room_ids))
                continue;
            for (j = 0; j < g_nprop; j++) {
                float dx, dy, dz;
                if (g_prop[j].type != PDEF_DOOR)
                    continue;
                dx = g_prop[j].pos[0] - pos[0];
                dy = g_prop[j].pos[1] - pos[1];
                dz = g_prop[j].pos[2] - pos[2];
                if (dx * dx + dy * dy + dz * dz < 250.f * 250.f) {
                    covered = 1;
                    break;
                }
            }
            if (covered)
                continue;
            lx = pos[0] - pwx;
            lz = pos[2] - pwz;
            tox = pos[0] - pwx;
            toz = pos[2] - pwz;
            {
                float d2 = lx * lx + lz * lz;
                int path = port_stage_path_opening(ra, rb);
                if (d2 > 900.f * 900.f)
                    continue;
                /* Path lab slabs stay visible inside Z-range (~200).
                 * Start-hall 250 min is unchanged so spawn pixels hold. */
                if (!path && d2 < 250.f * 250.f)
                    continue;
                if ((!path || d2 >= 250.f * 250.f) &&
                    tox * lookx + toz * lookz < 40.f)
                    continue;
            }
            /* Upper/catwalk portals sit hundreds of units above the floor. */
            if (pos[1] > floor_y + 200.f)
                continue;
            memset(&tmp, 0, sizeof tmp);
            tmp.pos[0] = pos[0];
            tmp.pos[1] = floor_y;
            tmp.pos[2] = pos[2];
            /* Face the player so the XY slab is not edge-on / backface. */
            if (yaw == 90.f)
                tmp.yaw = (pwx > pos[0]) ? 90.f : -90.f;
            else
                tmp.yaw = (pwz > pos[2]) ? 0.f : 180.f;
            tmp.scale = (width > 1.f) ? (width / (2.f * half_w)) : (PORT_DOOR_HALF_W / half_w);
            tmp.mdl = slab;
            {
                float add_yaw = 0.f, odx = 0.f, ody = 0.f, odz = 0.f;
                {
                    float frac = port_stan_door_frac_at(pos[0], pos[2]);
                    if (frac > 0.f) {
                        PortProp pose = tmp;
                        pose.look[0] = (yaw == 90.f) ? 1.f : 0.f;
                        pose.look[2] = (yaw == 90.f) ? 0.f : -1.f;
                        pose.door_type = DOORTYPE_SWINGING;
                        pose.max_frac = 90.f;
                        door_open_pose(&pose, frac, &add_yaw, &odx, &ody, &odz);
                    }
                }
                k = emit_parts(out, cap, k, &tmp, slab, room1, 0.f,
                               -bottom * tmp.scale, 0.f, 0.f, 0.f, 0.f,
                               add_yaw, odx, ody, odz);
            }
        }
        /* Start alcoves are in-room GDL, not portals. One left-wall slab
         * when no door-sized portal already sits on that wall. */
        if (k < cap && no > 0) {
            float leftx = lookz, leftz = -lookx;
            int o, have_left = 0, no = port_stage_opening_count();
            for (o = 0; o < no; o++) {
                float pos[3], yaw, width, tox, toz, d2;
                int ra, rb;
                if (port_stage_opening(o, pos, &yaw, &width, &ra, &rb) != 0)
                    continue;
                tox = pos[0] - pwx;
                toz = pos[2] - pwz;
                d2 = tox * tox + toz * toz;
                if (d2 < 400.f * 400.f && tox * leftx + toz * leftz > 50.f)
                    have_left = 1;
            }
            if (!have_left) {
                PortProp tmp;
                memset(&tmp, 0, sizeof tmp);
                tmp.pos[0] = pwx + leftx * 160.f;
                tmp.pos[1] = floor_y;
                tmp.pos[2] = pwz + leftz * 160.f;
                tmp.yaw = (pwz + leftz * 160.f > pwz) ? 0.f : 180.f;
                /* Face the player: left wall is +Z at spawn, yaw 0 faces +Z. */
                if (leftz > 0.f)
                    tmp.yaw = 180.f;
                else if (leftz < 0.f)
                    tmp.yaw = 0.f;
                else
                    tmp.yaw = (leftx > 0.f) ? -90.f : 90.f;
                tmp.scale = 1.15f * (PORT_DOOR_HALF_W / half_w);
                tmp.mdl = slab;
                k = emit_parts(out, cap, k, &tmp, slab, room1, 0.f, -bottom * tmp.scale,
                               0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
            }
        }
    }
    /* Test mover first so far-away setup guards cannot eat the prop-pass cap. */
    if (g_walk_prop >= 0 && g_walk_prop < g_nprop && k < cap) {
        PortProp *pr = &g_prop[g_walk_prop];
        if (pr->mdl && pr->mdl->npart &&
            near_room(pr, room1, room_xyz, nrooms, room_ids))
            k = emit_guard_body(out, cap, k, pr, room1);
    }
    /* Nearest remaining props first so the cap spends on this room and
     * the next walked rooms, not a far setup cluster. */
    {
        int idx[PORT_MAX_PROPS];
        float d2[PORT_MAX_PROPS];
        int nidx = 0, a, b;
        float px = port_player_x() + room1[0];
        float py = port_player_y() + room1[1];
        float pz = port_player_z() + room1[2];
        for (i = 0; i < g_nprop; i++) {
            PortProp *pr = &g_prop[i];
            float dx, dy, dz;
            if (i == g_walk_prop)
                continue;
            if (pr->hidden)
                continue;
            if (!pr->mdl || pr->mdl->npart == 0 || pr->type == PDEF_DOOR)
                continue;
            if (pr->pickup_kind) {
                float pdx = pr->pos[0] - px, pdz = pr->pos[2] - pz;
                if (pdx * pdx + pdz * pdz > PORT_PICKUP_DRAW * PORT_PICKUP_DRAW)
                    continue;
            }
            if (!near_room(pr, room1, room_xyz, nrooms, room_ids))
                continue;
            dx = pr->pos[0] - px;
            dy = pr->pos[1] - py;
            dz = pr->pos[2] - pz;
            idx[nidx] = i;
            d2[nidx] = dx * dx + dy * dy + dz * dz;
            nidx++;
        }
        for (a = 1; a < nidx; a++) {
            int ii = idx[a];
            float dd = d2[a];
            for (b = a; b > 0 && d2[b - 1] > dd; b--) {
                idx[b] = idx[b - 1];
                d2[b] = d2[b - 1];
            }
            idx[b] = ii;
            d2[b] = dd;
        }
        for (i = 0; i < nidx && k < cap; i++) {
            PortProp *pr = &g_prop[idx[i]];
            if (pr->type == PDEF_GUARD)
                k = emit_guard_body(out, cap, k, pr, room1);
            else {
                if (pr->pickup_kind)
                    g_pickup_drawn = 1;
                if (drop_index_of(idx[i]) >= 0)
                    g_drop_drawn = 1;
                k = emit_parts(out, cap, k, pr, pr->mdl, room1, 0.f, 0.f, 0.f, 0.f, 0.f,
                               0.f, 0.f, 0.f, 0.f, 0.f);
                if (pr->head && pr->head->npart)
                    k = emit_parts(out, cap, k, pr, pr->head, room1, pr->head_off[0],
                                   pr->head_off[1], pr->head_off[2], pr->head_rx,
                                   pr->head_ry, pr->head_rz, 0.f, 0.f, 0.f, 0.f);
            }
        }
    }
    g_drawn = k;
    return k;
}

int port_prop_pickup_pad(void)
{
    if (g_pickup_prop < 0 || g_pickup_prop >= g_nprop)
        return -1;
    return g_prop[g_pickup_prop].pad;
}

int port_prop_pickup_type(void)
{
    if (g_pickup_prop < 0 || g_pickup_prop >= g_nprop)
        return 0;
    return g_prop[g_pickup_prop].type;
}

int port_prop_pickup_model(void)
{
    if (g_pickup_prop < 0 || g_pickup_prop >= g_nprop)
        return -1;
    return g_prop[g_pickup_prop].model;
}

int port_prop_pickup_kind(void)
{
    if (g_pickup_prop < 0 || g_pickup_prop >= g_nprop)
        return 0;
    return g_prop[g_pickup_prop].pickup_kind;
}

int port_prop_pickup_hidden(void)
{
    if (g_pickup_prop < 0 || g_pickup_prop >= g_nprop)
        return 1;
    return g_prop[g_pickup_prop].hidden;
}

int port_prop_pickup_drawn(void) { return g_pickup_drawn; }

int port_prop_pickup_xyz(float *x, float *y, float *z)
{
    PortProp *pr;
    if (g_pickup_prop < 0 || g_pickup_prop >= g_nprop)
        return -1;
    pr = &g_prop[g_pickup_prop];
    if (x)
        *x = pr->pos[0];
    if (y)
        *y = pr->pos[1];
    if (z)
        *z = pr->pos[2];
    return 0;
}

void port_prop_choose_pickup(void)
{
    choose_pickup();
}

void port_prop_tick_pickup(void)
{
    float r1[3];
    int i;

    maybe_spawn_death_drops();
    if (port_player_health() <= 0)
        return;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    for (i = 0; i < g_nprop; i++) {
        PortProp *pr = &g_prop[i];
        float lx, lz, dx, dz, dist;
        if (pr->hidden || !pr->pickup_kind)
            continue;
        lx = pr->pos[0] - r1[0];
        lz = pr->pos[2] - r1[2];
        dx = port_player_x() - lx;
        dz = port_player_z() - lz;
        dist = sqrtf(dx * dx + dz * dz);
        if (dist > PORT_PICKUP_RADIUS)
            continue;
        pr->hidden = 1;
        if (i == g_pickup_prop)
            g_pickup_drawn = 0;
        if (drop_index_of(i) >= 0)
            g_drop_drawn = 0;
        if (pr->pickup_kind == PORT_PICKUP_ARMOUR)
            port_player_add_armour(pr->pickup_amount);
        else if (pr->model == PORT_PROP_CHRKALASH ||
                 pr->model == PORT_PROP_CHRMP5K)
            port_gun_collect_model(pr->model);
        else
            port_gun_add_reserve(pr->pickup_amount);
    }
}

int port_prop_drop_count(void) { return g_ndrop; }

int port_prop_drop_model_at(int i)
{
    int pi;
    if (i < 0 || i >= g_ndrop)
        return -1;
    pi = g_drops[i];
    if (pi < 0 || pi >= g_nprop)
        return -1;
    return g_prop[pi].model;
}

int port_prop_drop_hidden_at(int i)
{
    int pi;
    if (i < 0 || i >= g_ndrop)
        return 1;
    pi = g_drops[i];
    if (pi < 0 || pi >= g_nprop)
        return 1;
    return g_prop[pi].hidden;
}

int port_prop_drop_xyz_at(int i, float *x, float *y, float *z)
{
    PortProp *pr;
    int pi;
    if (i < 0 || i >= g_ndrop)
        return -1;
    pi = g_drops[i];
    if (pi < 0 || pi >= g_nprop)
        return -1;
    pr = &g_prop[pi];
    if (x)
        *x = pr->pos[0];
    if (y)
        *y = pr->pos[1];
    if (z)
        *z = pr->pos[2];
    return 0;
}

int port_prop_drop_model(void)
{
    return port_prop_drop_model_at(g_ndrop - 1);
}

int port_prop_drop_hidden(void)
{
    return port_prop_drop_hidden_at(g_ndrop - 1);
}

int port_prop_drop_drawn(void) { return g_drop_drawn; }

int port_prop_drop_xyz(float *x, float *y, float *z)
{
    return port_prop_drop_xyz_at(g_ndrop - 1, x, y, z);
}

int port_prop_door_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type == PDEF_DOOR)
            n++;
    }
    return n;
}

int port_prop_door_xz(int want, float *x, float *z, float *lx, float *lz)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_DOOR)
            continue;
        if (n == want) {
            if (x)
                *x = g_prop[i].pos[0];
            if (z)
                *z = g_prop[i].pos[2];
            if (lx)
                *lx = g_prop[i].look[0];
            if (lz)
                *lz = g_prop[i].look[2];
            return 0;
        }
        n++;
    }
    return -1;
}
