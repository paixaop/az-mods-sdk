/* SPDX-License-Identifier: GPL-3.0-only */
/* The same dynamically loaded mod is compiled as C and C++, with or without
 * the optional readers. The host compares its public callback transcript. */
#ifdef WITH_HELPERS
#include "azmod-data-helpers.h"
#else
#include "azmod-mod.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ID "status-client"
static const az_library_v1 *library;
static az_deck_subscription handle;
static void changed(const az_deck_status_event_v1 *e, void *user)
{
    assert(user == &handle && handle && e->subscription == handle && e->deck == 0);
    assert(e->view->loop && e->view->phrase_playback && !e->view->grid);
    int engaged = -1, from = -1, to = -1;
    double in = -1;
#ifdef WITH_HELPERS
    az_status_phrase_transition_v1 pair;
    memset(&pair, 0, sizeof pair);
    pair.size = sizeof pair;
    az_read_event_loop_engaged(e, &engaged);
    az_read_event_loop_point(e, AZ_STATUS_LOOP_RETAINED_IN, &in);
    if (az_read_phrase_transition(e, &pair)) {
        from = (int)pair.from.kind;
        to = (int)pair.to.kind;
    }
#else
    const az_status_loop_v1 *loop = e->view->loop;
    if ((loop->header.valid & az_status_field_bit(AZ_STATUS_LOOP_ENGAGED)) &&
        loop->engaged_info.info.availability == AZ_AVAIL_READY) engaged = loop->engaged;
    if ((loop->header.valid & az_status_field_bit(AZ_STATUS_LOOP_RETAINED_IN)) &&
        loop->retained_in_info.info.availability == AZ_AVAIL_READY) in = loop->retained_in.seconds;
    if (e->phrase_transition) {
        from = (int)e->phrase_transition->from.kind;
        to = (int)e->phrase_transition->to.kind;
    }
#endif
    printf("event %llu %u %d %.1f %d %d\n", (unsigned long long)e->sequence, e->kind, engaged, in, from, to);
    if (to == AZ_PHRASE_END) {
        assert(library->deck_status_release(ID, handle) == AZ_OK);
        puts("released");
    }
}
static int initialize(const az_core_v1 *core)
{
    assert(AZ_HAS(core, library_service));
    library = core->library_service(AZ_LIBRARY_ABI);
    assert(AZ_LIB_HAS(library, on_deck_status) && AZ_LIB_HAS(library, deck_status_release));
    const az_deck_watch_v1 watch = { sizeof watch, AZ_DATA_VERSION, 1, 1, AZ_STATUS_TOPIC(AZ_STATUS_LOOP) | AZ_STATUS_TOPIC(AZ_STATUS_PHRASE_TRANSITION) };
    az_deck_subscription untouched = 123;
    assert(library->on_deck_status(ID, &watch, (az_deck_status_fn)(uintptr_t)&handle, NULL, &untouched) == AZ_INVALID);
    assert(untouched == 123 && library->deck_status_release(ID, UINT64_MAX) == AZ_INVALID);
    return library->on_deck_status(ID, &watch, changed, &handle, &handle) == AZ_OK ? 0 : -1;
}
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void)
{
    static const char *const tags[] = { "any", NULL };
    static az_mod_v1 mod;
    mod.abi = AZ_MOD_ABI;
    mod.size = sizeof mod;
    mod.id = ID;
    mod.fw_tags = tags;
    mod.init = initialize;
    return &mod;
}
