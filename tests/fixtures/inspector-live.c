/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
#include <math.h>
#include "live.c"

static az_deck_status_fn receive;
static unsigned registered;
static int subscribe(const char *id, const az_deck_watch_v1 *w, az_deck_status_fn fn, void *user, az_deck_subscription *out)
{
    assert(!strcmp(id, ID) && w->topics == AZ_STATUS_ALL_V1 && w->deck_mask == 15 && !user);
    *out = ++registered;
    receive = fn;
    return AZ_OK;
}
static int release(const char *id, az_deck_subscription s)
{
    (void)id;
    (void)s;
    return AZ_OK;
}
static int poll_trap(int deck, az_deck_snapshot *out)
{
    (void)deck;
    (void)out;
    assert(0);
    return AZ_FAILED;
}
static const az_library_v1 service = { .abi = AZ_LIBRARY_ABI, .size = sizeof service, .on_deck_status = subscribe, .deck_status_release = release, .deck_snapshot = poll_trap };
const az_library_v1 *library = &service;
static unsigned drawn_lines, redraws;
static char drawn_title[116];
static uint32_t text_color(int role) { return (uint32_t)role; }
static int width(const char *text, int px) { return (int)strlen(text) * px / 2; }
static void paint_text(az_draw *d, int x, int y, const char *text, int px, uint32_t color)
{
    (void)d;
    (void)x;
    (void)y;
    (void)px;
    (void)color;
    if (!drawn_lines) snprintf(drawn_title, sizeof drawn_title, "%s", text);
    drawn_lines++;
}
static void redraw(void) { redraws++; }
int main(void)
{
    az_core_v1 c = { .color = text_color, .text_width = width, .draw_text = paint_text, .request_redraw = redraw };
    assert(init(&c) == 0 && registered == 1 && !library_page_live.tick && receive);
    az_status_loop_v1 l = { 0 };
    l.header = (az_status_topic_v1){ sizeof l, 1, 9, 9, 9 };
    l.engaged = 1;
    l.engaged_info = (az_status_field_info_v1){ .info = { AZ_AVAIL_READY, AZ_PROV_NATIVE, 1, 1 }, .scope = AZ_STATUS_LOAD_SCOPE, .count = 1, .scope_id = 7 };
    l.retained_in_info = l.engaged_info;
    l.retained_out_info.info.availability = AZ_AVAIL_EMPTY;
    az_deck_status_view_v1 v = { .size = sizeof v, .version = 1, .loop = &l };
    az_deck_status_event_v1 e = { .size = sizeof e, .version = 1, .kind = AZ_STATUS_CHANGE, .deck = 1, .topics = AZ_STATUS_TOPIC(AZ_STATUS_LOOP), .load_epoch = 7, .view = &v };
    receive(&e, NULL);
    assert(strstr(lines[1][LIVE_CUES][0], "engaged"));
    assert(strstr(lines[1][LIVE_CUES][1], "0.000000"));
    assert(strstr(lines[1][LIVE_CUES][2], "empty"));
    l.header.valid = 0;
    l.engaged_info.info.availability = AZ_AVAIL_STALE;
    l.retained_in_info.info.availability = AZ_AVAIL_STALE;
    e.kind = AZ_STATUS_RESYNC;
    receive(&e, NULL);
    assert(strstr(lines[1][LIVE_CUES][0], "stale") && strstr(lines[1][LIVE_CUES][1], "stale"));
    v.loop = NULL;
    receive(&e, NULL);
    assert(!lines[1][LIVE_CUES][0][0] && !lines[1][LIVE_CUES][1][0]);
    /* A complete ready receipt exercises every copied page. Withdrawal must
     * clear the earlier strings rather than leave a plausible stale value. */
    az_status_metadata_v1 metadata = { .header = { .size = sizeof metadata, .version = AZ_DATA_VERSION }, .title = "Fixture track" };
    metadata.title_info.info.availability = AZ_AVAIL_READY;
    az_status_position_v1 position = { .header = { .size = sizeof position, .version = AZ_DATA_VERSION } };
    position.head.seconds = 1.25;
    position.remaining_s = 8.75;
    position.head_info.info.availability = position.remaining_s_info.info.availability = AZ_AVAIL_READY;
    az_status_transport_v1 transport = { .header = { .size = sizeof transport, .version = AZ_DATA_VERSION }, .state = AZ_PLAYBACK_PLAYING };
    transport.state_info.info.availability = AZ_AVAIL_READY;
    az_status_sync_v1 sync = { .header = { .size = sizeof sync, .version = AZ_DATA_VERSION }, .enabled = 1 };
    sync.enabled_info.info.availability = AZ_AVAIL_READY;
    az_status_tempo_v1 tempo = { .header = { .size = sizeof tempo, .version = AZ_DATA_VERSION }, .current_bpm = 128 };
    tempo.current_bpm_info.info.availability = AZ_AVAIL_READY;
    az_status_key_v1 key = { .header = { .size = sizeof key, .version = AZ_DATA_VERSION }, .original = "8A" };
    key.original_info.info.availability = AZ_AVAIL_READY;
    az_cue_entry memory = { .size = sizeof memory, .version = AZ_DATA_VERSION, .valid = AZ_CUE_START | AZ_CUE_COMMENT, .start_s = 2.5 };
    strcpy(memory.comment, "Opening");
    az_status_cues_v1 cues = { .header = { .size = sizeof cues, .version = AZ_DATA_VERSION }, .live_memory = &memory };
    cues.live_memory_info.info.availability = AZ_AVAIL_READY;
    cues.live_memory_info.count = 1;
    az_beat beat = { .time_s = 0.5 };
    az_phrase phrase = { .start_beat = 1, .end_beat = 9, .raw_kind = 3 };
    az_status_grid_v1 grid = { .header = { .size = sizeof grid, .version = AZ_DATA_VERSION }, .current = &beat };
    grid.current_info.info.availability = AZ_AVAIL_READY;
    grid.current_info.count = 1;
    az_status_phrases_v1 phrases = { .header = { .size = sizeof phrases, .version = AZ_DATA_VERSION }, .current = &phrase };
    phrases.current_info.info.availability = AZ_AVAIL_READY;
    phrases.current_info.count = 1;
    az_waveform overview_data = { .size = sizeof overview_data, .sample_count = 64 };
    az_status_overview_v1 overview = { .header = { .size = sizeof overview, .version = AZ_DATA_VERSION }, .saved = &overview_data };
    overview.saved_info.info.availability = AZ_AVAIL_READY;
    az_status_phrase_endpoint_v1 active = { .kind = AZ_PHRASE_ITEM, .index = 2, .phrase = { .raw_kind = 3 } };
    az_status_phrase_playback_v1 playback = { .header = { .size = sizeof playback, .version = AZ_DATA_VERSION }, .active = &active };
    playback.active_info.info.availability = AZ_AVAIL_READY;
    v.metadata = &metadata;
    v.position = &position;
    v.transport = &transport;
    v.sync = &sync;
    v.tempo = &tempo;
    v.key = &key;
    v.cues = &cues;
    v.grid = &grid;
    v.phrases = &phrases;
    v.overview = &overview;
    v.phrase_playback = &playback;
    receive(&e, NULL);
    assert(strstr(lines[1][LIVE_STATUS][1], "Fixture track"));
    assert(strstr(lines[1][LIVE_STATUS][2], "1.250 / remaining 8.750"));
    assert(strstr(lines[1][LIVE_STATUS][3], "playing / Sync on"));
    assert(strstr(lines[1][LIVE_STATUS][4], "128.000 / original key 8A"));
    assert(strstr(lines[1][LIVE_CUES][5], "2.500 s  Opening"));
    assert(strstr(lines[1][LIVE_ANALYSIS][3], "64 samples"));
    assert(strstr(lines[1][LIVE_ANALYSIS][4], "Active phrase 3 (kind 3)"));
    assert(strstr(lines[1][LIVE_ANALYSIS][5], "0.500 s / first phrase beats 1..9"));
    for (unsigned kind = AZ_PHRASE_START; kind <= AZ_PHRASE_GAP; ++kind) {
        active.kind = kind;
        receive(&e, NULL);
        assert(strstr(lines[1][LIVE_ANALYSIS][4], "Active "));
    }
    transport.state = AZ_PLAYBACK_PAUSED;
    sync.enabled = 0;
    receive(&e, NULL);
    assert(strstr(lines[1][LIVE_STATUS][3], "paused / Sync off"));
    position.head.seconds = NAN;
    playback.active_info.info.availability = AZ_AVAIL_STALE;
    receive(&e, NULL);
    assert(strstr(lines[1][LIVE_STATUS][2], "Head unavailable"));
    assert(strstr(lines[1][LIVE_ANALYSIS][4], "stale"));
    v = (az_deck_status_view_v1){ .size = sizeof v, .version = AZ_DATA_VERSION };
    receive(&e, NULL);
    assert(!lines[1][LIVE_STATUS][1][0] && !lines[1][LIVE_CUES][5][0] && !lines[1][LIVE_ANALYSIS][4][0]);
    az_event knob = { .kind = AZ_EV_KNOB, .dir = 1 };
    assert(input(&knob, NULL) && selected == 1 && redraws == 1);
    draw(NULL, (az_rect){ 0, 0, 1000, 300 }, NULL);
    assert(drawn_lines == 7 && strstr(drawn_title, "Deck 2 / Status"));
    az_event click = { .kind = AZ_EV_CLICK };
    assert(input(&click, NULL) && page == LIVE_CUES);
    knob.dir = -1;
    assert(input(&knob, NULL) && selected == 0);
    knob.dir = 0;
    assert(!input(&knob, NULL));
    drawn_lines = 0;
    draw(NULL, (az_rect){ 0, 0, 31, 300 }, NULL);
    assert(!drawn_lines);
    draw(NULL, (az_rect){ 0, 0, 1000, 31 }, NULL);
    assert(!drawn_lines);
    draw(NULL, (az_rect){ 0, 0, 1000, 60 }, NULL);
    assert(drawn_lines == 1);
    return 0;
}
