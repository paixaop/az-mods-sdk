/* SPDX-License-Identifier: GPL-3.0-only */
/* Compiled into the SDK harness; the real packaged client's entry points are
 * invoked by dlopen through the public library service. */
static int harness_scenarios(const char *id)
{
    if (strcmp(id, "loop-watch")) return 0;
    expect(!mod->tick && !mod->on_frame && !screen && !nthreads, "loop-only client without polling, worker or screen");
    hs_deck_changed(0, AZ_DECK_EJECTED);
    hs_drain();
    unsigned long before = log_lines;
    hs_loop_fixture(0);
    expect(log_lines == before + 4, "retained IN, OUT/activate, EXIT and RELOOP each delivered once");
    expect(strstr(last_log, "deck=1 ") && strstr(last_log, "in=0.000000") && strstr(last_log, "engaged=1"),
           "re-entered working loop has a valid zero IN on deck 1");
    before = log_lines;
    hs_phrase_fixture(0, AZ_PHRASE_ITEM, AZ_STATUS_TRAVERSAL);
    hs_phrase_fixture(0, AZ_PHRASE_END, AZ_STATUS_JUMP);
    hs_drain();
    expect(log_lines == before, "unsubscribed phrase and position events stay quiet");
    for (unsigned i = 0; i < HS_HISTORY + 2; ++i) {
        struct hs_record r = hs_next(0);
        r.image.tempo.current_bpm = 100 + (i % 10);
        hs_field(&r, 0, AZ_STATUS_TEMPO, &r.image.tempo.header, AZ_STATUS_TEMPO_CURRENT_BPM,
                 &r.image.tempo.current_bpm_info, AZ_AVAIL_READY, AZ_STATUS_UNIT, 1);
        hs_commit(0, &r);
    }
    hs_drain();
    expect(log_lines == before, "overwritten tempo history does not cause loop resync");
    struct hs_record r = hs_next(0);
    r.image.loop.engaged = 0;
    hs_field(&r, 0, AZ_STATUS_LOOP, &r.image.loop.header, AZ_STATUS_LOOP_ENGAGED,
             &r.image.loop.engaged_info, AZ_AVAIL_READY, AZ_STATUS_LOAD_SCOPE, 1);
    hs_commit(0, &r);
    for (unsigned i = 0; i < HS_HISTORY + 2; ++i) {
        r = hs_next(0);
        r.image.tempo.current_bpm = 120 + (i % 10);
        hs_field(&r, 0, AZ_STATUS_TEMPO, &r.image.tempo.header, AZ_STATUS_TEMPO_CURRENT_BPM,
                 &r.image.tempo.current_bpm_info, AZ_AVAIL_READY, AZ_STATUS_UNIT, 1);
        hs_commit(0, &r);
    }
    hs_drain();
    expect(log_lines == before + 1 && strstr(last_log, "kind=3") && strstr(last_log, "engaged=0"),
           "final evicted EXIT reports one filtered recovery with current state");
    return 1;
}
