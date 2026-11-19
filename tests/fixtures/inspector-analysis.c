/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
#include "analysis.c"

static az_library_v1 service;
const az_library_v1 *library = &service;
static int requests, wave_requests, released, painted, bars;
static int track_status = AZ_OK, wave_status = AZ_PENDING, release_status = AZ_OK;
static int caps(az_data_capabilities *out)
{
    (void)out;
    return AZ_OK;
}
static int deck(int index, az_deck_snapshot *out)
{
    (void)index;
    (void)out;
    return AZ_UNSUPPORTED;
}
static const uint8_t overview[] = { 0, 64, 255, 128 };
static const uint8_t columns[] = { 0, 0xe8, 0xff, 4 };
static const az_phrase phrases[] = { { .raw_kind = 7, .raw_mood = 2 } };
static az_track_snapshot track = {
    .size = sizeof track, .version = AZ_DATA_VERSION, .beat_count = 3, .phrase_count = 1, .phrases = phrases, .grid_info = { .availability = AZ_AVAIL_READY }, .phrases_info = { .availability = AZ_AVAIL_READY }, .waveform = { .sample_count = 4, .samples = overview, .seconds_per_sample = .5 }
};
static az_wave_snapshot wave = {
    .size = sizeof wave, .version = AZ_DATA_VERSION, .column_count = 4, .total_columns = 100, .columns = columns, .seconds_per_column = .01
};
static int tracks(const char *id, int stick, int playlist, int (*fn)(const az_track *, void *), void *user)
{
    assert(!strcmp(id, ID) && stick == 0 && playlist == 0);
    az_track item = { .id = 42 };
    assert(fn(&item, user) == 1);
    return 1;
}
static int resolve(const char *id, int stick, uint32_t selected, az_track_ref *out)
{
    assert(!strcmp(id, ID) && stick == 0 && selected == 42);
    *out = (az_track_ref){ .size = out->size, .version = AZ_DATA_VERSION, .track_id = selected };
    return AZ_OK;
}
static int request(const char *id, const az_track_ref *ref, uint32_t mask, uint64_t *out)
{
    assert(!strcmp(id, ID) && ref->track_id == 42 && mask == 31);
    if (!requests++) return AZ_BUSY;
    *out = 1;
    return AZ_OK;
}
static int wave_request(const char *id, const az_track_ref *ref, const az_wave_query *query, uint64_t *out)
{
    assert(!strcmp(id, ID) && ref->track_id == 42 && query->encoding == AZ_WAVE_PWV3_MONO && query->column_count == 4096);
    if (!wave_requests++) return AZ_BUSY;
    *out = 2;
    return AZ_OK;
}
static int acquire(const char *id, uint64_t value, const az_track_snapshot **out)
{
    assert(!strcmp(id, ID) && value == 1);
    *out = track_status == AZ_OK ? &track : NULL;
    return track_status;
}
static int wave_acquire(const char *id, uint64_t value, const az_wave_snapshot **out)
{
    assert(!strcmp(id, ID) && value == 2);
    *out = wave_status == AZ_OK ? &wave : NULL;
    return wave_status;
}
static int release(const char *id, uint64_t value)
{
    assert(!strcmp(id, ID) && (value == 1 || value == 2));
    if (release_status == AZ_OK) ++released;
    return release_status;
}
static int timing(const az_track_snapshot *s, double time, double *beat)
{
    assert(s == &track);
    (void)time;
    *beat = .5;
    return AZ_OK;
}
static int start(const char *id, const char *name, void *(*fn)(void *), void *arg)
{
    assert(!strcmp(id, ID) && !strcmp(name, "track-browser"));
    fn(arg);
    return 0;
}
static uint32_t color(int key)
{
    (void)key;
    return 0xffffff;
}
static int width(const char *text, int px) { return (int)strlen(text) * px / 2; }
static void paint(az_draw *d, int x, int y, const char *text, int px, uint32_t rgb)
{
    (void)d;
    (void)text;
    assert(x >= 0 && y >= 0 && px == 16 && rgb == 0xffffff);
    ++painted;
}
static void rect(az_draw *d, int x, int y, int w, int h, uint32_t rgb)
{
    (void)d;
    assert(x >= 0 && y >= 0 && w >= 0 && h > 0 && rgb == 0xffffff);
    ++bars;
}
int main(void)
{
    service = (az_library_v1){ .abi = AZ_LIBRARY_ABI, .size = sizeof service, .lib_tracks = tracks, .data_capabilities = caps, .deck_snapshot = deck, .track_resolve = resolve, .track_request = request, .track_acquire = acquire, .track_release = release, .waveform_request = wave_request, .waveform_acquire = wave_acquire, .time_to_beat = timing };
    const az_core_v1 c = { .abi = AZ_MOD_ABI, .size = sizeof c, .thread_start = start, .color = color, .text_width = width, .draw_text = paint, .fill_rect = rect };
    strcpy(track.metadata.title, "Analysis fixture");
    tick(); /* The page has no outstanding requests before initialization. */
    assert(init(&c) == 0 && requests == 2 && wave_requests == 2);
    track_status = AZ_BUSY;
    tick();
    assert(ticket == 1 && wave_ticket == 2 && !released);
    track_status = AZ_OK;
    tick();
    assert(!ticket && wave_ticket == 2 && released == 1);
    assert(strstr(lines[1], "Analysis fixture") && strstr(lines[2], "3 beats, 1 phrases"));
    assert(strstr(lines[5], "kind 7, mood 2") && strstr(lines[7], "beat 0.500"));
    assert(preview_count == 4 && !memcmp(preview, overview, 4));
    draw(NULL, (az_rect){ 0, 0, 1280, 800 }, NULL);
    assert(painted == 8 && bars == 3);
    draw(NULL, (az_rect){ 0, 0, 20, 20 }, NULL);
    assert(painted == 8 && bars == 3);
    wave_status = AZ_OK;
    tick();
    assert(!wave_ticket && released == 2 && detail_ready);
    assert(strstr(lines[4], "Detail: 4/100") && strstr(lines[6], "PWV3 height"));
    assert(preview[0] == 0 && preview[1] == 8 * 255 / 31 && preview[2] == 255 && preview[3] == 4 * 255 / 31);
    track_status = AZ_STALE;
    release_status = AZ_BUSY;
    atomic_store(&ready_ticket, 1);
    tick();
    assert(ticket == 1 && strstr(lines[1], "Track data status"));
    release_status = AZ_OK;
    tick();
    assert(!ticket && released == 3);
    atomic_store(&ready_wave, 2);
    wave_status = AZ_UNSUPPORTED;
    tick();
    assert(!wave_ticket && released == 4 && strstr(lines[4], "unavailable"));
    service.size = offsetof(az_library_v1, track_acquire);
    assert(init(&c) == -1);
    return 0;
}
