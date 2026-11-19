/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
#include <time.h>
static uint64_t test_ns = UINT64_C(1000000000);
static int test_clock(clockid_t clock, struct timespec *out)
{
    assert(clock == CLOCK_MONOTONIC);
    out->tv_sec = test_ns / UINT64_C(1000000000);
    out->tv_nsec = test_ns % UINT64_C(1000000000);
    return 0;
}
#define clock_gettime test_clock
#include "cues.c"
static az_cue_entry entries[2];
static az_track_snapshot track;
static int acquire_status = AZ_OK, request_status = AZ_OK, release_status = AZ_OK;
static int request_calls, release_calls, painted;
static int acquire(const char *id, uint64_t t, const az_track_snapshot **out)
{
    (void)id;
    assert(t == 1);
    *out = acquire_status == AZ_OK ? &track : NULL;
    return acquire_status;
}
static int request(const char *id, const az_track_ref *ref, uint32_t mask, uint64_t *out)
{
    assert(!strcmp(id, ID) && ref->track_id >= 41 && mask == (AZ_DATA_METADATA | AZ_DATA_CUES));
    ++request_calls;
    if (request_status == AZ_OK) *out = 1;
    return request_status;
}
static int release(const char *id, uint64_t value)
{
    assert(!strcmp(id, ID) && value == 1);
    ++release_calls;
    return release_status;
}
static int resolve(const char *id, int stick, uint32_t track_id, az_track_ref *out)
{
    assert(!strcmp(id, ID) && stick == 0);
    *out = (az_track_ref){ .size = out->size, .version = AZ_DATA_VERSION, .track_id = track_id };
    return AZ_OK;
}
static int tracks(const char *id, int stick, int playlist, int (*fn)(const az_track *, void *), void *user)
{
    assert(!strcmp(id, ID) && stick == 0 && playlist == 0);
    az_track row = { .id = 41, .title = "First" };
    assert(fn(&row, user) == 0);
    row.id = 42;
    row.title = NULL;
    assert(fn(&row, user) == 0);
    return 2;
}
static int capabilities(az_data_capabilities *out)
{
    out->stored_components = AZ_DATA_CUES;
    return AZ_OK;
}
static int start(const char *id, const char *name, void *(*fn)(void *), void *arg)
{
    assert(!strcmp(id, ID) && !strcmp(name, "cue-browser"));
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
    (void)rgb;
    assert(x >= 0 && y >= 0 && px == 16);
    ++painted;
}
static az_library_v1 service = { .abi = AZ_LIBRARY_ABI, .size = sizeof service, .track_acquire = acquire, .track_request = request, .track_release = release, .track_resolve = resolve, .lib_tracks = tracks, .data_capabilities = capabilities };
const az_library_v1 *library = &service;
static void browse_and_retry(void)
{
    const az_core_v1 c = { .abi = AZ_MOD_ABI, .size = sizeof c, .thread_start = start, .color = color, .text_width = width, .draw_text = paint };
    assert(init(&c) == 0 && catalog_count == 2);
    acquire_status = AZ_PENDING;
    tick();
    assert(request_calls == 1 && ticket == 1 && strstr(lines[2], "Loading"));
    acquire_status = AZ_OK;
    tick();
    assert(strstr(lines[1], "1/2: First") && cue_count == 2);
    assert(input(&(az_event){ .kind = AZ_EV_CLICK }, NULL) == 1 && browse_cues);
    assert(input(&(az_event){ .kind = AZ_EV_KNOB, .dir = 1 }, NULL) == 1 && cue_cursor == 1);
    tick();
    assert(!lines[4][0]);
    draw(NULL, (az_rect){ 0, 0, 1280, 800 }, NULL);
    assert(painted == 8);
    draw(NULL, (az_rect){ 0, 0, 10, 10 }, NULL);
    assert(painted == 8);
    assert(input(&(az_event){ .kind = AZ_EV_BACK }, NULL) == 1 && !browse_cues);
    assert(input(&(az_event){ .kind = AZ_EV_KNOB, .dir = 1 }, NULL) == 1 && selected == 1);
    release_status = AZ_BUSY;
    tick();
    assert(request_calls == 1 && issued == 0);
    release_status = AZ_OK;
    request_status = AZ_BUSY;
    tick();
    assert(!ticket && issued == 0);
    request_status = AZ_OK;
    acquire_status = AZ_STALE;
    tick();
    assert(!ticket && issued == -1 && !cue_count);
    int calls = request_calls;
    tick();
    assert(request_calls == calls); /* Stale file retries are rate limited. */
    test_ns += UINT64_C(1000000000);
    acquire_status = AZ_OK;
    tick();
    assert(request_calls == calls + 1 && issued == 1 && cue_count == 2);
    track.cues_info.availability = AZ_AVAIL_UNSUPPORTED;
    tick();
    assert(!cue_count && strstr(lines[2], "unavailable") && !lines[3][0]);
    assert(input(&(az_event){ .kind = AZ_EV_KNOB }, NULL) == 0);
    service.track_request = NULL;
    assert(init(&c) == -1);
}
int main(void)
{
    catalog_count = 1;
    atomic_store(&catalog_ready, 1);
    selected = issued = 0;
    ticket = 1;
    track.cues = entries;
    track.cue_count = 2;
    track.cues_info.availability = AZ_AVAIL_READY;
    entries[0].classification = AZ_CUE_MEMORY;
    entries[0].type = AZ_CUE_POINT;
    entries[0].valid = AZ_CUE_START | AZ_CUE_COLOR_ID;
    entries[0].color_id = 7;
    entries[0].rgb = 0x123456;
    strcpy(entries[0].comment, "HIDDEN");
    entries[1].classification = AZ_CUE_HOT;
    entries[1].type = AZ_CUE_LOOP;
    entries[1].valid = AZ_CUE_START | AZ_CUE_COLOR;
    entries[1].start_s = 2;
    entries[1].end_s = 9;
    entries[1].rgb = 0x234567;
    tick();
    assert(strstr(lines[3], "0.000s") && !strstr(lines[3], "point 0.000s"));
    assert(strstr(lines[3], "palette 7") && !strstr(lines[3], "HIDDEN") && !row_colored[0]);
    assert(strstr(lines[4], "2.000s loop to unavailable") && row_colored[1] && row_colors[1] == 0x234567);
    entries[0].valid = 0;
    entries[1].valid = AZ_CUE_END | AZ_CUE_COMMENT;
    strcpy(entries[1].comment, "VISIBLE");
    tick();
    assert(strstr(lines[3], "unavailable") && !strstr(lines[3], "0.000s") && !strstr(lines[3], "palette"));
    assert(strstr(lines[4], "unavailable loop to 9.000s") && strstr(lines[4], "VISIBLE") && !row_colored[1]);
    browse_and_retry();
    return 0;
}
