/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-data-helpers.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int seconds_equal(double a, double b)
{
    int order = compare_timestamps(a, b, 1, 48000, 1); /* test native clock */
    assert(order != AZ_TIME_INVALID);
    return order == AZ_TIME_EQUAL;
}
static az_status_field_info_v1 scalar(uint64_t load)
{
    az_status_field_info_v1 f;
    memset(&f, 0, sizeof f);
    f.scope = AZ_STATUS_LOAD_SCOPE;
    f.scope_id = load;
    f.count = 1;
    f.info.availability = AZ_AVAIL_READY;
    return f;
}
static az_status_phrase_endpoint_v1 endpoint(uint32_t kind)
{
    az_status_phrase_endpoint_v1 p;
    memset(&p, 0, sizeof p);
    p.kind = kind;
    if (kind == AZ_PHRASE_ITEM) p.phrase.end_beat = 4;
    p.phrase_provider_id = 11;
    p.phrase_revision = 3;
    p.grid.load_epoch = 7;
    p.grid.provider_id = 12;
    p.grid.revision = 8;
    p.grid.provenance = AZ_PROV_FILE;
    p.grid.grid_field = AZ_STATUS_GRID_CURRENT;
    return p;
}

int main(void)
{
    az_deck_status_event_v1 e;
    az_deck_status_view_v1 view;
    az_status_loop_v1 loop;
    memset(&e, 0, sizeof e);
    memset(&view, 0, sizeof view);
    memset(&loop, 0, sizeof loop);
    e.size = sizeof e;
    e.version = AZ_DATA_VERSION;
    e.deck = 1;
    e.kind = AZ_STATUS_INITIAL;
    e.load_epoch = 7;
    e.topics = AZ_STATUS_TOPIC(AZ_STATUS_LOOP);
    e.view = &view;
    view.size = sizeof view;
    view.version = AZ_DATA_VERSION;
    view.loop = &loop;
    loop.header.size = sizeof loop;
    loop.header.version = AZ_DATA_VERSION;
    loop.header.supported = loop.header.valid = az_status_field_bit(AZ_STATUS_LOOP_RETAINED_IN) | az_status_field_bit(AZ_STATUS_LOOP_ENGAGED);
    loop.retained_in_info = loop.engaged_info = scalar(7);
    loop.retained_out_info.info.availability = AZ_AVAIL_EMPTY;
    const az_status_loop_v1 *borrow = NULL;
    int on = 123;
    double sec = 99;
    assert(az_read_event_loop(&e, &borrow) && borrow == &loop);
    assert(az_read_event_loop_engaged(&e, &on) && !on);
    assert(az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec) && seconds_equal(sec, 0.0));
    loop.retained_in.seconds = 1.0; /* rate0 remains a readable seconds-only scalar */
    assert(az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec) && seconds_equal(sec, 1.0));
    assert(compare_timestamps(sec, 1.0, 1, loop.retained_in.rate_hz, 1) == AZ_TIME_INVALID);
    loop.retained_in.seconds = -1.0;
    assert(!az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec) && seconds_equal(sec, 1.0));
    assert(!az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, NULL));
    loop.retained_in.seconds = -0.0;
    assert(az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec) && seconds_equal(sec, 0.0));
    loop.retained_in.rate_hz = 48000;
    loop.retained_in.samples = 1;
    loop.retained_in.seconds = 1.0 / loop.retained_in.rate_hz;
    assert(az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec));
    assert(compare_timestamps(loop.retained_in.samples, 2, loop.retained_in.rate_hz, loop.retained_in.rate_hz, 1) == AZ_TIME_BEFORE);
    assert(compare_timestamps(loop.retained_in.samples, 2, loop.retained_in.rate_hz, loop.retained_in.rate_hz, 2) == AZ_TIME_EQUAL);
    loop.retained_in.samples = 0;
    loop.retained_in.seconds = 0;
    assert(az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec) && seconds_equal(sec, 0.0));
    loop.engaged = 1;
    assert(az_read_event_loop_engaged(&e, &on) && on == 1);
    assert(!az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_OUT, &sec) && seconds_equal(sec, 0.0));
    assert(!az_read_event_loop_point(&e, 64, &sec) && seconds_equal(sec, 0.0));
    loop.header.size = offsetof(az_status_loop_v1, engaged_info);
    assert(!az_read_event_loop(&e, &borrow) && borrow == &loop);
    assert(!az_read_event_loop_engaged(&e, &on) && on == 1);
    assert(az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec));
    loop.header.size = sizeof loop;
    loop.retained_in.seconds = NAN;
    assert(!az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec) && seconds_equal(sec, 0.0));
    loop.engaged_info.info.availability = AZ_AVAIL_LOADING;
    assert(!az_read_event_loop_engaged(&e, &on) && on == 1);
    loop.engaged_info = scalar(8);
    assert(!az_read_event_loop_engaged(&e, &on) && on == 1);
    loop.engaged_info = scalar(7);
    loop.engaged_info.scope = AZ_STATUS_UNIT;
    loop.retained_in_info.scope = AZ_STATUS_UNIT;
    loop.retained_in.seconds = 2;
    assert(!az_read_event_loop_engaged(&e, &on) && on == 1);
    assert(!az_read_event_loop_point(&e, AZ_STATUS_LOOP_RETAINED_IN, &sec) && seconds_equal(sec, 0.0));
    loop.engaged_info = scalar(7);
    e.deck = 4;
    assert(!az_read_event_loop(&e, &borrow));
    e.deck = 1;
    uint32_t *short_record = (uint32_t *)malloc(sizeof *short_record);
    assert(short_record);
    *short_record = sizeof *short_record;
    assert(!az_read_event_loop((const az_deck_status_event_v1 *)short_record, &borrow));
    e.view = (const az_deck_status_view_v1 *)short_record;
    assert(!az_read_event_loop(&e, &borrow));
    e.view = &view;

    az_status_phrase_transition_v1 t;
    az_status_phrase_playback_v1 active;
    memset(&t, 0, sizeof t);
    memset(&active, 0, sizeof active);
    t.size = sizeof t;
    t.version = AZ_DATA_VERSION;
    t.from = endpoint(AZ_PHRASE_START);
    t.to = endpoint(AZ_PHRASE_ITEM);
    active.header.size = sizeof active;
    active.header.version = AZ_DATA_VERSION;
    active.header.valid = active.header.supported = az_status_field_bit(AZ_STATUS_PHRASE_TRANSITION_ACTIVE);
    active.active = &t.to;
    active.active_info = scalar(7);
    view.phrase_playback = &active;
    e.topics = e.changed_topics = AZ_STATUS_TOPIC(AZ_STATUS_PHRASE_TRANSITION);
    e.kind = AZ_STATUS_CHANGE;
    e.phrase_transition = &t;
    struct {
        az_status_phrase_transition_v1 copy;
        uint64_t tail;
    } out;
    memset(&out, 0, sizeof out);
    out.copy.size = sizeof out;
    out.tail = 123;
    assert(az_read_phrase_transition(&e, &out.copy));
    assert(out.copy.from.kind == AZ_PHRASE_START && out.copy.to.kind == AZ_PHRASE_ITEM && out.copy.size == sizeof out && out.tail == 123);
    t.from = t.to;
    t.to = endpoint(AZ_PHRASE_END);
    t.discontinuity = AZ_STATUS_JUMP;
    assert(az_read_phrase_transition(&e, &out.copy) && out.copy.to.kind == AZ_PHRASE_END);
    az_status_phrase_transition_v1 saved = out.copy;
    az_status_phrase_endpoint_v1 wrong = t.to;
    wrong.grid.revision++;
    active.active = &wrong;
    assert(!az_read_phrase_transition(&e, &out.copy));
    active.active = &t.to;
    t.from.phrase_revision++;
    assert(!az_read_phrase_transition(&e, &out.copy));
    t.from.phrase_revision--;
    t.to = t.from;
    assert(!az_read_phrase_transition(&e, &out.copy));
    t.to = endpoint(AZ_PHRASE_END);
    e.kind = AZ_STATUS_INITIAL;
    assert(!az_read_phrase_transition(&e, &out.copy));
    e.kind = AZ_STATUS_RESYNC;
    assert(!az_read_phrase_transition(&e, &out.copy));
    e.kind = AZ_STATUS_CHANGE;
    e.phrase_transition = NULL;
    assert(!az_read_phrase_transition(&e, &out.copy));
    e.phrase_transition = (const az_status_phrase_transition_v1 *)short_record;
    assert(!az_read_phrase_transition(&e, &out.copy));
    e.phrase_transition = &t;
    assert(!az_read_phrase_transition(&e, (az_status_phrase_transition_v1 *)short_record));
    assert(!memcmp(&saved, &out.copy, sizeof saved) && out.tail == 123);
    free(short_record);
    return 0;
}
