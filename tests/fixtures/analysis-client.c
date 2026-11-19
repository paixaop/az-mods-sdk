/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdarg.h>
#include "../../examples/analysis-client/example.c"
static az_deck_status_fn observed;
static unsigned requests, copies, cancels, closes;
static int next_status = AZ_BUSY, next_release = AZ_BUSY;
static void log_line(const char *id, const char *fmt, ...) { assert(!strcmp(id, ID) && fmt); }
static int request_work(const char *id, const az_analysis_request_v1 *r, uint64_t *out) {
    assert(!strcmp(id, ID) && r->source_kind == AZ_ANALYSIS_DECK && r->deck == 2);
    assert(r->components == (AZ_DATA_PHRASES | AZ_ANALYSIS_VOCALS));
    assert(r->stage_mode == AZ_ANALYSIS_SEQUENTIAL && r->phrase_threads == 1 && r->vocal_threads == 1);
    assert(r->load_epoch == 37); requests++; *out = 9; return AZ_OK;
}
static int status_work(const char *id, uint64_t token, az_analysis_status_v1 *out) {
    assert(!strcmp(id, ID) && token == 9);
    out->state = AZ_ANALYSIS_COMPLETE; out->result = AZ_OK; return next_status;
}
static int result_work(const char *id, uint64_t token, az_analysis_result_v1 *out,
                       az_analysis_phrase_v1 *p, uint32_t pc, az_vocal_frame_v1 *v, uint32_t vc) {
    assert(!strcmp(id, ID) && token == 9 && pc >= 1 && vc >= 1);
    p[0].start_beat = 4; v[0].activity = 3; out->phrases.count = out->vocals.count = 1;
    copies++; return AZ_OK;
}
static int cancel_work(const char *id, uint64_t token) {
    assert(!strcmp(id, ID) && token == 9); cancels++; return AZ_OK;
}
static int release_work(const char *id, uint64_t token) {
    assert(!strcmp(id, ID) && token == 9); closes++; return next_release;
}
static int subscribe(const char *id, const az_deck_watch_v1 *watch, az_deck_status_fn fn,
                      void *user, az_deck_subscription *out) {
    assert(!strcmp(id, ID) && watch->topics == AZ_STATUS_TOPIC(AZ_STATUS_LOAD) && !user);
    observed = fn; *out = 3; return AZ_OK;
}
static int unsubscribe(const char *id, az_deck_subscription token) {
    assert(!strcmp(id, ID) && token == 3); return AZ_OK;
}
static const az_analysis_v1 provider = {.abi = AZ_ANALYSIS_ABI, .size = sizeof provider,
    .request = request_work, .status = status_work, .result = result_work,
    .cancel = cancel_work, .release = release_work};
static const az_observation_v1 observer = {.abi = AZ_OBSERVATION_ABI, .size = sizeof observer,
    .on_deck_status = subscribe, .deck_status_release = unsubscribe};
static const az_analysis_v1 *get_analysis(uint32_t abi) { return abi == AZ_ANALYSIS_ABI ? &provider : NULL; }
static const az_observation_v1 *get_observer(uint32_t abi) { return abi == AZ_OBSERVATION_ABI ? &observer : NULL; }
int main(void) {
    const az_mod_v1 *client = az_mod_v1_entry();
    az_core_v1 host = {.abi = AZ_MOD_ABI, .size = sizeof host, .log = log_line,
        .analysis_service = get_analysis, .observation_service = get_observer};
    assert(client->init(NULL) < 0); host.abi--; assert(client->init(&host) < 0); host.abi++;
    assert(client->init(&host) == AZ_OK && observed);
    az_status_load_v1 load = {.header = {.size = sizeof load, .version = AZ_DATA_VERSION,
        .valid = UINT64_C(1) << AZ_STATUS_LOAD_STATE}, .state = AZ_STATUS_LOAD_LOADED,
        .state_info = {.info = {.availability = AZ_AVAIL_READY, .provenance = AZ_PROV_NATIVE}}};
    az_deck_status_view_v1 view = {.size = sizeof view, .version = AZ_DATA_VERSION, .load = &load};
    az_deck_status_event_v1 event = {.size = sizeof event, .version = AZ_DATA_VERSION,
        .deck = 2, .load_epoch = 37, .view = &view};
    observed(NULL, NULL); event.version++; observed(&event, NULL); assert(!requests); event.version--;
    load.header.valid = 0; observed(&event, NULL); assert(!requests);
    load.header.valid = UINT64_C(1) << AZ_STATUS_LOAD_STATE;
    observed(&event, NULL); observed(&event, NULL); assert(requests == 1);
    client->tick(); assert(!copies); next_status = AZ_OK; client->tick(); assert(copies == 1);
    assert(phrases[0].start_beat == 4 && vocals[0].activity == 3);
    client->tick(); assert(request_id); next_release = AZ_OK; client->tick(); assert(!request_id);
    observed(&event, NULL); client->stop(); assert(cancels == 1 && closes == 3);
    assert(!request_id && !subscription);
    return 0;
}
