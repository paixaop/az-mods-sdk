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
    if (!observed || !now) return 0;
    int order = compare_timestamps(observed, now, UINT64_C(1000000000), UINT64_C(1000000000), 1);
    return order != AZ_TIME_INVALID && (order == AZ_TIME_BEFORE || order == AZ_TIME_EQUAL);
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

/* The host owns event, view and topic records until callback return. Check each
 * prefix separately so a scalar reader can accept a shorter topic than a
 * whole-record reader. All public readers preserve their outputs on failure. */
static inline const az_deck_status_view_v1 *az_data_event_view(const az_deck_status_event_v1 *e,
                                                               uint64_t topic, size_t view_end)
{
    if (!az_data_covers(e, AZ_DATA_END(az_deck_status_event_v1, view)) || e->version != AZ_DATA_VERSION ||
        e->deck >= 4 || e->kind < AZ_STATUS_INITIAL || e->kind > AZ_STATUS_RESYNC ||
        !(e->topics & topic) || !az_data_covers(e->view, view_end) || e->view->version != AZ_DATA_VERSION)
        return NULL;
    return e->view;
}
static inline const az_status_loop_v1 *az_data_event_loop_prefix(const az_deck_status_event_v1 *e, size_t end)
{
    const az_deck_status_view_v1 *view =
        az_data_event_view(e, AZ_STATUS_TOPIC(AZ_STATUS_LOOP), AZ_DATA_END(az_deck_status_view_v1, loop));
    if (!view || !az_data_covers(view->loop, end) || view->loop->header.version != AZ_DATA_VERSION) return NULL;
    return view->loop;
}
static inline int az_read_event_loop(const az_deck_status_event_v1 *e, const az_status_loop_v1 **out)
{
    const az_status_loop_v1 *loop = az_data_event_loop_prefix(e, sizeof *loop);
    if (!out || !loop) return 0;
    *out = loop;
    return 1;
}
static inline int az_data_event_scalar_ready(const az_deck_status_event_v1 *e, const az_status_topic_v1 *h,
                                             uint32_t field, const az_status_field_info_v1 *receipt)
{
    return (h->supported & h->valid & az_status_field_bit(field)) && receipt->info.availability == AZ_AVAIL_READY &&
           receipt->count == 1 && receipt->scope_id &&
           receipt->scope == AZ_STATUS_LOAD_SCOPE && receipt->scope_id == e->load_epoch;
}
static inline int az_read_event_loop_engaged(const az_deck_status_event_v1 *e, int *engaged)
{
    const az_status_loop_v1 *loop = az_data_event_loop_prefix(e, AZ_DATA_END(az_status_loop_v1, engaged_info));
    if (!engaged || !loop || !az_data_event_scalar_ready(e, &loop->header, AZ_STATUS_LOOP_ENGAGED, &loop->engaged_info) ||
        (loop->engaged != 0 && loop->engaged != 1)) return 0;
    *engaged = loop->engaged;
    return 1;
}
static inline int az_read_event_loop_point(const az_deck_status_event_v1 *e, uint32_t field, double *seconds)
{
    const az_status_loop_v1 *loop = az_data_event_loop_prefix(e, sizeof(az_status_topic_v1));
    if (!seconds || !loop) return 0;
    const az_status_field_info_v1 *info;
    const az_status_coord_v1 *point;
    switch (field) {
    case AZ_STATUS_LOOP_RETAINED_IN:
        if (!az_data_covers(loop, AZ_DATA_END(az_status_loop_v1, retained_in_info))) return 0;
        info = &loop->retained_in_info;
        point = &loop->retained_in;
        break;
    case AZ_STATUS_LOOP_RETAINED_OUT:
        if (!az_data_covers(loop, AZ_DATA_END(az_status_loop_v1, retained_out_info))) return 0;
        info = &loop->retained_out_info;
        point = &loop->retained_out;
        break;
    case AZ_STATUS_LOOP_WORKING_IN:
        if (!az_data_covers(loop, AZ_DATA_END(az_status_loop_v1, working_in_info))) return 0;
        info = &loop->working_in_info;
        point = &loop->working_in;
        break;
    case AZ_STATUS_LOOP_WORKING_OUT:
        if (!az_data_covers(loop, AZ_DATA_END(az_status_loop_v1, working_out_info))) return 0;
        info = &loop->working_out_info;
        point = &loop->working_out;
        break;
    default:
        return 0;
    }
    if (!az_data_event_scalar_ready(e, &loop->header, field, info) ||
        !__builtin_isfinite(point->seconds)) return 0;
    /* This scalar reader classifies malformed numeric representations only.
     * Signed zero is valid. A negative nonzero number is invalid independently
     * of the observation clock. Callers need the source rate to compare points. */
    if (__builtin_signbit(point->seconds) && !__builtin_fpclassify(0, 0, 0, 0, 1, point->seconds)) return 0;
    *seconds = point->seconds;
    return 1;
}
static inline int az_data_phrase_map_equal(const az_status_phrase_endpoint_v1 *a, const az_status_phrase_endpoint_v1 *b)
{
    return a->phrase_provider_id == b->phrase_provider_id && a->phrase_revision == b->phrase_revision &&
           a->grid.load_epoch == b->grid.load_epoch && a->grid.provider_id == b->grid.provider_id &&
           a->grid.revision == b->grid.revision && a->grid.provenance == b->grid.provenance && a->grid.grid_field == b->grid.grid_field;
}
static inline int az_data_phrase_endpoint_valid(const az_status_phrase_endpoint_v1 *p, uint64_t load)
{
    if (!load || p->grid.load_epoch != load || !p->phrase_provider_id || !p->phrase_revision || !p->grid.provider_id ||
        !p->grid.revision || p->grid.grid_field != AZ_STATUS_GRID_CURRENT ||
        p->grid.provenance < AZ_PROV_NATIVE || p->grid.provenance > AZ_PROV_DERIVED) return 0;
    if (p->kind == AZ_PHRASE_ITEM) return p->phrase.start_beat < p->phrase.end_beat;
    return (p->kind == AZ_PHRASE_START || p->kind == AZ_PHRASE_END || p->kind == AZ_PHRASE_GAP) && !p->index &&
           !p->phrase.start_beat && !p->phrase.end_beat && !p->phrase.raw_kind && !p->phrase.raw_mood &&
           !p->phrase.category && !p->phrase.reserved;
}
static inline int az_data_phrase_endpoint_equal(const az_status_phrase_endpoint_v1 *a, const az_status_phrase_endpoint_v1 *b)
{
    return az_data_phrase_map_equal(a, b) && a->kind == b->kind && a->index == b->index &&
           a->phrase.start_beat == b->phrase.start_beat && a->phrase.end_beat == b->phrase.end_beat &&
           a->phrase.raw_kind == b->phrase.raw_kind && a->phrase.raw_mood == b->phrase.raw_mood &&
           a->phrase.category == b->phrase.category && a->phrase.reserved == b->phrase.reserved;
}
static inline int az_read_phrase_transition(const az_deck_status_event_v1 *e, az_status_phrase_transition_v1 *out)
{
    const uint64_t topic = AZ_STATUS_TOPIC(AZ_STATUS_PHRASE_TRANSITION);
    if (!az_data_covers(out, sizeof *out)) return 0;
    const az_deck_status_view_v1 *view = az_data_event_view(e, topic, AZ_DATA_END(az_deck_status_view_v1, phrase_playback));
    if (!view || e->kind != AZ_STATUS_CHANGE || !(e->changed_topics & topic) ||
        !az_data_covers(e, AZ_DATA_END(az_deck_status_event_v1, phrase_transition))) return 0;
    const az_status_phrase_transition_v1 *t = e->phrase_transition;
    const az_status_phrase_playback_v1 *state = view->phrase_playback;
    if (!az_data_covers(t, sizeof *t) || t->version != AZ_DATA_VERSION || t->discontinuity > AZ_STATUS_WRAP ||
        !az_data_covers(state, AZ_DATA_END(az_status_phrase_playback_v1, active_info)) || state->header.version != AZ_DATA_VERSION ||
        !az_data_event_scalar_ready(e, &state->header, AZ_STATUS_PHRASE_TRANSITION_ACTIVE, &state->active_info) ||
        state->active_info.scope != AZ_STATUS_LOAD_SCOPE || !state->active ||
        !az_data_phrase_endpoint_valid(&t->from, e->load_epoch) || !az_data_phrase_endpoint_valid(&t->to, e->load_epoch) ||
        !az_data_phrase_map_equal(&t->from, &t->to) || (t->from.kind == t->to.kind && t->from.index == t->to.index) ||
        !az_data_phrase_endpoint_equal(&t->to, state->active)) return 0;
    uint32_t capacity = out->size;
    *out = *t;
    out->size = capacity;
    return 1;
}
#undef AZ_DATA_END
#endif
