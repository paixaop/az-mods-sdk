/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AZMOD_DATA_HELPERS_H
#define AZMOD_DATA_HELPERS_H
#include "azmod-mod.h"
#include "azmod-timestamps.h"
#include <stddef.h>
#include <string.h>

/* Read only the size word until the caller's capacity has been established. */
static inline int az_data_covers(const void *record, size_t end)
{
    uint32_t size;
    if (!record) return 0;
    memcpy(&size, record, sizeof size);
    return size >= end;
}
#define AZ_DATA_END(type, member) (offsetof(type, member) + sizeof(((type *)0)->member))
static inline int az_data_deck_header(const az_deck_snapshot *s, uint32_t deck)
{
    return deck < 4 && az_data_covers(s, AZ_DATA_END(az_deck_snapshot, deck)) &&
           s->version == AZ_DATA_VERSION && s->deck == deck;
}
static inline int az_data_receipt_time(uint64_t observed, uint64_t now)
{
    return observed && now &&
           compare_timestamps(observed, now, UINT64_C(1000000000), UINT64_C(1000000000), 1) <= AZ_TIME_EQUAL;
}
static inline int az_read_native_load_target(const az_deck_snapshot *s, uint32_t deck,
                                             uint64_t now_ns, uint64_t *generation)
{
    if (!generation || !az_data_deck_header(s, deck) ||
        !az_data_covers(s, AZ_DATA_END(az_deck_snapshot, native_load_observed_ns)) ||
        !(s->valid & AZ_LIVE_NATIVE_LOAD) || !s->native_load_unit_generation ||
        !s->native_load_generation || !az_data_receipt_time(s->native_load_observed_ns, now_ns)) return 0;
    *generation = s->native_load_generation;
    return 1;
}
static inline int az_data_loop_receipt(const az_deck_snapshot *s, uint32_t deck, uint64_t now_ns)
{
    return az_data_deck_header(s, deck) &&
           az_data_covers(s, AZ_DATA_END(az_deck_snapshot, loop_observed_ns)) &&
           az_data_covers(&s->loop, AZ_DATA_END(az_loop_state, version)) &&
           s->loop.version == AZ_DATA_VERSION && s->loop_unit_generation && s->loop_revision &&
           az_data_receipt_time(s->loop_observed_ns, now_ns);
}
static inline int az_read_loop_engaged(const az_deck_snapshot *s, uint32_t deck,
                                       uint64_t now_ns, int *engaged)
{
    if (!engaged || !az_data_loop_receipt(s, deck, now_ns) ||
        !az_data_covers(&s->loop, AZ_DATA_END(az_loop_state, engaged)) ||
        !(s->valid & AZ_LIVE_LOOP_ENGAGED) || !(s->loop.valid & AZ_LOOP_ENGAGED) ||
        (s->loop.engaged != 0 && s->loop.engaged != 1)) return 0;
    *engaged = s->loop.engaged;
    return 1;
}
static inline int az_read_loop_bounds(const az_deck_snapshot *s, uint32_t deck,
                                      uint64_t now_ns, double *in_s, double *out_s)
{
    if (!in_s || !out_s || !az_data_loop_receipt(s, deck, now_ns) ||
        !az_data_covers(&s->loop, AZ_DATA_END(az_loop_state, out_s)) ||
        !(s->valid & AZ_LIVE_LOOP_BOUNDS) || !(s->loop.valid & AZ_LOOP_BOUNDS) ||
        !__builtin_isfinite(s->loop.in_s) || !__builtin_isfinite(s->loop.out_s)) return 0;
    *in_s = s->loop.in_s;
    *out_s = s->loop.out_s;
    return 1;
}
static inline int az_command_result_matches(const az_command_result *r, uint64_t request_id)
{
    return request_id && az_data_covers(r, sizeof *r) && r->version == AZ_DATA_VERSION &&
           r->request_id == request_id && r->outcome <= AZ_COMMAND_UNCERTAIN;
}
#undef AZ_DATA_END
#endif
