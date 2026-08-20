#include "match_config.h"

#include <string.h>

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int encodeMatchConfig(const MatchConfig *cfg, uint8_t out[MATCH_CONFIG_BYTES])
{
    int i;
    uint32_t bits;
    if (!cfg || !out)
        return -1;
    if (cfg->pad0 != 0)
        return -1;
    if (cfg->protocol != MATCH_CONFIG_PROTOCOL)
        return -1;
    memset(out, 0, MATCH_CONFIG_BYTES);
    wr16(out + 0, cfg->protocol);
    out[2] = cfg->region;
    out[3] = cfg->nseats;
    out[4] = cfg->delayTicks;
    out[5] = cfg->speedgraphframes;
    out[6] = cfg->aimSight;
    out[7] = cfg->autoAim;
    out[8] = cfg->lookAhead;
    out[9] = cfg->aimControl;
    out[10] = cfg->radar;
    out[11] = 0;
    wr32(out + 12, cfg->rngSeed);
    wr32(out + 16, cfg->stage);
    wr32(out + 20, cfg->scenario);
    wr32(out + 24, cfg->gameLength);
    for (i = 0; i < 4; i++)
        wr32(out + 28 + 4 * i, cfg->chars[i]);
    for (i = 0; i < 4; i++)
        wr32(out + 44 + 4 * i, cfg->handicaps[i]);
    for (i = 0; i < 8; i++)
        wr32(out + 60 + 4 * i, cfg->favWeapons[i / 2][i % 2]);
    for (i = 0; i < 4; i++) {
        memcpy(&bits, &cfg->slider007[i], 4);
        wr32(out + 92 + 4 * i, bits);
    }
    memcpy(out + 108, cfg->packHash, 32);
    memcpy(out + 140, cfg->buildId, 20);
    return MATCH_CONFIG_BYTES;
}

int decodeMatchConfig(const uint8_t in[MATCH_CONFIG_BYTES], MatchConfig *cfg)
{
    int i;
    uint32_t bits;
    if (!in || !cfg)
        return -1;
    memset(cfg, 0, sizeof *cfg);
    cfg->protocol = rd16(in + 0);
    cfg->region = in[2];
    cfg->nseats = in[3];
    cfg->delayTicks = in[4];
    cfg->speedgraphframes = in[5];
    cfg->aimSight = in[6];
    cfg->autoAim = in[7];
    cfg->lookAhead = in[8];
    cfg->aimControl = in[9];
    cfg->radar = in[10];
    cfg->pad0 = in[11];
    cfg->rngSeed = rd32(in + 12);
    cfg->stage = rd32(in + 16);
    cfg->scenario = rd32(in + 20);
    cfg->gameLength = rd32(in + 24);
    for (i = 0; i < 4; i++)
        cfg->chars[i] = rd32(in + 28 + 4 * i);
    for (i = 0; i < 4; i++)
        cfg->handicaps[i] = rd32(in + 44 + 4 * i);
    for (i = 0; i < 8; i++)
        cfg->favWeapons[i / 2][i % 2] = rd32(in + 60 + 4 * i);
    for (i = 0; i < 4; i++) {
        bits = rd32(in + 92 + 4 * i);
        memcpy(&cfg->slider007[i], &bits, 4);
    }
    memcpy(cfg->packHash, in + 108, 32);
    memcpy(cfg->buildId, in + 140, 20);
    if (cfg->pad0 != 0 || cfg->protocol != MATCH_CONFIG_PROTOCOL)
        return -1;
    return 0;
}
