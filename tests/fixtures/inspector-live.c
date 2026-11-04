/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
#include <time.h>
static int clock_failed, read_status;
static az_deck_snapshot source;
static int test_clock(clockid_t id, struct timespec *out)
{
    assert(id == CLOCK_MONOTONIC);
    out->tv_sec = 0;
    out->tv_nsec = 10;
    return clock_failed ? -1 : 0;
}
#define clock_gettime test_clock
#include "live.c"
static int read_deck(int deck, az_deck_snapshot *out)
{
    *out = source;
    out->deck = (uint32_t)deck;
    return read_status;
}
static int caps(az_data_capabilities *out)
{
    (void)out;
    return AZ_OK;
}
static const az_library_v1 service = { .abi = AZ_LIBRARY_ABI, .size = sizeof service, .data_capabilities = caps, .deck_snapshot = read_deck };
const az_library_v1 *library = &service;
int main(void)
{
    az_core_v1 c = { 0 };
    assert(init(&c) == 0);
    source.size = sizeof source;
    source.version = AZ_DATA_VERSION;
    source.valid = AZ_LIVE_LOADED | AZ_LIVE_LOOP_ENGAGED | AZ_LIVE_LOOP_BOUNDS;
    source.loaded = 1;
    source.loop.size = sizeof source.loop;
    source.loop.version = AZ_DATA_VERSION;
    source.loop.valid = AZ_LOOP_ENGAGED | AZ_LOOP_BOUNDS;
    source.loop.engaged = 1;
    source.loop.out_s = 4;
    source.loop_unit_generation = 1;
    source.loop_revision = 1;
    source.loop_observed_ns = 5;
    tick();
    assert(strstr(lines[1], "loaded 1, loop engaged"));
    assert(strstr(lines[2], "IN 0.000000"));
    read_status = AZ_BUSY;
    tick();
    assert(strstr(lines[1], "loaded -1, loop unavailable"));
    assert(strstr(lines[2], "bounds unavailable"));
    read_status = AZ_FAILED;
    tick();
    assert(strstr(lines[1], "loop unavailable"));
    read_status = AZ_OK;
    tick();
    assert(strstr(lines[1], "loop engaged"));
    source.valid &= ~AZ_LIVE_LOOP_ENGAGED;
    tick();
    assert(strstr(lines[1], "loop unavailable"));
    assert(strstr(lines[2], "IN 0.000000"));
    source.valid |= AZ_LIVE_LOOP_ENGAGED;
    source.loop.valid &= ~AZ_LOOP_BOUNDS;
    tick();
    assert(strstr(lines[1], "loop engaged"));
    assert(strstr(lines[2], "bounds unavailable"));
    source.loop.valid |= AZ_LOOP_BOUNDS;
    clock_failed = 1;
    tick();
    assert(strstr(lines[1], "loaded 1, loop unavailable"));
    assert(strstr(lines[2], "bounds unavailable"));
    clock_failed = 0;
    source.version++;
    tick();
    assert(strstr(lines[1], "loaded -1, loop unavailable"));
    source.version--;
    source.size = 4;
    tick();
    assert(strstr(lines[1], "loop unavailable"));
    source.size = sizeof source;
    tick();
    assert(strstr(lines[1], "loop engaged"));
    return 0;
}
