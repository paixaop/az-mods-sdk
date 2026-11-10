/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-data-helpers.h"
#include <inttypes.h>
#include <time.h>

#define ID "loop-watch"
static const az_core_v1 *core;
static az_deck_subscription subscription;
static struct loop_display {
    double in_s, out_s;
    int has_in, has_out, engaged;
} decks[4];

static void loop_changed(const az_deck_status_event_v1 *e, void *user)
{
    const az_status_loop_v1 *loop;
    if (!az_read_event_loop(e, &loop)) return;
    struct loop_display *state = &((struct loop_display *)user)[e->deck];
    struct loop_display next = { 0 };
    int on;
    next.engaged = az_read_event_loop_engaged(e, &on) ? on : -1;
    next.has_in = az_read_event_loop_point(e, AZ_STATUS_LOOP_RETAINED_IN, &next.in_s);
    next.has_out = az_read_event_loop_point(e, AZ_STATUS_LOOP_RETAINED_OUT, &next.out_s);
    /* Ignore routine empty-load baselines. Always log an edit, an exit or a
     * recovery with the exact callback sequence, including a retained IN at 0. */
    if (next.has_in || next.has_out || next.engaged == 1 || state->has_in || state->has_out ||
        state->engaged == 1 || e->kind == AZ_STATUS_RESYNC) {
        double working_in = 0, working_out = 0;
        int has_working_in = az_read_event_loop_point(e, AZ_STATUS_LOOP_WORKING_IN, &working_in);
        int has_working_out = az_read_event_loop_point(e, AZ_STATUS_LOOP_WORKING_OUT, &working_out);
        struct timespec now;
        uint64_t callback_ns = clock_gettime(CLOCK_MONOTONIC, &now) == 0
                                   ? (uint64_t)now.tv_sec * UINT64_C(1000000000) + now.tv_nsec
                                   : 0;
        core->log(ID, "deck=%u seq=%" PRIu64 " kind=%u load=%" PRIu64
                      " in_status=%u in=%.6f out_status=%u out=%.6f engaged=%d"
                      " working_in_valid=%d working_in=%.6f working_out_valid=%d working_out=%.6f"
                      " changed=%" PRIx64 " lost=%" PRIx64 " observed_ns=%" PRIu64 " published_ns=%" PRIu64 " callback_ns=%" PRIu64,
                  e->deck + 1, e->sequence, e->kind, e->load_epoch, loop->retained_in_info.info.availability, next.in_s,
                  loop->retained_out_info.info.availability, next.out_s, next.engaged, has_working_in, working_in,
                  has_working_out, working_out, loop->header.changed, e->lost_topics, e->observed_ns, e->published_ns, callback_ns);
    }
    *state = next;
}
static int init(const az_core_v1 *c)
{
    if (!AZ_HAS(c, library_service) || !AZ_HAS(c, log)) return -1;
    const az_library_v1 *lib = c->library_service(AZ_LIBRARY_ABI);
    if (!AZ_LIB_HAS(lib, on_deck_status) || !AZ_LIB_HAS(lib, deck_status_release)) return -1;
    core = c;
    const az_deck_watch_v1 watch = { sizeof watch, AZ_DATA_VERSION, AZ_STATUS_ALL_DECKS, 0, AZ_STATUS_TOPIC(AZ_STATUS_LOOP) };
    return lib->on_deck_status(ID, &watch, loop_changed, decks, &subscription) == AZ_OK ? 0 : -1;
}
static const char *const tags[] = { "any", NULL };
static const az_mod_v1 mod = { .abi = AZ_MOD_ABI, .size = sizeof mod, .id = ID, .fw_tags = tags, .init = init };
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
