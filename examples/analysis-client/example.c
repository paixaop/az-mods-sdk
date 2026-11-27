/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
#include <string.h>
#define ID "analysis-client"
static const az_core_v1 *core;
static const az_analysis_v1 *analysis;
static const az_observation_v1 *observations;
static az_deck_subscription subscription;
static uint64_t request_id;
static int releasing, copying;
static az_analysis_phrase_v1 phrases[1024];
static az_vocal_frame_v1 vocals[25840];
static int event_valid(const az_deck_status_event_v1 *event)
{
    if (!event || event->size < sizeof *event || event->version != AZ_DATA_VERSION) return 0;
    if (event->deck >= 4 || !event->load_epoch || !event->view) return 0;
    return event->view->size >= sizeof *event->view && event->view->version == AZ_DATA_VERSION;
}
static int state_current(const az_status_field_info_v1 *info, uint64_t epoch)
{
    return info->scope == AZ_STATUS_UNIT ||
           (info->scope == AZ_STATUS_LOAD_SCOPE && info->scope_id == epoch);
}
static int ready_load(const az_deck_status_event_v1 *event)
{
    if (!event_valid(event) || !event->view->load) return 0;
    const az_status_load_v1 *load = event->view->load;
    if (load->header.size < sizeof *load || load->header.version != AZ_DATA_VERSION) return 0;
    if (!(load->header.valid & (UINT64_C(1) << AZ_STATUS_LOAD_STATE))) return 0;
    if (!state_current(&load->state_info, event->load_epoch)) return 0;
    return load->state == AZ_STATUS_LOAD_LOADED &&
           load->state_info.info.availability == AZ_AVAIL_READY &&
           load->state_info.info.provenance == AZ_PROV_NATIVE;
}
static void loaded(const az_deck_status_event_v1 *event, void *unused)
{
    (void)unused;
    if (request_id || !ready_load(event)) return;
    az_analysis_request_v1 r = { .size = sizeof r, .version = AZ_ANALYSIS_REQUEST_VERSION, .source_kind = AZ_ANALYSIS_DECK, .components = AZ_DATA_PHRASES | AZ_ANALYSIS_VOCALS, .deck = event->deck, .load_epoch = event->load_epoch, .stage_mode = AZ_ANALYSIS_SEQUENTIAL, .phrase_threads = 1, .vocal_threads = 1 };
    int rc = analysis->request(ID, &r, &request_id);
    if (rc) core->log(ID, "analysis request refused: %d", rc);
}
static void read_result(void)
{
    az_analysis_result_v1 result = { .size = sizeof result };
    int rc = analysis->result(ID, request_id, &result, phrases, 1024, vocals, 25840);
    if (rc == AZ_BUSY) return;
    if (rc == AZ_OK)
        core->log(ID, "copied %u phrases and %u vocal frames at source rate %u",
                  result.phrases.count, result.vocals.count, result.source_rate);
    else core->log(ID, "analysis result read: %d", rc);
    copying = 0;
    releasing = 1;
}
static void release_result(void)
{
    int rc = analysis->release(ID, request_id);
    if (rc == AZ_BUSY) return;
    request_id = 0;
    releasing = 0;
}
static void observe_result(void)
{
    az_analysis_status_v1 status = { .size = sizeof status };
    int rc = analysis->status(ID, request_id, &status);
    if (rc == AZ_BUSY) return;
    if (rc != AZ_OK) {
        core->log(ID, "analysis status: %d", rc);
        releasing = 1;
        return;
    }
    if (status.state < AZ_ANALYSIS_COMPLETE) return;
    core->log(ID, "analysis state %u result %d: %s", status.state, status.result, status.diagnostic);
    /* Terminal AZ_BUSY is a failed outcome, not result-lock contention. */
    if (status.state == AZ_ANALYSIS_COMPLETE && status.result == AZ_OK) {
        copying = 1;
        read_result();
    } else releasing = 1;
}
static void tick(void)
{
    if (!request_id) return;
    if (releasing) release_result();
    else if (copying) read_result();
    else observe_result();
}
static int service_ready(void)
{
    return AZ_ANALYSIS_HAS(analysis, request) && AZ_ANALYSIS_HAS(analysis, status) &&
           AZ_ANALYSIS_HAS(analysis, result) && AZ_ANALYSIS_HAS(analysis, cancel) &&
           AZ_ANALYSIS_HAS(analysis, release) && AZ_OBSERVATION_HAS(observations, on_deck_status) &&
           AZ_OBSERVATION_HAS(observations, deck_status_release);
}
static int init(const az_core_v1 *c)
{
    if (!c || c->abi != AZ_MOD_ABI || !AZ_HAS(c, analysis_service) || !AZ_HAS(c, observation_service)) return -1;
    core = c;
    analysis = c->analysis_service(AZ_ANALYSIS_ABI);
    observations = c->observation_service(AZ_OBSERVATION_ABI);
    if (!service_ready()) return -2;
    az_deck_watch_v1 watch = { .size = sizeof watch, .version = AZ_DATA_VERSION, .deck_mask = AZ_STATUS_ALL_DECKS, .topics = AZ_STATUS_TOPIC(AZ_STATUS_LOAD) };
    return observations->on_deck_status(ID, &watch, loaded, NULL, &subscription);
}
static void stop(void)
{
    if (request_id) {
        analysis->cancel(ID, request_id);
        analysis->release(ID, request_id);
    }
    if (subscription) observations->deck_status_release(ID, subscription);
    request_id = subscription = 0;
    releasing = copying = 0;
}
static const char *const firmware[] = { "any", NULL };
static const az_mod_v1 mod = { .abi = AZ_MOD_ABI, .size = sizeof mod, .id = ID, .fw_tags = firmware, .init = init, .tick = tick, .stop = stop };
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
