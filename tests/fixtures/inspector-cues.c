/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
#include "cues.c"
static az_cue_entry entries[2];
static az_track_snapshot track;
static int acquire(const char *id, uint64_t t, const az_track_snapshot **out)
{
    (void)id;
    assert(t == 1);
    *out = &track;
    return AZ_OK;
}
static const az_library_v1 service = { .abi = AZ_LIBRARY_ABI, .size = sizeof service, .track_acquire = acquire };
const az_library_v1 *library = &service;
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
    return 0;
}
