/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "../../examples/loop-watch/loop-watch.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static unsigned logs, registrations;
static char line[2048];
static az_deck_status_fn callback;
static void *callback_user;
static void log_line(const char *id, const char *format, ...)
{
    assert(!strcmp(id, ID));
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    assert(n > 0 && (size_t)n < sizeof line);
    ++logs;
}
static int subscribe(const char *id, const az_deck_watch_v1 *watch, az_deck_status_fn fn,
                     void *user, az_deck_subscription *out)
{
    assert(!strcmp(id, ID) && watch->deck_mask == AZ_STATUS_ALL_DECKS);
    assert(watch->topics == AZ_STATUS_TOPIC(AZ_STATUS_LOOP));
    callback = fn;
    callback_user = user;
    *out = 1;
    ++registrations;
    return AZ_OK;
}
static int release(const char *id, az_deck_subscription handle)
{
    (void)id;
    (void)handle;
    assert(!"process-lifetime listener must remain registered");
    return AZ_FAILED;
}
static az_observation_v1 library = { .abi = AZ_OBSERVATION_ABI, .size = sizeof library, .on_deck_status = subscribe, .deck_status_release = release };
static const az_observation_v1 *service(uint32_t abi)
{
    assert(abi == AZ_OBSERVATION_ABI);
    return &library;
}
int main(void)
{
    const az_core_v1 host = { .abi = AZ_MOD_ABI, .size = sizeof host, .log = log_line, .observation_service = service };
    assert(!init(&host) && registrations == 1 && subscription == 1);
    assert(az_mod_v1_entry()->init == init);
    az_status_loop_v1 loop = { .header = { .size = sizeof loop, .version = AZ_DATA_VERSION } };
    az_deck_status_view_v1 view = { .size = sizeof view, .version = AZ_DATA_VERSION, .loop = &loop };
    az_deck_status_event_v1 e = { .size = sizeof e, .version = AZ_DATA_VERSION, .deck = 1, .kind = AZ_STATUS_INITIAL, .load_epoch = 7, .sequence = 1, .view = &view, .topics = AZ_STATUS_TOPIC(AZ_STATUS_LOOP) };
    callback(NULL, callback_user);
    callback(&e, callback_user);
    assert(!logs); /* Empty-load initial state is quiet. */
    loop.header.supported = loop.header.valid = az_status_field_bit(AZ_STATUS_LOOP_RETAINED_IN) |
                                                az_status_field_bit(AZ_STATUS_LOOP_RETAINED_OUT) | az_status_field_bit(AZ_STATUS_LOOP_ENGAGED);
    loop.retained_in_info = (az_status_field_info_v1){ .scope = AZ_STATUS_LOAD_SCOPE, .scope_id = 7, .count = 1, .info.availability = AZ_AVAIL_READY };
    loop.retained_out_info = loop.engaged_info = loop.retained_in_info;
    loop.retained_in.seconds = 0;
    loop.retained_out.seconds = 2;
    loop.engaged = 1;
    e.sequence = 2;
    callback(&e, callback_user);
    assert(logs == 1 && strstr(line, "deck=2 seq=2") && strstr(line, "in=0.000000") && strstr(line, "out=2.000000 engaged=1"));
    loop.header.valid = 0;
    e.sequence = 3;
    callback(&e, callback_user);
    assert(logs == 2 && strstr(line, "engaged=-1")); /* Clearing retained state is observable. */
    callback(&e, callback_user);
    assert(logs == 2);
    e.kind = AZ_STATUS_RESYNC;
    e.lost_topics = AZ_STATUS_TOPIC(AZ_STATUS_LOOP);
    callback(&e, callback_user);
    assert(logs == 3 && strstr(line, "lost="));
    library.size = offsetof(az_observation_v1, on_deck_status);
    assert(init(&host) == -1 && registrations == 1);
    assert(init(NULL) == -1);
    puts("PASS loop watch: retained zero, exit, empty baseline and recovery");
    return 0;
}
