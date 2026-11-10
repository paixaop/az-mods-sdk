/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
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
int main(void)
{
    az_core_v1 c = { 0 };
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
    return 0;
}
