/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>

static unsigned phase_changes;
static uint64_t phase_cut, recovered_cut;
static void phase_changed(const az_deck_status_event_v1 *event, void *unused)
{
    (void)unused;
    assert(event->view->phase && !event->view->position && !event->occurrence);
    if (event->kind == AZ_STATUS_CHANGE) {
        ++phase_changes;
        phase_cut = event->sequence;
    }
    if (event->kind == AZ_STATUS_RESYNC) {
        assert(event->lost_topics == AZ_STATUS_TOPIC(AZ_STATUS_PHASE) && event->loss_reason == AZ_STATUS_EVICTED);
        recovered_cut = event->sequence;
    }
}
static void phase_barrier(void)
{
    const unsigned d = 0;
    hs_deck_changed(d, AZ_DECK_LOADED);
    struct hs_listener *s = &hs_listeners[0];
    *s = (struct hs_listener){ .watch = { sizeof s->watch, AZ_DATA_VERSION, 1, 1, AZ_STATUS_TOPIC(AZ_STATUS_PHASE) },
                               .fn = phase_changed,
                               .initial = 1,
                               .handle = ++hs_next_handle };
    s->baseline[d] = hs_decks[d].current;
    s->after[d] = s->baseline[d].sequence;
    s->discrete[d] = hs_decks[d].discrete;
    hs_drain();
    hs_motion(d, 0);
    hs_drain();
    assert(phase_changes == 1);
    hs_motion(d, 12000);
    hs_drain();
    assert(phase_changes == 1 && hs_deadline);
    hs_phrase_fixture(d, AZ_PHRASE_ITEM, AZ_STATUS_JUMP);
    hs_drain();
    assert(phase_changes == 2 && phase_cut == hs_decks[d].current.sequence && !hs_deadline);
    advance(NS);
    assert(phase_changes == 2);
    hs_motion(d, 18000);
    hs_phrase_fixture(d, AZ_PHRASE_ITEM, AZ_STATUS_SEEK);
    uint64_t barrier = hs_decks[d].current.sequence;
    for (unsigned i = 0; i < HS_HISTORY; ++i) hs_deck_changed(d, i % 2 ? AZ_DECK_STOP : AZ_DECK_PLAY);
    hs_drain();
    assert(recovered_cut == barrier + 1 && phase_changes == 2 && !hs_deadline);
}

static void timestamp_boundaries(void)
{
    hs_reset();
    phase_changes = 0;
    hs_deck_changed(0, AZ_DECK_LOADED);
    struct hs_listener *s = &hs_listeners[0];
    *s = (struct hs_listener){ .watch = { sizeof s->watch, AZ_DATA_VERSION, 1, 1, AZ_STATUS_TOPIC(AZ_STATUS_PHASE) },
                               .fn = phase_changed, .initial = 1, .handle = ++hs_next_handle };
    s->baseline[0] = hs_decks[0].current;
    s->after[0] = s->baseline[0].sequence;
    s->discrete[0] = hs_decks[0].discrete;
    hs_drain();
    hs_motion(0, 0);
    hs_drain();
    assert(phase_changes == 1);
    hs_motion(0, 1);
    hs_drain();
    assert(phase_changes == 1 && hs_deadline);
    advance(NS - 1);
    assert(phase_changes == 1);
    advance(1);
    assert(phase_changes == 2 && !hs_deadline);
    uint64_t cut = hs_decks[0].current.sequence;
    hs_motion(0, 1);
    assert(hs_decks[0].current.sequence == cut);
    hs_motion(0, 2);
    assert(hs_decks[0].current.sequence == cut + 1); /* one frame is a distinct timestamp */
    cut = hs_decks[0].current.sequence;
    hs_motion(0, -1);
    hs_motion(0, 4 * 48000 + 1);
    assert(hs_decks[0].current.sequence == cut);
    hs_reset();
    phase_changes = 0;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    reset_world();
    timestamp_boundaries();
    mod_id = "status-client";
    mod_path = argv[1];
    assert(load_mod() && hs_count() == 1);
    hs_drain();
    hs_loop_fixture(0);
    hs_phrase_fixture(0, AZ_PHRASE_ITEM, AZ_STATUS_TRAVERSAL);
    hs_phrase_fixture(0, AZ_PHRASE_ITEM, AZ_STATUS_WRAP);
    hs_phrase_fixture(0, AZ_PHRASE_END, AZ_STATUS_JUMP);
    hs_drain();
    assert(!hs_count());
    hs_deck_changed(0, AZ_DECK_EJECTED);
    hs_drain();
    phase_barrier();
    return 0;
}
