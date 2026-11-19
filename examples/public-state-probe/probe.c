/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "azmod-mod.h"
#include "azmod-timestamps.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <time.h>
#include <stdatomic.h>
#define ID "sdk-state-probe"
#ifndef PROBE_MAX_RECORDS
#define PROBE_MAX_RECORDS 512u
#endif
#ifndef PROBE_MAX_TICKS
#define PROBE_MAX_TICKS 1800u
#endif
#ifndef PROBE_NATIVE_ONLY
#define PROBE_NATIVE_ONLY 0
#endif
#ifndef PROBE_CUE_ONLY
#define PROBE_CUE_ONLY 0
#endif
static const az_core_v1 *probe_core;
static const az_library_v1 *probe_library;
static int probe_tempo_fields, probe_native_fields;
#ifndef PROBE_TEMPO_LINE_BYTES
#define PROBE_TEMPO_LINE_BYTES 512u
#endif
struct probe_record {
    int rc, mt, sync, raw;
    uint32_t valid, returned;
    uint64_t mt_unit, mt_rev, mt_ns, sync_unit, sync_rev, sync_ns;
};
struct probe_tempo_record {
    int rc, tempo_valid, slider_valid, slider_locked;
    uint32_t valid, returned;
    double tempo;
    uint64_t tempo_state, tempo_rev, tempo_ns, slider_unit, slider_rev, slider_ns;
};
struct probe_native_record {
    int rc, master_valid, master, load_valid;
    uint32_t valid, returned;
    uint64_t master_rev, master_ns, native_load, load_unit, load_epoch, load_ns, source_load;
    struct probe_tempo_record tempo;
};
static uint64_t probe_now(void)
{
    struct timespec time = { 0 };
    if (clock_gettime(CLOCK_MONOTONIC, &time)) return 0;
    const int nonnegative = compare_timestamps(time.tv_sec, 0, 1, 1000000000, 1);
    const int nanos = compare_timestamps(time.tv_nsec, 1000000000L, 1000000000, 1000000000, 1);
    if (nonnegative == AZ_TIME_INVALID || nonnegative == AZ_TIME_BEFORE || nanos == AZ_TIME_INVALID || nanos != AZ_TIME_BEFORE) return 0;
    uint64_t seconds = (uint64_t)time.tv_sec;
    const int range = compare_timestamps(seconds, (UINT64_MAX - (uint64_t)time.tv_nsec) / UINT64_C(1000000000), 1, 1000000000, 1);
    if (range == AZ_TIME_INVALID || range == AZ_TIME_AFTER) return 0;
    return seconds * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}
static int probe_snapshot_valid(int rc, const az_deck_snapshot *s, size_t end)
{
    return rc == AZ_OK && s && s->size >= end && s->version == AZ_DATA_VERSION;
}
static int probe_field_available(const az_deck_snapshot *s, int deck, uint32_t field)
{
    return (s->valid & field) && s->deck == (uint32_t)deck;
}
static int probe_receipt_valid(uint64_t unit, uint64_t revision, uint64_t observed, uint64_t query)
{
    if (!unit || !revision || !observed) return 0;
    int order = compare_timestamps(observed, query, 1000000000, 1000000000, 1);
    if (order == AZ_TIME_INVALID) return 0;
    return order == AZ_TIME_BEFORE || order == AZ_TIME_EQUAL;
}
static const char *probe_query_status(uint64_t query, uint64_t started)
{
    if (!query || compare_timestamps(query, started, 1000000000, 1000000000, 1) == AZ_TIME_BEFORE) return "clock";
    if (compare_timestamps(query - started, UINT64_C(1800000000000), 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL) return "deadline";
    return NULL;
}
static int probe_record_room(unsigned records)
{
    return records < PROBE_MAX_RECORDS;
}
static const char *probe_delay(unsigned records, unsigned tick)
{
    if (!probe_record_room(records) || tick + 1 >= PROBE_MAX_TICKS) return NULL;
    const struct timespec delay = { 1, 0 };
    if (nanosleep(&delay, NULL) && errno != EINTR) return "sleep";
    return NULL;
}
/* Every mode shares the same total record, tick and wall-clock budget. The
 * poll callback owns its receipts and may emit several records for one deck. */
static const char *probe_loop(unsigned *records, uint64_t started,
                              const char *(*poll)(void *, int, unsigned), void *state)
{
    if (!started) return "clock";
    for (unsigned tick = 0; tick < PROBE_MAX_TICKS && probe_record_room(*records); ++tick) {
        for (int deck = 0; deck < 4 && probe_record_room(*records); ++deck) {
            const char *reason = poll(state, deck, tick);
            if (reason) return reason;
        }
        const char *reason = probe_delay(*records, tick);
        if (reason) return reason;
    }
    return *records == PROBE_MAX_RECORDS ? "record_limit" : "ticks";
}
static _Atomic uint64_t probe_cue_events[4];
static void probe_cue_event(int deck, int what, void *user)
{
    (void)user;
    if (deck < 0 || deck >= 4 || what != AZ_DECK_CUES_CHANGED) return;
    uint64_t stamp = probe_now();
    if (compare_timestamps(stamp, atomic_load_explicit(&probe_cue_events[deck], memory_order_relaxed), 1000000000, 1000000000, 1) == AZ_TIME_AFTER)
        atomic_store_explicit(&probe_cue_events[deck], stamp, memory_order_release);
}
struct probe_cue_record {
    int rc, available;
    uint32_t valid, returned;
    uint64_t unit, revision, observed_ns, event_ns, source_load;
};
static struct probe_cue_record probe_cue_values(int deck, uint64_t query, int rc, const az_deck_snapshot *s)
{
    struct probe_cue_record v = { 0 };
    v.rc = rc;
    v.returned = UINT32_MAX;
    v.event_ns = atomic_load_explicit(&probe_cue_events[deck], memory_order_acquire);
    if (compare_timestamps(v.event_ns, query, 1000000000, 1000000000, 1) == AZ_TIME_AFTER) v.event_ns = 0; /* Callback may follow this poll's query. */
    if (!probe_snapshot_valid(rc, s, offsetof(az_deck_snapshot, cue_observed_ns) + sizeof s->cue_observed_ns)) return v;
    v.valid = s->valid;
    v.returned = s->deck;
    v.source_load = s->load_generation;
    v.unit = s->cue_unit_generation;
    v.revision = s->cue_revision;
    v.observed_ns = s->cue_observed_ns;
    v.available = probe_field_available(s, deck, AZ_LIVE_CUE_REVISION) && probe_receipt_valid(v.unit, v.revision, v.observed_ns, query);
    return v;
}
static int probe_cue_changed(const struct probe_cue_record *a, const struct probe_cue_record *b)
{
    return a->rc != b->rc ||
           a->available != b->available ||
           a->valid != b->valid ||
           a->returned != b->returned ||
           a->unit != b->unit ||
           a->revision != b->revision ||
           a->source_load != b->source_load ||
           compare_timestamps(a->observed_ns, b->observed_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL ||
           compare_timestamps(a->event_ns, b->event_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL;
}
static int probe_cue_emit(unsigned seq, int deck, uint64_t query, const struct probe_cue_record *v)
{
    char line[512];
    int n = snprintf(line, sizeof line,
                     "SDK_PUBLIC_CUE v=1 seq=%u deck=%d returned=%u query_ns=%llu rc=%d valid=%u cue_valid=%d cue_unit=%llu cue_rev=%llu cue_ns=%llu event_ns=%llu source_load=%llu",
                     seq, deck + 1, v->returned < 4 ? v->returned + 1 : 0, (unsigned long long)query, v->rc, v->valid, v->available,
                     (unsigned long long)v->unit, (unsigned long long)v->revision, (unsigned long long)v->observed_ns,
                     (unsigned long long)v->event_ns, (unsigned long long)v->source_load);
    if (n < 0 || (size_t)n >= sizeof line) return 0;
    probe_core->log(ID, "%s", line);
    return 1;
}
struct probe_cue_session {
    unsigned records;
    uint64_t started;
    int seen[4];
    struct probe_cue_record previous[4];
};
static const char *probe_cue_poll(void *state, int deck, unsigned tick)
{
    struct probe_cue_session *session = state;
    az_deck_snapshot s = { .size = sizeof s };
    int rc = probe_library->deck_snapshot(deck, &s);
    uint64_t query = probe_now();
    const char *reason = probe_query_status(query, session->started);
    if (reason) return reason;
    struct probe_cue_record v = probe_cue_values(deck, query, rc, &s);
    if (!session->seen[deck] || tick % 10u == 0 || probe_cue_changed(&v, &session->previous[deck])) {
        if (!probe_cue_emit(session->records + 1, deck, query, &v)) return "format_limit";
        session->records++;
        session->previous[deck] = v;
        session->seen[deck] = 1;
    }
    return NULL;
}
static void *probe_cue_worker(void *unused)
{
    (void)unused;
    struct probe_cue_session session = { .started = probe_now() };
    const char *reason = probe_loop(&session.records, session.started, probe_cue_poll, &session);
    probe_core->log(ID, "SDK_PUBLIC_CUE complete reason=%s records=%u", reason, session.records);
    return NULL;
}

static struct probe_record probe_values(int rc, const az_deck_snapshot *s)
{
    struct probe_record value = { 0 };
    value.rc = rc;
    value.mt = value.sync = value.raw = -1;
    if (rc == AZ_OK) {
        value.valid = s->valid;
        value.returned = s->deck;
        if (s->valid & AZ_LIVE_MASTER_TEMPO) value.mt = s->master_tempo;
        if (s->valid & AZ_LIVE_SYNC) value.sync = s->sync;
        value.mt_unit = s->live_unit_generation;
        value.mt_rev = s->key_lock_revision;
        value.mt_ns = s->key_lock_observed_ns;
        value.sync_unit = s->sync_unit_generation;
        value.sync_rev = s->sync_revision;
        value.sync_ns = s->sync_observed_ns;
        if (value.sync_unit && value.sync_rev && value.sync_ns) value.raw = s->sync_raw_mode;
    }
    return value;
}
static int probe_state_receipts_changed(const struct probe_record *a, const struct probe_record *b)
{
    return a->mt_unit != b->mt_unit || a->mt_rev != b->mt_rev || compare_timestamps(a->mt_ns, b->mt_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL ||
           a->sync_unit != b->sync_unit || a->sync_rev != b->sync_rev || compare_timestamps(a->sync_ns, b->sync_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL;
}
static int probe_changed(const struct probe_record *a, const struct probe_record *b)
{
    return a->rc != b->rc || a->valid != b->valid || a->returned != b->returned ||
           a->mt != b->mt || a->sync != b->sync || a->raw != b->raw || probe_state_receipts_changed(a, b);
}

static int probe_emit(unsigned seq, int deck, uint64_t query_ns, const struct probe_record *v)
{
    char line[384];
    int n = snprintf(line, sizeof line,
                     "SDK_PUBLIC_STATE v=1 seq=%u deck=%d returned=%u query_ns=%llu rc=%d valid=%u mt=%d mt_unit=%llu mt_rev=%llu mt_ns=%llu sync=%d sync_unit=%llu sync_rev=%llu sync_ns=%llu raw=%d",
                     seq, deck + 1, v->returned + 1, (unsigned long long)query_ns, v->rc, v->valid, v->mt,
                     (unsigned long long)v->mt_unit, (unsigned long long)v->mt_rev, (unsigned long long)v->mt_ns,
                     v->sync, (unsigned long long)v->sync_unit, (unsigned long long)v->sync_rev, (unsigned long long)v->sync_ns, v->raw);
    if (n < 0 || (size_t)n >= sizeof line) return 0;
    probe_core->log(ID, "%s", line);
    return 1;
}
static void probe_tempo_capture(struct probe_tempo_record *v, int deck, uint64_t query, const az_deck_snapshot *s)
{
    if (s->size < offsetof(az_deck_snapshot, tempo_observed_ns) + sizeof s->tempo_observed_ns) return;
    v->tempo_state = s->tempo_state_generation;
    v->tempo_rev = s->tempo_revision;
    v->tempo_ns = s->tempo_observed_ns;
    if (probe_field_available(s, deck, AZ_LIVE_TEMPO) && query &&
        probe_receipt_valid(v->tempo_state, v->tempo_rev, v->tempo_ns, query) &&
        isfinite(s->tempo) && s->tempo > 20.0 && s->tempo < 400.0) {
        v->tempo_valid = 1;
        v->tempo = s->tempo;
    }
}
static void probe_slider_capture(struct probe_tempo_record *v, int deck, uint64_t query, const az_deck_snapshot *s)
{
    if (s->size < offsetof(az_deck_snapshot, tempo_slider_locked) + sizeof s->tempo_slider_locked) return;
    v->slider_unit = s->tempo_lock_unit_generation;
    v->slider_rev = s->tempo_lock_revision;
    v->slider_ns = s->tempo_lock_observed_ns;
    if (probe_field_available(s, deck, AZ_LIVE_TEMPO_SLIDER_LOCKED) && query &&
        probe_receipt_valid(v->slider_unit, v->slider_rev, v->slider_ns, query) &&
        (s->tempo_slider_locked == 0 || s->tempo_slider_locked == 1)) {
        v->slider_valid = 1;
        v->slider_locked = s->tempo_slider_locked;
    }
}
static struct probe_tempo_record probe_tempo_values(int deck, uint64_t query_ns, int rc, const az_deck_snapshot *s)
{
    struct probe_tempo_record value = { .rc = rc, .returned = UINT32_MAX, .slider_locked = -1 };
    if (!probe_snapshot_valid(rc, s, offsetof(az_deck_snapshot, deck) + sizeof s->deck)) return value;
    value.valid = s->valid;
    value.returned = s->deck;
    probe_tempo_capture(&value, deck, query_ns, s);
    probe_slider_capture(&value, deck, query_ns, s);
    return value;
}

static int probe_tempo_changed(const struct probe_tempo_record *a, const struct probe_tempo_record *b)
{
    /* GUI BPM poll receipts advance even when the value is unchanged. Log the
     * newest clocks periodically, without spending the budget on receipt-only bumps. */
    const uint32_t fields = AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED;
    return a->rc != b->rc || (a->valid & fields) != (b->valid & fields) || a->returned != b->returned ||
           a->tempo_valid != b->tempo_valid || a->tempo != b->tempo || a->tempo_state != b->tempo_state ||
           a->slider_valid != b->slider_valid || a->slider_locked != b->slider_locked || a->slider_unit != b->slider_unit;
}
static int probe_tempo_emit(unsigned seq, int deck, uint64_t query_ns, const struct probe_tempo_record *v)
{
    char line[PROBE_TEMPO_LINE_BYTES];
    /* Qualified BPM is0 or20..400. Integer formatting keeps the decimal point
     * stable without changing the host process's numeric locale. */
    uint32_t decimal = (uint32_t)(v->tempo * 1000000.0 + 0.5);
    int n = snprintf(line, sizeof line,
                     "SDK_PUBLIC_TEMPO v=1 seq=%u deck=%d returned=%u query_ns=%llu rc=%d valid=%u tempo_valid=%d tempo=%u.%06u tempo_state=%llu tempo_rev=%llu tempo_ns=%llu slider_valid=%d slider_locked=%d slider_unit=%llu slider_rev=%llu slider_ns=%llu",
                     seq, deck + 1, v->returned < 4 ? v->returned + 1 : 0, (unsigned long long)query_ns,
                     v->rc, v->valid, v->tempo_valid, decimal / 1000000u, decimal % 1000000u, (unsigned long long)v->tempo_state,
                     (unsigned long long)v->tempo_rev, (unsigned long long)v->tempo_ns,
                     v->slider_valid, v->slider_locked, (unsigned long long)v->slider_unit,
                     (unsigned long long)v->slider_rev, (unsigned long long)v->slider_ns);
    if (n < 0 || (size_t)n >= sizeof line) return 0;
    probe_core->log(ID, "%s", line);
    return 1;
}
static void probe_master_capture(struct probe_native_record *v, int deck, uint64_t query, const az_deck_snapshot *s)
{
    if (s->size < offsetof(az_deck_snapshot, master_observed_ns) + sizeof s->master_observed_ns) return;
    v->master_rev = s->master_revision;
    v->master_ns = s->master_observed_ns;
    if (probe_field_available(s, deck, AZ_LIVE_MASTER) && query &&
        probe_receipt_valid(1, v->master_rev, v->master_ns, query) && (s->master == 0 || s->master == 1)) {
        v->master_valid = 1;
        v->master = s->master;
    }
}
static void probe_load_capture(struct probe_native_record *v, int deck, uint64_t query, const az_deck_snapshot *s)
{
    if (s->size < offsetof(az_deck_snapshot, native_load_observed_ns) + sizeof s->native_load_observed_ns) return;
    v->load_unit = s->native_load_unit_generation;
    v->load_epoch = s->native_load_invalidation_epoch;
    v->load_ns = s->native_load_observed_ns;
    if (probe_field_available(s, deck, AZ_LIVE_NATIVE_LOAD) && query && s->native_load_generation &&
        probe_receipt_valid(v->load_unit, v->load_epoch, v->load_ns, query)) {
        v->load_valid = 1;
        v->native_load = s->native_load_generation;
    }
}
static struct probe_native_record probe_native_values(int deck, uint64_t query_ns, int rc, const az_deck_snapshot *s)
{
    struct probe_native_record v = { .rc = rc, .returned = UINT32_MAX, .master = -1 };
    v.tempo = probe_tempo_values(deck, query_ns, rc, s);
    if (!probe_snapshot_valid(rc, s, offsetof(az_deck_snapshot, deck) + sizeof s->deck)) return v;
    v.valid = s->valid;
    v.returned = s->deck;
    if (s->size >= offsetof(az_deck_snapshot, load_generation) + sizeof s->load_generation) v.source_load = s->load_generation;
    probe_master_capture(&v, deck, query_ns, s);
    probe_load_capture(&v, deck, query_ns, s);
    return v;
}
static int probe_master_changed(const struct probe_native_record *a, const struct probe_native_record *b)
{
    return a->master_valid != b->master_valid || a->master != b->master || a->master_rev != b->master_rev ||
           compare_timestamps(a->master_ns, b->master_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL;
}
static int probe_load_changed(const struct probe_native_record *a, const struct probe_native_record *b)
{
    return a->load_valid != b->load_valid || a->native_load != b->native_load || a->load_unit != b->load_unit ||
           a->load_epoch != b->load_epoch || compare_timestamps(a->load_ns, b->load_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL || a->source_load != b->source_load;
}
static int probe_native_changed(const struct probe_native_record *a, const struct probe_native_record *b)
{
    const uint32_t fields = AZ_LIVE_MASTER | AZ_LIVE_NATIVE_LOAD | AZ_LIVE_TRACK;
    return a->rc != b->rc || (a->valid & fields) != (b->valid & fields) || a->returned != b->returned ||
           probe_master_changed(a, b) || probe_load_changed(a, b) || probe_tempo_changed(&a->tempo, &b->tempo);
}

static int probe_native_emit(unsigned seq, int deck, uint64_t query_ns, const struct probe_native_record *v)
{
    char line[768];
    uint32_t decimal = (uint32_t)(v->tempo.tempo * 1000000.0 + 0.5);
    int n = snprintf(line, sizeof line,
                     "SDK_PUBLIC_NATIVE v=1 seq=%u deck=%d returned=%u query_ns=%llu rc=%d valid=%u master_valid=%d master=%d master_rev=%llu master_ns=%llu load_valid=%d native_load=%llu load_unit=%llu load_epoch=%llu load_ns=%llu source_load=%llu tempo_valid=%d tempo=%u.%06u tempo_state=%llu tempo_rev=%llu tempo_ns=%llu slider_valid=%d slider_locked=%d slider_unit=%llu slider_rev=%llu slider_ns=%llu",
                     seq, deck + 1, v->returned < 4 ? v->returned + 1 : 0, (unsigned long long)query_ns, v->rc, v->valid,
                     v->master_valid, v->master, (unsigned long long)v->master_rev, (unsigned long long)v->master_ns,
                     v->load_valid, (unsigned long long)v->native_load, (unsigned long long)v->load_unit,
                     (unsigned long long)v->load_epoch, (unsigned long long)v->load_ns, (unsigned long long)v->source_load,
                     v->tempo.tempo_valid, decimal / 1000000u, decimal % 1000000u, (unsigned long long)v->tempo.tempo_state,
                     (unsigned long long)v->tempo.tempo_rev, (unsigned long long)v->tempo.tempo_ns, v->tempo.slider_valid, v->tempo.slider_locked,
                     (unsigned long long)v->tempo.slider_unit, (unsigned long long)v->tempo.slider_rev, (unsigned long long)v->tempo.slider_ns);
    if (n < 0 || (size_t)n >= sizeof line) return 0;
    probe_core->log(ID, "%s", line);
    return 1;
}
struct probe_session {
    unsigned records, state_records, tempo_records, native_records;
    int tempo_fields, native_fields;
    uint64_t started;
    struct probe_record previous[4];
    struct probe_tempo_record previous_tempo[4];
    struct probe_native_record previous_native[4];
    int seen[4], tempo_seen[4], native_seen[4];
};
static int probe_publish_state(struct probe_session *s, int deck, unsigned tick, uint64_t query,
                               const struct probe_record *value)
{
    if (PROBE_NATIVE_ONLY) return 1;
    if (s->seen[deck] && tick % 10u != 0 && !probe_changed(value, &s->previous[deck])) return 1;
    if (!probe_emit(s->state_records + 1, deck, query, value)) return 0;
    s->records++;
    s->state_records++;
    s->previous[deck] = *value;
    s->seen[deck] = 1;
    return 1;
}
static int probe_tempo_present(const struct probe_tempo_record *tempo)
{
    return (tempo->valid & (AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED)) || tempo->tempo_rev || tempo->slider_rev;
}
static int probe_native_present(const struct probe_native_record *native)
{
    return (native->valid & (AZ_LIVE_MASTER | AZ_LIVE_NATIVE_LOAD)) || native->master_rev || native->load_ns;
}
static int probe_publish_tempo(struct probe_session *s, int deck, unsigned tick, uint64_t query,
                               const struct probe_tempo_record *tempo)
{
    if (probe_tempo_present(tempo)) s->tempo_fields = 1;
    if (PROBE_NATIVE_ONLY || !s->tempo_fields || !probe_record_room(s->records)) return 1;
    if (s->tempo_seen[deck] && tick % 10u != 0 && !probe_tempo_changed(tempo, &s->previous_tempo[deck])) return 1;
    if (!probe_tempo_emit(s->tempo_records + 1, deck, query, tempo)) return 0;
    s->records++;
    s->tempo_records++;
    s->previous_tempo[deck] = *tempo;
    s->tempo_seen[deck] = 1;
    return 1;
}
static int probe_publish_native(struct probe_session *s, int deck, unsigned tick, uint64_t query,
                                const struct probe_native_record *native)
{
    if (probe_native_present(native)) s->native_fields = 1;
    if (!s->native_fields || !probe_record_room(s->records)) return 1;
    if (s->native_seen[deck] && tick % 10u != 0 && !probe_native_changed(native, &s->previous_native[deck])) return 1;
    if (!probe_native_emit(s->native_records + 1, deck, query, native)) return 0;
    s->records++;
    s->native_records++;
    s->previous_native[deck] = *native;
    s->native_seen[deck] = 1;
    return 1;
}
static const char *probe_poll(void *state, int deck, unsigned tick)
{
    struct probe_session *session = state;
    az_deck_snapshot snapshot = { .size = sizeof snapshot };
    int rc = probe_library->deck_snapshot(deck, &snapshot);
    uint64_t query_ns = probe_now();
    const char *reason = probe_query_status(query_ns, session->started);
    if (reason) return reason;
    struct probe_record value = probe_values(rc, &snapshot);
    if (!probe_publish_state(session, deck, tick, query_ns, &value)) return "format_limit";
    struct probe_tempo_record tempo = probe_tempo_values(deck, query_ns, rc, &snapshot);
    if (!probe_publish_tempo(session, deck, tick, query_ns, &tempo)) return "format_limit";
    struct probe_native_record native = probe_native_values(deck, query_ns, rc, &snapshot);
    if (!probe_publish_native(session, deck, tick, query_ns, &native)) return "format_limit";
    return NULL;
}
static void *probe_worker(void *unused)
{
    (void)unused;
    struct probe_session s = { .tempo_fields = probe_tempo_fields, .native_fields = probe_native_fields, .started = probe_now() };
    const char *reason = probe_loop(&s.records, s.started, probe_poll, &s);
    probe_core->log(ID, "SDK_PUBLIC_STATE complete reason=%s records=%u", reason, s.state_records);
    if (s.tempo_fields) probe_core->log(ID, "SDK_PUBLIC_TEMPO complete reason=%s records=%u", reason, s.tempo_records);
    if (s.native_fields) probe_core->log(ID, "SDK_PUBLIC_NATIVE complete reason=%s records=%u", reason, s.native_records);
    return NULL;
}
static int probe_core_valid(const az_core_v1 *core)
{
    return core && core->abi == AZ_MOD_ABI && AZ_HAS(core, library_service) && AZ_HAS(core, thread_start) && AZ_HAS(core, log);
}
static int probe_listen_cues(const az_core_v1 *core, uint32_t fields)
{
    if (!AZ_HAS(core, on_deck_event)) {
        core->log(ID, "SDK_PUBLIC_CUE listener unsupported");
        return -1;
    }
    int rc = core->on_deck_event(ID, probe_cue_event, NULL);
    core->log(ID, "SDK_PUBLIC_CUE listener rc=%d current_fields=%u", rc, fields);
    return rc;
}

static int init(const az_core_v1 *core)
{
    if (!probe_core_valid(core)) return -1;
    const az_library_v1 *shared = core->library_service(AZ_LIBRARY_ABI);
    if (!AZ_LIB_HAS(shared, data_capabilities) || !AZ_LIB_HAS(shared, deck_snapshot)) return -1;
    az_data_capabilities caps = { .size = sizeof caps };
    if (shared->data_capabilities(&caps) != AZ_OK) return -1;
    probe_core = core;
    probe_library = shared;
    probe_tempo_fields = !!(caps.live_fields & (AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED));
    probe_native_fields = !!(caps.live_fields & (AZ_LIVE_MASTER | AZ_LIVE_NATIVE_LOAD));
    /* live_fields reports current receipt availability. Empty units must be
     * allowed to subscribe before the first genuine cue change arrives. */
    if (PROBE_CUE_ONLY && probe_listen_cues(core, caps.live_fields)) return -1;
    core->log(ID, "SDK_PUBLIC_STATE capabilities version=%u fields=%u commands=%llu", caps.version, caps.live_fields, (unsigned long long)caps.commands);
    return core->thread_start(ID, "sdk-public-state", PROBE_CUE_ONLY ? probe_cue_worker : probe_worker, NULL) == 0 ? 0 : -1;
}
static const char *const tags[] = { "any", NULL };
static const az_mod_v1 mod = { .abi = AZ_MOD_ABI, .size = sizeof(az_mod_v1), .id = ID, .fw_tags = tags, .init = init };
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
