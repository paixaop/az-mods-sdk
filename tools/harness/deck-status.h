/* SPDX-License-Identifier: GPL-3.0-only */
/* Scripted UI-thread publications for the standalone SDK harness. Each record
 * owns its scalar values. Nested collection pointers refer only to immutable
 * fixture data with process lifetime. Core tests exercise the native writers. */
#define HS_TOPICS(X)                                                                                \
    X(LOAD, load, 4)                                                                                \
    X(METADATA, metadata, 8)                                                                        \
    X(KEY, key, 3)                                                                                  \
    X(CUES, cues, 5) X(LOOP, loop, 9)                                                               \
        X(GRID, grid, 2) X(PHRASES, phrases, 4) X(TRANSPORT, transport, 6) X(POSITION, position, 4) \
            X(TEMPO, tempo, 4) X(SYNC, sync, 5) X(SETTINGS, settings, 5) X(PHASE, phase, 2)         \
                X(STEMS, stems, 2) X(OVERVIEW, overview, 1) X(PHRASE_TRANSITION, phrase_playback, 1)
struct hs_image {
#define HS_RECORD(tag, name, count) az_status_##name##_v1 name;
    HS_TOPICS(HS_RECORD)
#undef HS_RECORD
};
#define HS_HISTORY 256
struct hs_record {
    struct hs_image image;
    char title[192];
    uint64_t sequence, epoch, observed, topics, motion_sequence;
    int motion_barrier;
    az_status_occurrence_v1 occurrence;
    az_status_phrase_transition_v1 pair;
    az_status_phrase_endpoint_v1 active;
};
static struct {
    struct hs_record current, history[HS_HISTORY], motion;
    uint64_t discrete;
    uint64_t evicted[AZ_STATUS_TOPIC_COUNT];
} hs_decks[4];
static struct hs_listener {
    az_deck_subscription handle;
    az_deck_watch_v1 watch;
    az_deck_status_fn fn;
    void *user;
    unsigned initial;
    uint64_t after[4], discrete[4], due[4], evicted_motion[4];
    struct hs_record baseline[4];
} hs_listeners[4];
static uint64_t hs_next_handle, hs_next_epoch;
static pthread_t hs_ui;
static int hs_draining;
static uint64_t hs_deadline;
static void hs_deck_changed(unsigned d, int what);

static void hs_reset(void)
{
    hs_ui = pthread_self();
    hs_deadline = 0;
    memset(hs_listeners, 0, sizeof hs_listeners);
    memset(hs_decks, 0, sizeof hs_decks);
    for (unsigned d = 0; d < 4; ++d) {
        struct hs_record *r = &hs_decks[d].current;
        r->sequence = 1;
        r->observed = (uint64_t)mono_ns;
#define HS_INIT(tag, n, count) r->image.n.header = (az_status_topic_v1){ sizeof r->image.n, AZ_DATA_VERSION, 0, 0, 0 };
        HS_TOPICS(HS_INIT)
#undef HS_INIT
        hs_deck_changed(d, D.loaded[d] ? AZ_DECK_LOADED : AZ_DECK_EJECTED);
        if (D.playing[d]) hs_deck_changed(d, AZ_DECK_PLAY);
    }
}
static unsigned hs_count(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 4; ++i) n += hs_listeners[i].handle != 0;
    return n;
}
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#else
#include <link.h>
static int hs_code_segment(struct dl_phdr_info *image, size_t size, void *context)
{
    (void)size;
    uintptr_t address = *(const uintptr_t *)context;
    for (unsigned i = 0; i < image->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &image->dlpi_phdr[i];
        uintptr_t start = image->dlpi_addr + p->p_vaddr;
        if (p->p_type == PT_LOAD && (p->p_flags & PF_X) && address >= start && address - start < p->p_memsz) return 1;
    }
    return 0;
}
#endif
static int hs_mod_code(void *pointer)
{
    Dl_info image;
    if (!pointer || !mod_base || !dladdr(pointer, &image) || image.dli_fbase != mod_base) return 0;
    uintptr_t address = (uintptr_t)pointer;
#ifdef __APPLE__
    mach_vm_address_t start = address;
    mach_vm_size_t length = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t rc = mach_vm_region(mach_task_self(), &start, &length, VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info, &count, &object);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    return rc == KERN_SUCCESS && address >= start && address - start < length && (info.protection & VM_PROT_EXECUTE);
#else
    return dl_iterate_phdr(hs_code_segment, &address);
#endif
}
static int hs_owner(const char *id, void *caller)
{
    return pthread_equal(pthread_self(), hs_ui) && ctx != CTX_NONE && id && mod_id && !strcmp(id, mod_id) &&
           hs_mod_code(caller);
}
static __attribute__((noinline)) int hs_subscribe(const char *id, const az_deck_watch_v1 *w, az_deck_status_fn fn, void *user, az_deck_subscription *out)
{
    if (!hs_owner(id, __builtin_return_address(0)) || !w || ht_record_size(w) < sizeof *w ||
        w->version != AZ_DATA_VERSION || !w->deck_mask || (w->deck_mask & ~AZ_STATUS_ALL_DECKS) ||
        !w->topics || (w->topics & ~AZ_STATUS_ALL_V1) || w->position_hz > 30 || !out || !fn ||
        !hs_mod_code((void *)fn)) return AZ_INVALID;
    for (unsigned i = 0; i < 4 && hs_next_handle != UINT64_MAX; ++i) {
        struct hs_listener *s = &hs_listeners[i];
        if (s->handle) continue;
        memset(s, 0, sizeof *s);
        s->watch = *w;
        s->fn = fn;
        s->user = user;
        s->initial = w->deck_mask;
        for (unsigned d = 0; d < 4; ++d) {
            s->baseline[d] = hs_decks[d].current;
            s->after[d] = s->baseline[d].sequence;
            s->discrete[d] = hs_decks[d].discrete;
        }
        *out = s->handle = ++hs_next_handle;
        return AZ_OK;
    }
    return AZ_LIMIT;
}
static __attribute__((noinline)) int hs_release(const char *id, az_deck_subscription h)
{
    if (!hs_owner(id, __builtin_return_address(0)) || !h || h > hs_next_handle) return AZ_INVALID;
    for (unsigned i = 0; i < 4; ++i) {
        if (hs_listeners[i].handle == h) {
            hs_listeners[i].handle = 0;
            return AZ_OK;
        }
    }
    return AZ_STALE;
}
static void hs_deliver(const struct hs_record *source, unsigned d, uint32_t kind, uint64_t lost, uint32_t reason, struct hs_listener *s)
{
    struct hs_record copy = *source;
    az_deck_status_view_v1 v = { .size = sizeof v, .version = AZ_DATA_VERSION };
    az_deck_status_event_v1 e = { .size = sizeof e, .version = AZ_DATA_VERSION, .kind = kind, .deck = d, .subscription = s->handle, .sequence = copy.sequence, .load_epoch = copy.epoch, .observed_ns = copy.observed, .published_ns = copy.observed, .topics = s->watch.topics, .lost_topics = lost, .loss_reason = reason, .view = &v };
    e.changed_topics = kind == AZ_STATUS_CHANGE ? e.topics & copy.topics : e.topics;
    if (!e.changed_topics) return;
    if (kind != AZ_STATUS_CHANGE || (e.changed_topics & (AZ_STATUS_TOPIC(AZ_STATUS_POSITION) | AZ_STATUS_TOPIC(AZ_STATUS_PHASE)))) {
        unsigned hz = s->watch.position_hz ? s->watch.position_hz : 30;
        s->due[d] = (uint64_t)mono_ns + (NS + hz - 1) / hz;
    }
#define HS_SELECT(tag, n, count)                                                                  \
    if (e.topics & AZ_STATUS_TOPIC(AZ_STATUS_##tag)) {                                            \
        v.n = &copy.image.n;                                                                      \
        if (kind != AZ_STATUS_CHANGE) copy.image.n.header.changed = (UINT64_C(1) << (count)) - 1; \
    }
    HS_TOPICS(HS_SELECT)
#undef HS_SELECT
    if (copy.image.metadata.title_info.info.availability == AZ_AVAIL_READY) copy.image.metadata.title = copy.title;
    if (copy.image.phrase_playback.active_info.info.availability == AZ_AVAIL_READY) copy.image.phrase_playback.active = &copy.active;
    if (kind == AZ_STATUS_CHANGE) {
        if (copy.occurrence.size && (e.topics & (AZ_STATUS_TOPIC(AZ_STATUS_TRANSPORT) | AZ_STATUS_TOPIC(AZ_STATUS_POSITION))))
            e.occurrence = &copy.occurrence;
        if (copy.pair.size && (e.topics & AZ_STATUS_TOPIC(AZ_STATUS_PHRASE_TRANSITION))) e.phrase_transition = &copy.pair;
    }
    int previous = ctx;
    ctx = CTX_UI;
    arm(1);
    s->fn(&e, s->user);
    arm(0);
    ctx = previous;
}
static void hs_cover_motion(struct hs_record *r, uint64_t after);
/* The script may batch physical actions before the simulated UI runs. */
static void hs_commit(unsigned d, struct hs_record *next)
{
    struct hs_record *current = &hs_decks[d].current;
    if (current->sequence == UINT64_MAX) abort();
    next->sequence = current->sequence + 1;
    next->observed = (uint64_t)mono_ns;
    if (hs_decks[d].discrete == UINT64_MAX) abort();
    struct hs_record *slot = &hs_decks[d].history[++hs_decks[d].discrete % HS_HISTORY];
    if (slot->motion_barrier) {
        for (unsigned i = 0; i < 4; ++i) {
            struct hs_listener *s = &hs_listeners[i];
            if (!s->handle || !(s->watch.deck_mask & (1u << d)) || slot->sequence <= s->after[d]) continue;
            struct hs_record r = *slot;
            hs_cover_motion(&r, s->after[d]);
            s->evicted_motion[d] |= r.topics & s->watch.topics &
                (AZ_STATUS_TOPIC(AZ_STATUS_POSITION) | AZ_STATUS_TOPIC(AZ_STATUS_PHASE));
        }
    }
    for (unsigned t = 0; t < AZ_STATUS_TOPIC_COUNT; ++t)
        if (slot->topics & AZ_STATUS_TOPIC(t)) hs_decks[d].evicted[t] = slot->sequence;
    next->motion_barrier |= next->epoch != current->epoch || next->occurrence.size ||
        (next->image.transport.header.changed & az_status_field_bit(AZ_STATUS_TRANSPORT_DIRECTION)) != 0;
    *slot = *current = *next;
}
/* A discrete publication carries the current image, including any motion it
 * covers. Recover changed motion members from their own publication revisions. */
static void hs_cover_motion(struct hs_record *r, uint64_t after)
{
    az_status_position_v1 *position = &r->image.position;
    az_status_phase_v1 *phase = &r->image.phase;
    if (position->head_info.info.revision > after) position->header.changed |= az_status_field_bit(AZ_STATUS_POSITION_HEAD);
    if (position->remaining_s_info.info.revision > after) position->header.changed |= az_status_field_bit(AZ_STATUS_POSITION_REMAINING_S);
    if (phase->beat_info.info.revision > after) phase->header.changed |= az_status_field_bit(AZ_STATUS_PHASE_BEAT);
    if (position->header.changed) r->topics |= AZ_STATUS_TOPIC(AZ_STATUS_POSITION);
    if (phase->header.changed) r->topics |= AZ_STATUS_TOPIC(AZ_STATUS_PHASE);
}
static void hs_drain(void)
{
    if (hs_draining) return;
    hs_draining = 1;
    hs_deadline = 0;
    uint64_t admitted = hs_next_handle;
    for (unsigned i = 0; i < 4; ++i) {
        struct hs_listener *s = &hs_listeners[i];
        uint64_t h = s->handle;
        if (!h || h > admitted) continue;
        for (unsigned d = 0; d < 4 && h == s->handle; ++d) {
            if (!(s->watch.deck_mask & (1u << d))) continue;
            if (s->initial & (1u << d)) {
                s->initial &= ~(1u << d);
                hs_deliver(&s->baseline[d], d, AZ_STATUS_INITIAL, 0, 0, s);
            }
            const struct hs_record *last = &hs_decks[d].current;
            uint64_t loss = 0;
            for (unsigned t = 0; t < AZ_STATUS_TOPIC_COUNT; ++t)
                if (hs_decks[d].evicted[t] > s->after[d]) loss |= AZ_STATUS_TOPIC(t);
            loss &= s->watch.topics;
            loss |= s->evicted_motion[d];
            uint64_t discrete = hs_decks[d].discrete;
            if (discrete - s->discrete[d] > HS_HISTORY) s->discrete[d] = discrete - HS_HISTORY;
            if (h == s->handle && loss) {
                struct hs_record recovery = *last;
                uint64_t skipped = 0;
                while (s->discrete[d] < discrete) {
                    const struct hs_record *r = &hs_decks[d].history[++s->discrete[d] % HS_HISTORY];
                    if (r->sequence <= s->after[d]) continue;
                    recovery = *r;
                    if (recovery.motion_barrier) hs_cover_motion(&recovery, s->after[d]);
                    skipped = recovery.topics & s->watch.topics;
                    break;
                }
                s->after[d] = recovery.sequence;
                s->evicted_motion[d] = 0;
                hs_deliver(&recovery, d, AZ_STATUS_RESYNC, loss | skipped, AZ_STATUS_EVICTED | (skipped ? AZ_STATUS_CUT_SKIPPED : 0), s);
            }
            if (h != s->handle) break;
            while (h == s->handle && s->discrete[d] < discrete) {
                struct hs_record r = hs_decks[d].history[++s->discrete[d] % HS_HISTORY];
                if (r.sequence <= s->after[d]) continue;
                if (r.motion_sequence > s->after[d] && (r.motion_barrier || (r.topics & s->watch.topics)))
                    hs_cover_motion(&r, s->after[d]);
                if (!(r.topics & s->watch.topics)) continue;
                s->after[d] = r.sequence;
                hs_deliver(&r, d, AZ_STATUS_CHANGE, 0, 0, s);
            }
            const struct hs_record *motion = &hs_decks[d].motion;
            if (h != s->handle || motion->sequence <= s->after[d]) continue;
            struct hs_record sample = *motion;
            hs_cover_motion(&sample, s->after[d]);
            if (!(sample.topics & s->watch.topics)) continue;
            int due = compare_timestamps(mono_ns, s->due[d], NS, NS, 1);
            if (due == AZ_TIME_INVALID) continue;
            if (due == AZ_TIME_BEFORE) {
                int earlier = compare_timestamps(s->due[d], hs_deadline, NS, NS, 1);
                if (!hs_deadline || (earlier != AZ_TIME_INVALID && earlier == AZ_TIME_BEFORE)) hs_deadline = s->due[d];
                continue;
            }
            s->after[d] = sample.sequence;
            hs_deliver(&sample, d, AZ_STATUS_CHANGE, 0, 0, s);
        }
    }
    hs_draining = 0;
}
static void hs_advance(void)
{
    if (!hs_deadline) return;
    int order = compare_timestamps(mono_ns, hs_deadline, NS, NS, 1);
    if (order != AZ_TIME_INVALID && (order == AZ_TIME_EQUAL || order == AZ_TIME_AFTER)) hs_drain();
}
static struct hs_record hs_next(unsigned d)
{
    struct hs_record r = hs_decks[d].current;
    r.topics = 0;
    r.motion_barrier = 0;
    memset(&r.occurrence, 0, sizeof r.occurrence);
    memset(&r.pair, 0, sizeof r.pair);
#define HS_CLEAR(tag, n, count) r.image.n.header.changed = 0;
    HS_TOPICS(HS_CLEAR)
#undef HS_CLEAR
    return r;
}
static void hs_field(struct hs_record *r, unsigned d, unsigned topic, az_status_topic_v1 *header,
                     uint32_t field, az_status_field_info_v1 *info, int availability, int scope, uint32_t count)
{
    uint64_t bit = az_status_field_bit(field);
    header->supported |= bit;
    header->changed |= bit;
    if (availability == AZ_AVAIL_READY) header->valid |= bit;
    else header->valid &= ~bit;
    *info = (az_status_field_info_v1){ .info = { (uint32_t)availability, AZ_PROV_NATIVE, r->sequence + 1, (uint64_t)mono_ns },
                                       .scope = (uint32_t)scope,
                                       .count = count,
                                       .scope_id = scope == AZ_STATUS_LOAD_SCOPE ? r->epoch : d + 1,
                                       .provider_id = d + 1 };
    r->topics |= AZ_STATUS_TOPIC(topic);
}
#define HS_SET(r, d, topic, member, field, selector, status, scope, count)                           \
    hs_field((r), (d), AZ_STATUS_##topic, &(r)->image.member.header, AZ_STATUS_##topic##_##selector, \
             &(r)->image.member.field##_info, (status), (scope), (count))
static void hs_motion(unsigned d, int64_t frame)
{
    struct hs_record r = hs_next(d);
    const uint64_t source_rate = 48000; /* this fixture's native sample clock */
    int extent = compare_timestamps(frame, 4 * source_rate, source_rate, source_rate, 1);
    if (!r.epoch || extent == AZ_TIME_INVALID || extent == AZ_TIME_AFTER) return;
    az_status_position_v1 *p = &r.image.position;
    if (p->head_info.info.availability == AZ_AVAIL_READY && p->head.rate_hz == source_rate) {
        int order = compare_timestamps(p->head.samples, frame, source_rate, source_rate, 1);
        if (order != AZ_TIME_INVALID && order == AZ_TIME_EQUAL) return;
    }
    p->head = (az_status_coord_v1){ (double)frame / source_rate, frame, (uint32_t)source_rate, 0 };
    p->remaining_s = r.image.metadata.duration_s - p->head.seconds;
    HS_SET(&r, d, POSITION, position, head, HEAD, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    HS_SET(&r, d, POSITION, position, remaining_s, REMAINING_S, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    p->remaining_s_info.info.provenance = AZ_PROV_DERIVED;
    double beat;
    if (ht_time_to_beat_rate(&(az_track_snapshot){ .size = sizeof(az_track_snapshot), .version = AZ_DATA_VERSION, .grid_info = { .availability = AZ_AVAIL_READY }, .beat_count = r.image.grid.current_info.count, .beats = r.image.grid.current }, p->head.seconds, &beat, (uint32_t)source_rate) == AZ_OK) {
        double phase = beat - floor(beat);
        if (r.image.phase.beat_info.info.availability != AZ_AVAIL_READY || r.image.phase.beat != phase) {
            r.image.phase.beat = phase;
            HS_SET(&r, d, PHASE, phase, beat, BEAT, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
            r.image.phase.beat_info.info.provenance = AZ_PROV_DERIVED;
        }
    } else if (r.image.phase.beat_info.info.availability == AZ_AVAIL_READY) {
        HS_SET(&r, d, PHASE, phase, beat, BEAT, AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, 0);
    }
    if (r.sequence == UINT64_MAX) abort();
    const struct hs_image *before = &hs_decks[d].current.image;
    r.motion_barrier = p->head_info.info.availability != before->position.head_info.info.availability ||
        p->remaining_s_info.info.availability != before->position.remaining_s_info.info.availability ||
        r.image.phase.beat_info.info.availability != before->phase.beat_info.info.availability;
    r.motion_sequence = r.sequence + 1;
    if (r.motion_barrier) hs_commit(d, &r);
    else {
        ++r.sequence;
        r.observed = (uint64_t)mono_ns;
        hs_decks[d].current = r;
    }
    hs_decks[d].motion = r;
}
/* A single immutable export with independently identified file/native revisions.
 * Changing the file fixture affects only subsequent publications. */
static const az_waveform hs_wave = { sizeof hs_wave, AZ_DATA_VERSION, AZ_WAVE_PREVIEW, sizeof ht_wave, 0, 1.0, ht_wave };
static void hs_track_fields(struct hs_record *r, unsigned d, int loaded)
{
    struct hs_image *v = &r->image;
    unsigned a = loaded ? AZ_AVAIL_READY : AZ_AVAIL_EMPTY;
    const uint32_t n = loaded ? 1 : 0;
    const uint32_t nb = loaded ? sizeof ht_beats / sizeof *ht_beats : 0;
    snprintf(r->title, sizeof r->title, "Harness track / deck %u", d + 1);
    v->load.track = (az_track_ref){ sizeof(az_track_ref), AZ_DATA_VERSION, 1, 1, 1, 100 + d, 0 };
    v->load.source = AZ_STATUS_SOURCE_USB1;
    HS_SET(r, d, LOAD, load, track, TRACK, a, AZ_STATUS_LOAD_SCOPE, n);
    HS_SET(r, d, LOAD, load, source, SOURCE, a, AZ_STATUS_LOAD_SCOPE, n);
    v->metadata.duration_s = 4.0;
    HS_SET(r, d, METADATA, metadata, title, TITLE, a, AZ_STATUS_LOAD_SCOPE, n ? strlen(r->title) : 0);
    HS_SET(r, d, METADATA, metadata, duration_s, DURATION_S, a, AZ_STATUS_LOAD_SCOPE, n);
    v->cues.live_hot = v->cues.saved_hot = loaded ? &ht_cues[1] : NULL;
    v->cues.live_memory = v->cues.saved_memory = loaded ? ht_cues : NULL;
    HS_SET(r, d, CUES, cues, live_hot, LIVE_HOT, a, AZ_STATUS_LOAD_SCOPE, n);
    HS_SET(r, d, CUES, cues, live_memory, LIVE_MEMORY, a, AZ_STATUS_LOAD_SCOPE, n);
    HS_SET(r, d, CUES, cues, saved_hot, SAVED_HOT, a, AZ_STATUS_LOAD_SCOPE, n);
    HS_SET(r, d, CUES, cues, saved_memory, SAVED_MEMORY, a, AZ_STATUS_LOAD_SCOPE, n);
    v->cues.saved_hot_info.info.provenance = v->cues.saved_memory_info.info.provenance = AZ_PROV_FILE;
    v->grid.current = v->grid.saved = loaded ? ht_beats : NULL;
    HS_SET(r, d, GRID, grid, current, CURRENT, a, AZ_STATUS_LOAD_SCOPE, nb);
    HS_SET(r, d, GRID, grid, saved, SAVED, a, AZ_STATUS_LOAD_SCOPE, nb);
    v->grid.current_info.info.provenance = v->grid.saved_info.info.provenance = AZ_PROV_FILE;
    v->phrases.current = v->phrases.saved = loaded ? ht_phrases : NULL;
    HS_SET(r, d, PHRASES, phrases, current, CURRENT, a, AZ_STATUS_LOAD_SCOPE, n);
    HS_SET(r, d, PHRASES, phrases, saved, SAVED, a, AZ_STATUS_LOAD_SCOPE, n);
    v->phrases.saved_info.info.provenance = v->phrases.current_info.info.provenance = AZ_PROV_FILE;
    v->phrases.current_grid = (az_status_grid_ref_v1){ r->epoch, d + 1, v->grid.current_info.info.revision, AZ_PROV_FILE, AZ_STATUS_GRID_CURRENT };
    v->phrases.saved_grid = v->phrases.current_grid;
    v->phrases.saved_grid.grid_field = AZ_STATUS_GRID_SAVED;
    HS_SET(r, d, PHRASES, phrases, current_grid, CURRENT_GRID, a, AZ_STATUS_LOAD_SCOPE, n);
    HS_SET(r, d, PHRASES, phrases, saved_grid, SAVED_GRID, a, AZ_STATUS_LOAD_SCOPE, n);
    v->overview.saved = loaded ? &hs_wave : NULL;
    HS_SET(r, d, OVERVIEW, overview, saved, SAVED, a, AZ_STATUS_LOAD_SCOPE, n);
    v->overview.saved_info.info.provenance = AZ_PROV_FILE;
    r->active = (az_status_phrase_endpoint_v1){ .kind = AZ_PHRASE_START, .phrase_provider_id = d + 1, .phrase_revision = v->phrases.current_info.info.revision, .grid = v->phrases.current_grid };
    HS_SET(r, d, PHRASE_TRANSITION, phrase_playback, active, ACTIVE, a, AZ_STATUS_LOAD_SCOPE, n);
}
/* The soak owns the deck mutation. Publish at that point even for tick-less mods. */
static void hs_deck_changed(unsigned d, int what)
{
    struct hs_record r = hs_next(d);
    if (what == AZ_DECK_LOADED || what == AZ_DECK_EJECTED) {
        int loaded = what == AZ_DECK_LOADED;
        if (loaded && hs_next_epoch == UINT64_MAX) abort();
        r.epoch = loaded ? ++hs_next_epoch : 0;
        hs_track_fields(&r, d, loaded);
        hs_decks[d].motion = (struct hs_record){ 0 };
        r.motion_sequence = 0;
        HS_SET(&r, d, POSITION, position, head, HEAD, loaded ? AZ_AVAIL_LOADING : AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, 0);
        HS_SET(&r, d, POSITION, position, remaining_s, REMAINING_S, loaded ? AZ_AVAIL_LOADING : AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, 0);
        HS_SET(&r, d, PHASE, phase, beat, BEAT, loaded ? AZ_AVAIL_LOADING : AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, 0);
        r.image.load.state = loaded ? AZ_STATUS_LOAD_LOADED : AZ_STATUS_LOAD_EMPTY;
        HS_SET(&r, d, LOAD, load, state, STATE, AZ_AVAIL_READY, AZ_STATUS_UNIT, 1);
        r.image.transport.state = AZ_PLAYBACK_PAUSED;
        HS_SET(&r, d, TRANSPORT, transport, state, STATE, loaded ? AZ_AVAIL_READY : AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, loaded);
        r.image.loop.engaged = 0;
        HS_SET(&r, d, LOOP, loop, engaged, ENGAGED, loaded ? AZ_AVAIL_READY : AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, loaded);
        HS_SET(&r, d, LOOP, loop, retained_in, RETAINED_IN, AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, 0);
        HS_SET(&r, d, LOOP, loop, retained_out, RETAINED_OUT, AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, 0);
        HS_SET(&r, d, LOOP, loop, working_in, WORKING_IN, AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, 0);
        HS_SET(&r, d, LOOP, loop, working_out, WORKING_OUT, AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, 0);
    } else if (what == AZ_DECK_PLAY || what == AZ_DECK_STOP) {
        if (!r.epoch) return;
        r.image.transport.state = what == AZ_DECK_PLAY ? AZ_PLAYBACK_PLAYING : AZ_PLAYBACK_PAUSED;
        HS_SET(&r, d, TRANSPORT, transport, state, STATE, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    }
    if (r.topics) hs_commit(d, &r);
}
/* Deterministic native-like actions, independent of call_tick(). The scripted
 * sequence intentionally publishes EXIT and RELOOP before the same UI drain. */
static void hs_loop_fixture(unsigned d)
{
    hs_deck_changed(d, AZ_DECK_LOADED);
    struct hs_record r = hs_next(d);
    r.image.loop.retained_in.seconds = 0;
    HS_SET(&r, d, LOOP, loop, retained_in, RETAINED_IN, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    hs_commit(d, &r);
    r = hs_next(d);
    r.image.loop.retained_out.seconds = 2;
    r.image.loop.working_in = r.image.loop.retained_in;
    r.image.loop.working_out = r.image.loop.retained_out;
    r.image.loop.engaged = 1;
    HS_SET(&r, d, LOOP, loop, retained_out, RETAINED_OUT, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    HS_SET(&r, d, LOOP, loop, working_in, WORKING_IN, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    HS_SET(&r, d, LOOP, loop, working_out, WORKING_OUT, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    HS_SET(&r, d, LOOP, loop, engaged, ENGAGED, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    hs_commit(d, &r);
    for (int engaged = 0; engaged <= 1; ++engaged) {
        r = hs_next(d);
        r.image.loop.engaged = engaged;
        HS_SET(&r, d, LOOP, loop, engaged, ENGAGED, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
        HS_SET(&r, d, LOOP, loop, working_in, WORKING_IN, engaged ? AZ_AVAIL_READY : AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, engaged);
        HS_SET(&r, d, LOOP, loop, working_out, WORKING_OUT, engaged ? AZ_AVAIL_READY : AZ_AVAIL_EMPTY, AZ_STATUS_LOAD_SCOPE, engaged);
        hs_commit(d, &r);
    }
    hs_drain();
}
static void hs_phrase_fixture(unsigned d, uint32_t kind, uint32_t discontinuity)
{
    struct hs_record r = hs_next(d);
    if (!r.epoch || (!discontinuity && r.active.kind == kind)) return;
    r.pair = (az_status_phrase_transition_v1){ sizeof r.pair, AZ_DATA_VERSION, discontinuity, 0, r.active, r.active };
    r.pair.to.kind = kind;
    r.pair.to.phrase = kind == AZ_PHRASE_ITEM ? ht_phrases[0] : (az_phrase){ 0 };
    if (r.active.kind != kind) {
        r.active = r.pair.to;
        HS_SET(&r, d, PHRASE_TRANSITION, phrase_playback, active, ACTIVE, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    } else {
        memset(&r.pair, 0, sizeof r.pair);
    }
    if (discontinuity) {
        r.occurrence = (az_status_occurrence_v1){ .size = sizeof r.occurrence, .version = AZ_DATA_VERSION, .kind = discontinuity, .ordinal = r.sequence + 1 };
        r.topics |= AZ_STATUS_TOPIC(AZ_STATUS_POSITION) | AZ_STATUS_TOPIC(AZ_STATUS_TRANSPORT);
    }
    hs_commit(d, &r);
}
static void hs_scenario(void)
{
    if (!hs_count()) return;
    hs_drain();
    for (unsigned d = 0; d < 4; ++d) {
        hs_loop_fixture(d);
        hs_motion(d, 0);
        hs_drain();
        hs_motion(d, 12000);
        hs_drain();
        advance(NS);
        hs_phrase_fixture(d, AZ_PHRASE_ITEM, AZ_STATUS_TRAVERSAL);
        hs_phrase_fixture(d, AZ_PHRASE_END, AZ_STATUS_JUMP);
        hs_drain();
        hs_deck_changed(d, AZ_DECK_EJECTED);
        hs_drain();
    }
}
#undef HS_SET
#undef HS_TOPICS
