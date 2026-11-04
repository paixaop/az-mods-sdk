/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>
#include <float.h>
static void timing_precision(void)
{
    az_beat beats[] = { { 0, 120, 1, 0 }, { .5, 120, 2, 0 }, { 1.5, 60, 3, 0 } };
    az_track_snapshot s = { .size = sizeof s, .version = AZ_DATA_VERSION, .beat_count = 3, .beats = beats };
    s.grid_info.availability = AZ_AVAIL_READY;
    double out = 123;
    double source = 81673.0 / 44100;
    beats[2].time_s = source;
    assert(ht_beat_to_time(&s, 1.25, &out) == AZ_OK);
    double back = 0;
    assert(ht_time_to_beat(&s, out, &back) == AZ_OK && fabs(back - 1.25) < 1e-15);
    assert(ht_beat_to_time(&s, 2, &out) == AZ_OK && compare_timestamps(out, source, 1, 1000, 1) == AZ_TIME_EQUAL);
    const double invalid[] = { NAN, INFINITY, -INFINITY };
    for (unsigned i = 0; i < 3; i++)
        for (unsigned j = 0; j < 3; j++) {
            beats[0].time_s = 0;
            beats[1].time_s = .5;
            beats[2].time_s = 1.5;
            beats[i].time_s = invalid[j];
            out = 123;
            assert(ht_beat_to_time(&s, i == 2 ? 2 : i + .25, &out) == AZ_FAILED && out == 123);
        }
    beats[0].time_s = 0;
    beats[1].time_s = .5;
    beats[2].time_s = INFINITY;
    out = 123;
    assert(ht_time_to_beat(&s, .25, &out) == AZ_FAILED && out == 123);
    beats[2].time_s = .5;
    assert(ht_beat_to_time(&s, 1.5, &out) == AZ_FAILED && out == 123);
    beats[2].time_s = .25;
    assert(ht_beat_to_time(&s, 1.5, &out) == AZ_FAILED && out == 123);
    beats[2].time_s = 1.5;
    assert(ht_time_to_beat(&s, 1.5005, &out) == AZ_OK && out == 2);
    assert(ht_time_to_beat(&s, 1.5015, &out) == AZ_NOT_FOUND);
    az_time_interval duration = { .size = sizeof duration };
    assert(ht_interval(&s, 1., .9995, 1., &duration) == AZ_OK);
    assert(compare_timestamps(duration.source_s, 0., 1, 1000, 1) == AZ_TIME_EQUAL);
    assert(ht_interval(&s, 1., .9985, 1., &duration) == AZ_INVALID);
    assert(ht_interval(NULL, 0, DBL_MIN, DBL_MAX, &duration) == AZ_OK);
    assert(compare_timestamps(duration.source_s, 0., 1, 1000, 1) == AZ_TIME_EQUAL && (duration.valid & AZ_INTERVAL_PLAYBACK));
    assert(compare_timestamps(duration.playback_s, 0., 1, 1000, 1) == AZ_TIME_EQUAL && duration.playback_rate == DBL_MAX);
    assert(ht_interval(NULL, 0, 0, DBL_MAX, &duration) == AZ_OK);
    assert((duration.valid & AZ_INTERVAL_PLAYBACK) && compare_timestamps(duration.playback_s, 0., 1, 1000, 1) == AZ_TIME_EQUAL);
}
static void invalid_envelopes(const az_library_v1 *library, const az_track_ref *ref)
{
    for (unsigned field = 0; field < 3; field++) {
        az_track_ref invalid = *ref;
        if (field == 0) invalid.source_id = 0;
        if (field == 1) invalid.source_generation = 0;
        if (field == 2) invalid.library_id = 0;
        uint64_t ticket = UINT64_MAX;
        az_wave_query query = { sizeof query, 1, AZ_WAVE_PWV3_MONO, 0, 1, 0 };
        assert(library->track_request("fixture", &invalid, AZ_DATA_METADATA, &ticket) == AZ_INVALID && ticket == UINT64_MAX);
        assert(library->waveform_request("fixture", &invalid, &query, &ticket) == AZ_INVALID && ticket == UINT64_MAX);
    }
    az_command_request valid = { .size = sizeof valid, .version = 1, .operation = AZ_CMD_LOOP_EXIT, .request_id = 99, .expected_load_generation = 1, .deadline_ns = 1 };
    for (unsigned field = 0; field < 17; field++) {
        az_command_request invalid = valid;
        switch (field) {
        case 0: invalid.size = sizeof invalid - 1; break;
        case 1: invalid.version++; break;
        case 2: invalid.deck = 4; break;
        case 3: invalid.operation = 0; break;
        case 4: invalid.operation = AZ_CMD_CUE_COLOR + 1; break;
        case 5: invalid.request_id = 0; break;
        case 6: invalid.expected_load_generation = 0; break;
        case 7: invalid.deadline_ns = 0; break;
        case 8: invalid.quantization = AZ_QUANTIZE_ON + 1; break;
        case 9: invalid.flags = AZ_COMMAND_NATIVE_CUE_IDS << 1; break;
        case 10: invalid.reserved = 1; break;
        case 11: invalid.classification = AZ_CUE_MEMORY + 1; break;
        case 12: invalid.rgb = UINT32_C(0x1000000); break;
        case 13: invalid.position_s = NAN; break;
        case 14: invalid.end_s = INFINITY; break;
        case 15: invalid.beats = -INFINITY; break;
        case 16: memset(invalid.comment, 'x', sizeof invalid.comment); break;
        }
        az_command_result result;
        memset(&result, 0x5a, sizeof result);
        result.size = sizeof result;
        az_command_result before = result;
        assert(library->command_submit("fixture", &invalid, &result) == AZ_INVALID);
        assert(!memcmp(&result, &before, sizeof result));
    }
    az_command_result result = { .size = sizeof result };
    assert(library->command_submit("fixture", &valid, &result) == AZ_UNSUPPORTED);
    assert(result.request_id == valid.request_id && result.outcome == AZ_COMMAND_REJECTED);
    valid.flags = AZ_COMMAND_NATIVE_LOAD;
    assert(library->command_submit("fixture", &valid, &result) == AZ_UNSUPPORTED);
    valid.operation = AZ_CMD_RELOOP;
    assert(library->command_submit("fixture", &valid, &result) == AZ_UNSUPPORTED);
    assert(result.request_id == valid.request_id && result.outcome == AZ_COMMAND_REJECTED);
    valid.expected_revision = 1;
    assert(library->command_submit("fixture", &valid, &result) == AZ_INVALID);
    valid.expected_revision = 0;
    valid.flags |= AZ_COMMAND_PERSIST;
    assert(library->command_submit("fixture", &valid, &result) == AZ_INVALID);
    valid.flags = 0;
    az_command_result before = result;
    assert(AZ_LIB_HAS(library, command_release));
    assert(library->command_poll("fixture", valid.request_id, &result) == AZ_STALE);
    assert(library->command_cancel("fixture", valid.request_id, &result) == AZ_STALE);
    assert(library->command_release("fixture", valid.request_id) == AZ_STALE);
    assert(!memcmp(&result, &before, sizeof result));
    assert(library->command_poll("fixture", 0, &result) == AZ_INVALID);
    assert(library->command_cancel("fixture", 0, &result) == AZ_INVALID);
    assert(library->command_release("fixture", 0) == AZ_INVALID);
}
static void native_command_envelopes(const az_library_v1 *library)
{
    /* Valid requests remain unsupported by this synthetic provider. They must
     * not be rejected as malformed by an obsolete EXIT-only envelope. */
    for (unsigned deck = 0; deck < 4; ++deck) {
        for (unsigned operation = AZ_CMD_LOOP_IN; operation <= AZ_CMD_CUE_COLOR; ++operation) {
            az_command_request request = {
                .size = sizeof request,
                .version = AZ_DATA_VERSION,
                .operation = operation,
                .deck = deck,
                .request_id = 100 + operation,
                .expected_load_generation = 1,
                .deadline_ns = 1,
                .flags = AZ_COMMAND_NATIVE_LOAD,
            };
            const int selected = operation == AZ_CMD_SAVED_RECALL || operation == AZ_CMD_SAVED_DELETE ||
                                 operation == AZ_CMD_SAVED_AUTO || operation == AZ_CMD_MEMORY_RECALL ||
                                 operation == AZ_CMD_MEMORY_DELETE || operation == AZ_CMD_CUE_COMMENT || operation == AZ_CMD_CUE_COLOR;
            const int persistent = operation == AZ_CMD_SAVED_SAVE || operation == AZ_CMD_SAVED_DELETE ||
                                   operation == AZ_CMD_SAVED_AUTO || operation == AZ_CMD_HOT_SET || operation == AZ_CMD_HOT_CLEAR ||
                                   operation == AZ_CMD_MEMORY_SET || operation == AZ_CMD_MEMORY_DELETE ||
                                   operation == AZ_CMD_CUE_COMMENT || operation == AZ_CMD_CUE_COLOR;
            if (operation > AZ_CMD_LOOP_ADJUST_OUT) request.classification = AZ_CUE_MEMORY;
            if (operation == AZ_CMD_MAIN_SET || operation == AZ_CMD_MAIN_RECALL) request.classification = AZ_CUE_MAIN;
            if (operation == AZ_CMD_HOT_TRIGGER || operation == AZ_CMD_HOT_SET || operation == AZ_CMD_HOT_CLEAR) {
                request.classification = AZ_CUE_HOT;
                request.entry_id = 1;
            }
            if (selected) {
                request.flags |= AZ_COMMAND_NATIVE_CUE_IDS;
                request.expected_revision = 1;
                request.entry_id = 1;
            }
            if (persistent) request.flags |= AZ_COMMAND_PERSIST;
            if (operation == AZ_CMD_LOOP_LENGTH) request.beats = 4;
            if (operation == AZ_CMD_LOOP_MOVE || operation == AZ_CMD_LOOP_ADJUST_IN) request.position_s = .1;
            if (operation == AZ_CMD_LOOP_ADJUST_OUT) request.end_s = .1;
            if (operation == AZ_CMD_SAVED_AUTO) request.position_s = 1;
            if (operation == AZ_CMD_CUE_COMMENT) strcpy(request.comment, "SDK regression");
            if (operation == AZ_CMD_HOT_SET || operation == AZ_CMD_CUE_COLOR) request.rgb = 0x123456;
            az_command_result result = { .size = sizeof result };
            assert(library->command_submit("fixture", &request, &result) == AZ_UNSUPPORTED);
            assert(result.request_id == request.request_id && result.outcome == AZ_COMMAND_REJECTED && result.reason == AZ_UNSUPPORTED);
        }
    }
    az_command_request invalid = {
        .size = sizeof invalid,
        .version = AZ_DATA_VERSION,
        .operation = AZ_CMD_SAVED_DELETE,
        .request_id = 1,
        .expected_load_generation = 1,
        .deadline_ns = 1,
        .expected_revision = 1,
        .flags = AZ_COMMAND_NATIVE_CUE_IDS | AZ_COMMAND_PERSIST,
        .classification = AZ_CUE_MEMORY,
        .entry_id = 1,
    };
    az_command_result result = { .size = sizeof result }, before = result;
    assert(library->command_submit("fixture", &invalid, &result) == AZ_INVALID);
    invalid.flags |= AZ_COMMAND_NATIVE_LOAD;
    invalid.expected_revision = 0;
    assert(library->command_submit("fixture", &invalid, &result) == AZ_INVALID);
    invalid.expected_revision = 1;
    invalid.flags = AZ_COMMAND_NATIVE_LOAD | AZ_COMMAND_NATIVE_CUE_IDS;
    invalid.operation = AZ_CMD_LOOP_EXIT;
    assert(library->command_submit("fixture", &invalid, &result) == AZ_INVALID);
    assert(!memcmp(&result, &before, sizeof result));
}
static void short_records(const az_library_v1 *library)
{
    uint32_t *tiny = malloc(sizeof *tiny);
    assert(tiny);
    *tiny = sizeof *tiny;
    az_track_ref safe_ref = { sizeof safe_ref, 1, 1, 1, 1, 1, 0 };
    az_wave_query safe_query = { sizeof safe_query, 1, AZ_WAVE_PWV3_MONO, 0, 1, 0 };
    uint64_t safe_ticket = UINT64_MAX;
    az_command_request safe_request = { .size = sizeof safe_request, .version = 1, .operation = AZ_CMD_LOOP_EXIT, .request_id = 1, .expected_load_generation = 1, .deadline_ns = 1 };
    az_command_result safe_result = { .size = sizeof safe_result };
    assert(library->data_capabilities((az_data_capabilities *)tiny) == AZ_INVALID);
    assert(*tiny == sizeof *tiny);
    *tiny = sizeof *tiny;
    assert(library->track_resolve("fixture", 0, 1, (az_track_ref *)tiny) == AZ_INVALID);
    assert(*tiny == sizeof *tiny);
    *tiny = sizeof *tiny;
    assert(library->track_request("fixture", (const az_track_ref *)tiny, 1, &safe_ticket) == AZ_INVALID);
    assert(library->waveform_request("fixture", (const az_track_ref *)tiny, &safe_query, &safe_ticket) == AZ_INVALID);
    assert(library->waveform_request("fixture", &safe_ref, (const az_wave_query *)tiny, &safe_ticket) == AZ_INVALID);
    assert(library->deck_snapshot(0, (az_deck_snapshot *)tiny) == AZ_INVALID);
    assert(*tiny == sizeof *tiny);
    *tiny = sizeof *tiny;
    assert(library->data_event("fixture", 0, (az_data_change *)tiny) == AZ_INVALID);
    assert(*tiny == sizeof *tiny);
    *tiny = sizeof *tiny;
    assert(library->time_interval(NULL, 0, 1, 1, (az_time_interval *)tiny) == AZ_INVALID);
    assert(*tiny == sizeof *tiny);
    *tiny = sizeof *tiny;
    assert(library->command_submit("fixture", (const az_command_request *)tiny, &safe_result) == AZ_INVALID);
    assert(library->command_submit("fixture", &safe_request, (az_command_result *)tiny) == AZ_INVALID);
    assert(library->command_poll("fixture", 1, (az_command_result *)tiny) == AZ_INVALID);
    assert(library->command_cancel("fixture", 1, (az_command_result *)tiny) == AZ_INVALID);
    assert(*tiny == sizeof *tiny);
    *tiny = sizeof *tiny;
    assert(*tiny == sizeof *tiny && safe_ticket == UINT64_MAX);
    free(tiny);
    size_t earlier_size = offsetof(az_deck_snapshot, loop_unit_generation);
    void *earlier = malloc(earlier_size);
    assert(earlier);
    uint32_t declared = (uint32_t)earlier_size;
    memcpy(earlier, &declared, sizeof declared);
    assert(library->deck_snapshot(0, earlier) == AZ_OK);
    memcpy(&declared, earlier, sizeof declared);
    assert(declared == earlier_size);
    free(earlier);
}
static int callback_count;
static uint64_t callback_ticket;
static void got_data(const az_data_change *event, void *user)
{
    assert(user == &callback_count && ctx == CTX_UI);
    assert(event->ticket == callback_ticket && event->kind == AZ_CHANGE_TRACK_DATA);
    assert(!pthread_mutex_trylock(&ht_mu));
    pthread_mutex_unlock(&ht_mu);
    if (event->status == AZ_OK) {
        if (event->fields == AZ_DATA_WAVEFORM) {
            const az_wave_snapshot *snapshot;
            assert(HT_LIBRARY.waveform_acquire("fixture", event->ticket, &snapshot) == AZ_OK && snapshot);
        } else {
            const az_track_snapshot *snapshot;
            assert(HT_LIBRARY.track_acquire("fixture", event->ticket, &snapshot) == AZ_OK && snapshot);
        }
    } else assert(event->status == AZ_CANCELLED);
    ++callback_count;
}
static void got_command(const az_command_result *result, void *user)
{
    (void)result;
    (void)user;
    assert(!"unsupported synthetic commands must not produce completion callbacks");
}
static void callbacks(const az_library_v1 *library)
{
    az_library_v1 old = *library;
    const az_library_v1 *old_prefix = &old;
    old.size = offsetof(az_library_v1, on_data_event);
    assert(!AZ_LIB_HAS(old_prefix, on_data_event) && !AZ_LIB_HAS(old_prefix, on_command_complete));
    assert(AZ_LIB_HAS(library, on_data_event) && AZ_LIB_HAS(library, on_command_complete));
    ctx = CTX_NONE;
    assert(library->on_data_event("fixture", got_data, NULL) == AZ_INVALID);
    ctx = CTX_INIT;
    assert(library->on_data_event("fixture", NULL, NULL) == AZ_INVALID);
    assert(library->on_data_event("fixture", got_data, &callback_count) == AZ_OK);
    assert(library->on_data_event("fixture", got_data, NULL) == AZ_LIMIT);
    assert(library->on_command_complete("fixture", got_command, NULL) == AZ_OK);
    assert(library->on_command_complete("fixture", got_command, NULL) == AZ_LIMIT);
    static const az_mod_v1 callback_only_mod = { .tick = NULL };
    mod = &callback_only_mod;
    ctx = CTX_NONE;
    az_track_ref r = { sizeof r, 1, 1, 1, 1, 321, 0 };
    assert(library->track_request("fixture", &r, AZ_DATA_METADATA, &callback_ticket) == AZ_OK);
    assert(!callback_count); /* Never invoked inline on the request's thread. */
    call_tick();
    assert(callback_count == 1);
    call_tick();
    assert(callback_count == 1);
    az_data_change polled = { .size = sizeof polled };
    assert(library->data_event("fixture", ht_after - 1, &polled) == AZ_OK && polled.ticket == callback_ticket);
    assert(library->track_release("fixture", callback_ticket) == AZ_OK);
    az_wave_query q = { sizeof q, 1, AZ_WAVE_PWV3_MONO, 0, 1, 0 };
    assert(library->waveform_request("fixture", &r, &q, &callback_ticket) == AZ_OK);
    call_tick();
    assert(callback_count == 2);
    assert(library->track_cancel("fixture", callback_ticket) == AZ_OK);
    call_tick();
    assert(callback_count == 3);
    assert(library->track_cancel("fixture", callback_ticket) == AZ_OK);
    call_tick();
    assert(callback_count == 3);
    assert(library->track_release("fixture", callback_ticket) == AZ_OK);
}
static void never_complete(const az_command_completion *event, void *user)
{
    (void)event;
    (void)user;
    assert(!"refused submission has no callback ownership");
}
static unsigned initiation_calls;
static struct {
    uint64_t id;
    int status, owned;
} handoff[2];
static void initiate(void *user)
{
    unsigned at = *(unsigned *)user;
    ++initiation_calls;
    az_command_request request = { .size = sizeof request, .version = AZ_DATA_VERSION, .operation = AZ_CMD_LOOP_EXIT, .request_id = 100 + at, .expected_load_generation = 1, .deadline_ns = 1 };
    az_command_result result = { .size = sizeof result };
    handoff[at].status = HT_LIBRARY.command_submit_async("fixture", &request, never_complete, &handoff[at], &result);
    if (handoff[at].status == AZ_OK) {
        handoff[at].id = request.request_id;
        handoff[at].owned = 1;
    } else assert(handoff[at].status == AZ_UNSUPPORTED && result.request_id == request.request_id && result.outcome == AZ_COMMAND_REJECTED);
}
static void noop(void *user) { (void)user; }
static void asynchronous_boundary(void)
{
    Dl_info info;
    assert(dladdr((void *)never_complete, &info));
    mod_base = info.dli_fbase;
    az_command_request request = { .size = sizeof request, .version = AZ_DATA_VERSION, .operation = AZ_CMD_LOOP_EXIT, .request_id = 99, .expected_load_generation = 1, .deadline_ns = 1 };
    az_command_result result = { .size = sizeof result }, before = result;
    ctx = CTX_NONE;
    assert(HT_LIBRARY.command_submit_async("fixture", &request, never_complete, NULL, &result) == AZ_INVALID);
    assert(!memcmp(&before, &result, sizeof result));
    ctx = CTX_INIT;
    assert(HT_LIBRARY.command_submit_async("fixture", &request, NULL, NULL, &result) == AZ_INVALID);
    assert(!memcmp(&before, &result, sizeof result));
    assert(HT_LIBRARY.command_submit_async("fixture", &request, never_complete, NULL, &result) == AZ_UNSUPPORTED);
    assert(result.request_id == 99 && result.outcome == AZ_COMMAND_REJECTED);
    assert(HT_LIBRARY.command_acknowledge("fixture", 99) == AZ_STALE);
    assert(HT_LIBRARY.command_acknowledge("fixture", 0) == AZ_INVALID);
    /* Exercise the actual bounded post_ui queue. A dropped pre-admission job
     * cannot establish ownership or require a command callback. */
    ctx = CTX_NONE;
    for (unsigned i = 0; i < POSTQ; ++i) s_post_ui("fixture", noop, NULL);
    static unsigned first = 0, second = 1;
    s_post_ui("fixture", initiate, &first);
    assert(post_tail - post_head == POSTQ && !handoff[0].owned && !handoff[0].id);
    drain_post();
    assert(!initiation_calls);
    s_post_ui("fixture", initiate, &second);
    drain_post();
    assert(initiation_calls == 1 && !handoff[1].owned && !handoff[1].id && handoff[1].status == AZ_UNSUPPORTED);
}
int main(void)
{
    timing_precision();
    mod_id = "fixture";
    thread_lifetime = lifetime;
    const az_core_v1 *core_prefix = &CORE;
    assert(AZ_HAS(core_prefix, library_service));
    const az_library_v1 *library = CORE.library_service(AZ_LIBRARY_ABI);
    assert(library && library == CORE.library_service(AZ_LIBRARY_ABI));
    short_records(library);
    assert(!CORE.library_service(0) && !CORE.library_service(UINT32_MAX));
    assert(AZ_LIB_HAS(library, waveform_acquire));
    uint32_t *prefix = malloc(sizeof *prefix);
    assert(prefix);
    *prefix = sizeof *prefix;
    double unchanged = 123;
    az_time_interval checked = { .size = sizeof checked };
    assert(library->time_to_beat((const az_track_snapshot *)prefix, 0, &unchanged) == AZ_INVALID && unchanged == 123);
    assert(library->beat_to_time((const az_track_snapshot *)prefix, 0, &unchanged) == AZ_INVALID && unchanged == 123);
    assert(library->time_interval((const az_track_snapshot *)prefix, 0, 1, 1, &checked) == AZ_INVALID);
    free(prefix);
    az_track_snapshot wrong_version = { .size = sizeof wrong_version, .version = AZ_DATA_VERSION + 1 };
    assert(library->time_to_beat(&wrong_version, 0, &unchanged) == AZ_INVALID);
    assert(library->beat_to_time(&wrong_version, 0, &unchanged) == AZ_INVALID);
    assert(library->time_interval(&wrong_version, 0, 1, 1, &checked) == AZ_INVALID);
    assert(library->track_request == CORE.track_request && library->track_release == CORE.track_release);
    az_library_v1 older_library = *library;
    const az_library_v1 *library_prefix = &older_library;
    older_library.size = offsetof(az_library_v1, waveform_request);
    assert(AZ_LIB_HAS(library_prefix, track_request) && !AZ_LIB_HAS(library_prefix, waveform_request));
    older_library.abi++;
    assert(!AZ_LIB_HAS(library_prefix, track_request));
    az_core_v1 older_core = CORE;
    const az_core_v1 *older_prefix = &older_core;
    older_core.size = offsetof(az_core_v1, library_service);
    assert(!AZ_HAS(older_prefix, library_service));
    az_data_capabilities caps = { .size = sizeof caps };
    assert(ht_caps(&caps) == AZ_OK && caps.stored_components == 31 && caps.commands == 0);
    az_deck_snapshot raw = { .size = sizeof raw };
    assert(library->deck_snapshot(0, &raw) == AZ_OK);
    assert(raw.valid == AZ_LIVE_LOADED);
    assert(raw.quantize == -1 && raw.play_mode_is_2 == -1 && raw.play_pause == -1 && raw.play_aux == -1 && raw.key_lock == -1);
    for (int deck = 0; deck < 4; ++deck) {
        assert(library->deck_snapshot(deck, &raw) == AZ_OK);
        assert(!(raw.valid & (AZ_LIVE_MASTER_TEMPO | AZ_LIVE_SYNC)) && raw.master_tempo == -1 && raw.sync == -1 && raw.sync_raw_mode == -1);
    }
    assert(!raw.live_unit_generation && !raw.play_revision && !raw.quantize_revision && !raw.key_lock_revision);
    struct {
        az_data_capabilities value;
        uint64_t guard;
    } big = { .value = { .size = sizeof big }, .guard = UINT64_C(0x1234567890abcdef) };
    assert(ht_caps(&big.value) == AZ_OK && big.guard == UINT64_C(0x1234567890abcdef));
    caps.size = 4;
    assert(ht_caps(&caps) == AZ_INVALID);
    az_track_ref ref = { .size = sizeof ref };
    assert(ht_resolve("fixture", 0, 42, &ref) == AZ_OK);
    invalid_envelopes(library, &ref);
    native_command_envelopes(library);
    ref.size += 32; /* Newer input prefixes must not overstate the immutable stored record. */
    uint64_t tickets[4];
    for (int i = 0; i < 4; i++) assert(ht_request("fixture", &ref, 31, &tickets[i]) == AZ_OK);
    uint64_t extra;
    assert(ht_request("fixture", &ref, 31, &extra) == AZ_LIMIT);
    az_wave_query query = { sizeof query, 1, AZ_WAVE_PWV3_MONO, 150, 200, 0 };
    assert(library->waveform_request("fixture", &ref, &query, &extra) == AZ_LIMIT);
    const az_track_snapshot *snapshot;
    assert(ht_acquire("fixture", tickets[0], &snapshot) == AZ_OK && snapshot->cue_count == 2);
    assert(snapshot->track.size == sizeof snapshot->track);
    assert(compare_timestamps(snapshot->cues[0].start_s, 0., 1, 1000, 1) == AZ_TIME_EQUAL && snapshot->cues[1].type == AZ_CUE_LOOP);
    double beat, time_s;
    assert(ht_time_to_beat(snapshot, 1, &beat) == AZ_OK && beat == 1.5);
    assert(ht_beat_to_time(snapshot, beat, &time_s) == AZ_OK && compare_timestamps(time_s, 1., 1, 1000, 1) == AZ_TIME_EQUAL);
    az_time_interval duration = { .size = sizeof duration };
    assert(ht_interval(snapshot, .25, 1, 2, &duration) == AZ_OK && duration.beats == 1 && compare_timestamps(duration.playback_s, .375, 1, 1000, 1) == AZ_TIME_EQUAL);
    assert(!(duration.valid & AZ_INTERVAL_BARS));
    assert(ht_cancel("fixture", tickets[0]) == AZ_OK);
    assert(ht_acquire("fixture", tickets[0], &snapshot) == AZ_CANCELLED && !snapshot);
    for (int i = 0; i < 4; i++) assert(ht_release("fixture", tickets[i]) == AZ_OK);
    assert(ht_acquire("fixture", tickets[0], &snapshot) == AZ_STALE);
    assert(library->waveform_request("fixture", &ref, &query, &extra) == AZ_OK);
    const az_wave_snapshot *wave;
    assert(library->waveform_acquire("fixture", extra, &wave) == AZ_OK);
    assert(wave->track.size == sizeof wave->track);
    assert(wave->first_column == 150 && wave->column_count == 150 && wave->total_columns == 300);
    assert(wave->start_s == 1 && wave->seconds_per_column == 1.0 / 150.0 && wave->columns[0] == 150);
    assert(library->track_acquire("fixture", extra, &snapshot) == AZ_INVALID);
    assert(library->track_cancel("fixture", extra) == AZ_OK);
    assert(library->waveform_acquire("fixture", extra, &wave) == AZ_CANCELLED);
    assert(library->track_release("fixture", extra) == AZ_OK);
    for (uint32_t encoding = AZ_WAVE_PWV3_MONO; encoding <= AZ_WAVE_PWV6_PREVIEW; encoding++) {
        query.encoding = encoding;
        uint32_t stride = encoding == AZ_WAVE_PWV4_PREVIEW ? 6 : encoding == AZ_WAVE_PWV6_PREVIEW ? 3
                                                                                                  : encoding;
        uint32_t total = encoding >= AZ_WAVE_PWV4_PREVIEW ? 1200 : 300;
        assert(library->waveform_request("fixture", &ref, &query, &extra) == AZ_OK);
        assert(library->waveform_acquire("fixture", extra, &wave) == AZ_OK);
        assert(wave->encoding == encoding && wave->bytes_per_column == stride);
        assert(wave->total_columns == total && wave->column_count == (total == 300 ? 150u : 200u));
        assert(wave->seconds_per_column == (total == 300 ? 1.0 / 150.0 : 2.0 / 1200.0));
        assert(wave->start_s == 150 * wave->seconds_per_column);
        assert(wave->data_bytes == wave->column_count * stride && wave->columns[0] == (150 * stride) % 256);
        uint64_t overlap_ticket;
        az_wave_query overlap = query;
        overlap.first_column++;
        overlap.column_count = 2;
        assert(library->waveform_request("fixture", &ref, &overlap, &overlap_ticket) == AZ_OK);
        const az_wave_snapshot *overlap_wave;
        assert(library->waveform_acquire("fixture", overlap_ticket, &overlap_wave) == AZ_OK);
        assert(overlap_wave->data_bytes == 2 * stride && !memcmp(wave->columns + stride, overlap_wave->columns, 2 * stride));
        assert(library->track_release("fixture", overlap_ticket) == AZ_OK);
        assert(library->track_release("fixture", extra) == AZ_OK);
    }
    for (uint32_t encoding = 6; encoding <= 7; ++encoding) {
        query = (az_wave_query){ sizeof query, 1, encoding, 50, AZ_WAVE_WINDOW_MAX, 0 };
        uint32_t total = encoding == 6 ? 100u : 400u;
        assert(library->waveform_request("fixture", &ref, &query, &extra) == AZ_OK);
        assert(library->waveform_acquire("fixture", extra, &wave) == AZ_OK);
        assert(wave->encoding == encoding && wave->bytes_per_column == 1 && wave->total_columns == total);
        assert(wave->column_count == total - 50 && wave->data_bytes == total - 50 && wave->columns[0] == 50);
        assert(wave->seconds_per_column == 2.0 / total && wave->start_s == 50 * wave->seconds_per_column);
        assert(wave->info.provenance == AZ_PROV_FILE);
        assert(library->track_release("fixture", extra) == AZ_OK);
    }
    az_command_request request = { .size = sizeof request, .version = 1, .operation = AZ_CMD_LOOP_EXIT, .request_id = 99, .expected_load_generation = 1, .deadline_ns = 1 };
    az_command_result result = { .size = sizeof result };
    assert(ht_command("fixture", &request, &result) == AZ_UNSUPPORTED && result.request_id == 99 && result.outcome == AZ_COMMAND_REJECTED);
    callbacks(library);
    asynchronous_boundary();
    return 0;
}
