/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

enum WWStage { WW_IDLE, WW_REQUESTED, WW_LOADING, WW_ARRIVED, WW_FAILED };
typedef struct {
    enum WWStage stage;
    uint32_t destination;
    unsigned sequence, stable_ticks, saw_loading, saw_world_down;
    double began, steady;
    const char *error;
} WWState;

static inline uint32_t ww_map(uint32_t row) {
    switch (row) {
    case 2102950: return 0x15000000; /* Hunter's Dream */
    case 2302950: return 0x17000000; /* Old Yharnam */
    case 2412951: return 0x18010000; /* Central Yharnam */
    default: return 0xffffffff;
    }
}
static inline int ww_request(WWState *s, unsigned seq, uint32_t row,
                             double now, int world, int loading, uint32_t map) {
    if (s->stage==WW_FAILED || (s->stage!=WW_IDLE && s->stage!=WW_ARRIVED) ||
        seq!=s->sequence+1 || seq>24 || ww_map(row)==0xffffffff ||
        !world || loading || map==ww_map(row)) return 0;
    *s=(WWState){.stage=WW_REQUESTED,.destination=row,.sequence=seq,.began=now,.error=""};
    return 1;
}
static inline void ww_observe(WWState *s, double now, int world, int loading, uint32_t map) {
    if (s->stage!=WW_REQUESTED && s->stage!=WW_LOADING) return;
    if (!world) s->saw_world_down=1;
    if (loading) { s->saw_loading=1; s->stage=WW_LOADING; }
    if ((!s->saw_loading && now-s->began>25) || now-s->began>180) {
        s->stage=WW_FAILED;s->error=s->saw_loading ? "load-timeout" : "no-loading-transition";return;
    }
    if (!s->saw_loading || !world || loading || map!=ww_map(s->destination)) {
        s->steady=0;s->stable_ticks=0;return;
    }
    if (!s->stable_ticks) s->steady=now;
    ++s->stable_ticks;
    if (now-s->steady>=3 && s->stable_ticks>=30) s->stage=WW_ARRIVED;
}
