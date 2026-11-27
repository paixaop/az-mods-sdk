/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdarg.h>
#ifdef AZ_ANALYSIS_CLIENT_LOCAL_SOURCE
#include <example.c>
#else
#include "../../examples/analysis-client/example.c"
#endif
static az_deck_status_fn observed;
static unsigned requests, copies, cancels, closes, terminal_logs;
static int next_status = AZ_BUSY, next_release = AZ_BUSY, next_result = AZ_OK;
static unsigned status_state = AZ_ANALYSIS_COMPLETE;
static int status_result = AZ_OK;
static void log_line(const char *id, const char *fmt, ...)
{
    assert(!strcmp(id, ID) && fmt);
    terminal_logs += !strncmp(fmt, "analysis state ", 15);
}
static int request_work(const char *id, const az_analysis_request_v1 *r, uint64_t *out)
{
    assert(!strcmp(id, ID) && r->source_kind == AZ_ANALYSIS_DECK && r->deck == 2);
    assert(r->components == (AZ_DATA_PHRASES | AZ_ANALYSIS_VOCALS));
    assert(r->stage_mode == AZ_ANALYSIS_SEQUENTIAL && r->phrase_threads == 1 && r->vocal_threads == 1);
    assert(r->load_epoch == 37);
    requests++;
    *out = 9;
    return AZ_OK;
}
static int status_work(const char *id, uint64_t token, az_analysis_status_v1 *out)
{
    assert(!strcmp(id, ID) && token == 9);
    out->state = status_state;
    out->result = status_result;
    return next_status;
}
static int result_work(const char *id, uint64_t token, az_analysis_result_v1 *out,
                       az_analysis_phrase_v1 *p, uint32_t pc, az_vocal_frame_v1 *v, uint32_t vc)
{
    assert(!strcmp(id, ID) && token == 9 && pc >= 1 && vc >= 1);
    copies++;
    /* Actual core result semantics: a terminal failure can itself be AZ_BUSY. */
    if (status_state != AZ_ANALYSIS_COMPLETE || status_result != AZ_OK) return status_result;
    if (next_result != AZ_OK) return next_result;
    p[0].start_beat = 4;
    v[0].activity = 3;
    out->phrases.count = out->vocals.count = 1;
    return AZ_OK;
}
static int cancel_work(const char *id, uint64_t token)
{
    assert(!strcmp(id, ID) && token == 9);
    cancels++;
    return AZ_OK;
}
static int release_work(const char *id, uint64_t token)
{
    assert(!strcmp(id, ID) && token == 9);
    closes++;
    return next_release;
}
static int subscribe(const char *id, const az_deck_watch_v1 *watch, az_deck_status_fn fn,
                     void *user, az_deck_subscription *out)
{
    assert(!strcmp(id, ID) && watch->topics == AZ_STATUS_TOPIC(AZ_STATUS_LOAD) && !user);
    observed = fn;
    *out = 3;
    return AZ_OK;
}
static int unsubscribe(const char *id, az_deck_subscription token)
{
    assert(!strcmp(id, ID) && token == 3);
    return AZ_OK;
}
static const az_analysis_v1 provider = { .abi = AZ_ANALYSIS_ABI, .size = sizeof provider, .request = request_work, .status = status_work, .result = result_work, .cancel = cancel_work, .release = release_work };
static const az_observation_v1 observer = { .abi = AZ_OBSERVATION_ABI, .size = sizeof observer, .on_deck_status = subscribe, .deck_status_release = unsubscribe };
static const az_analysis_v1 *get_analysis(uint32_t abi) { return abi == AZ_ANALYSIS_ABI ? &provider : NULL; }
static const az_observation_v1 *get_observer(uint32_t abi) { return abi == AZ_OBSERVATION_ABI ? &observer : NULL; }
static void terminal_without_result(const az_mod_v1 *client, const az_deck_status_event_v1 *event,
                                    unsigned state, int result)
{
    unsigned before_copies = copies, before_logs = terminal_logs;
    status_state = state;
    status_result = result;
    next_status = AZ_OK;
    next_release = AZ_BUSY;
    observed(event, NULL);
    assert(request_id);
    client->tick();
    assert(copies == before_copies && terminal_logs == before_logs + 1);
    client->tick();
    assert(request_id); /* real release contention remains retryable */
    next_release = AZ_OK;
    client->tick();
    assert(!request_id);
    client->tick();
    assert(terminal_logs == before_logs + 1 && copies == before_copies);
}
int main(void)
{
    const az_mod_v1 *client = az_mod_v1_entry();
    az_core_v1 host = { .abi = AZ_MOD_ABI, .size = sizeof host, .log = log_line, .analysis_service = get_analysis, .observation_service = get_observer };
    assert(client->init(NULL) < 0);
    host.abi--;
    assert(client->init(&host) < 0);
    host.abi++;
    assert(client->init(&host) == AZ_OK && observed);
    az_status_load_v1 load = { .header = { .size = sizeof load, .version = AZ_DATA_VERSION, .valid = UINT64_C(1) << AZ_STATUS_LOAD_STATE }, .state = AZ_STATUS_LOAD_LOADED, .state_info = { .scope = AZ_STATUS_LOAD_SCOPE, .scope_id = 37, .info = { .availability = AZ_AVAIL_READY, .provenance = AZ_PROV_NATIVE } } };
    az_deck_status_view_v1 view = { .size = sizeof view, .version = AZ_DATA_VERSION, .load = &load };
    az_deck_status_event_v1 event = { .size = sizeof event, .version = AZ_DATA_VERSION, .deck = 2, .load_epoch = 37, .view = &view };
    observed(NULL, NULL);
    event.version++;
    observed(&event, NULL);
    assert(!requests);
    event.version--;
    load.header.valid = 0;
    observed(&event, NULL);
    assert(!requests);
    load.header.valid = UINT64_C(1) << AZ_STATUS_LOAD_STATE;
    load.state_info.scope_id = 36;
    observed(&event, NULL);
    assert(!requests);
    load.state_info.scope_id = 37;
    /* A failed source-not-ready request is terminal even when its code is BUSY. */
    terminal_without_result(client, &event, AZ_ANALYSIS_FAILED, AZ_BUSY);
    terminal_without_result(client, &event, AZ_ANALYSIS_FAILED, AZ_FAILED);
    terminal_without_result(client, &event, AZ_ANALYSIS_FAILED, AZ_STALE);
    terminal_without_result(client, &event, AZ_ANALYSIS_CANCELLED, AZ_STALE);
    terminal_without_result(client, &event, AZ_ANALYSIS_COMPLETE, AZ_NOT_FOUND);
    unsigned before_requests = requests, before_copies = copies, before_logs = terminal_logs;
    status_state = AZ_ANALYSIS_SAVED;
    status_result = AZ_PENDING;
    next_status = AZ_BUSY;
    observed(&event, NULL);
    observed(&event, NULL);
    assert(requests == before_requests + 1);
    client->tick();
    assert(copies == before_copies);
    next_status = AZ_OK;
    client->tick();
    assert(copies == before_copies);
    status_state = AZ_ANALYSIS_COMPLETE;
    status_result = AZ_OK;
    next_result = AZ_BUSY;
    client->tick();
    client->tick();
    assert(copies == before_copies + 2 && terminal_logs == before_logs + 1 && request_id);
    next_result = AZ_OK;
    client->tick();
    assert(copies == before_copies + 3);
    assert(phrases[0].start_beat == 4 && vocals[0].activity == 3);
    next_release = AZ_BUSY;
    client->tick();
    assert(request_id);
    next_release = AZ_OK;
    client->tick();
    assert(!request_id && terminal_logs == before_logs + 1);
    observed(&event, NULL);
    next_status = AZ_STALE;
    before_copies = copies;
    client->tick();
    client->tick();
    assert(!request_id && copies == before_copies);
    observed(&event, NULL);
    next_status = AZ_OK;
    next_result = AZ_BUSY;
    client->tick();
    unsigned before_closes = closes;
    client->stop();
    assert(cancels == 1 && closes == before_closes + 1);
    assert(!request_id && !subscription);
    assert(client->init(&host) == AZ_OK);
    terminal_without_result(client, &event, AZ_ANALYSIS_CANCELLED, AZ_STALE);
    client->stop();
    return 0;
}
