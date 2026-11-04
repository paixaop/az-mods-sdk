/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-data-helpers.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static az_deck_snapshot snapshot(void)
{
    az_deck_snapshot s;
    memset(&s, 0, sizeof s);
    s.size = sizeof s;
    s.version = AZ_DATA_VERSION;
    s.valid = AZ_LIVE_NATIVE_LOAD | AZ_LIVE_LOOP_ENGAGED | AZ_LIVE_LOOP_BOUNDS;
    s.native_load_unit_generation = 1;
    s.native_load_generation = 2;
    s.native_load_observed_ns = 3;
    s.loop.size = sizeof s.loop;
    s.loop.version = AZ_DATA_VERSION;
    s.loop.valid = AZ_LOOP_ENGAGED | AZ_LOOP_BOUNDS;
    s.loop.engaged = 0;
    s.loop_unit_generation = 1;
    s.loop_revision = 2;
    s.loop_observed_ns = 3;
    s.loop.in_s = 0;
    s.loop.out_s = 4;
    return s;
}
int main(void)
{
    uint32_t short_record = sizeof short_record;
    int short_output = 9;
    assert(!az_read_loop_engaged((const az_deck_snapshot *)&short_record, 0, 3, &short_output) && short_output == 9);
    assert(!az_command_result_matches((const az_command_result *)&short_record, 1));
    int engaged = 9;
    double in = 9, out = 9;
    uint64_t generation = 9;
    az_deck_snapshot s = snapshot();
    assert(az_read_loop_engaged(&s, 0, 3, &engaged) && engaged == 0);
    s.loop.engaged = 1;
    assert(az_read_loop_engaged(&s, 0, 3, &engaged) && engaged == 1);
    assert(az_read_loop_bounds(&s, 0, 3, &in, &out) && in == 0 && out == 4);
    assert(az_read_native_load_target(&s, 0, 3, &generation) && generation == 2);
    for (int i = 0; i < 14; ++i) {
        s = snapshot();
        switch (i) {
        case 0: s.version++; break;
        case 1: s.deck = 1; break;
        case 2: s.size = offsetof(az_deck_snapshot, loop_observed_ns); break;
        case 3: s.loop.size = offsetof(az_loop_state, engaged); break;
        case 4: s.loop.version++; break;
        case 5: s.valid &= ~AZ_LIVE_LOOP_ENGAGED; break;
        case 6: s.loop.valid &= ~AZ_LOOP_ENGAGED; break;
        case 7: s.loop_unit_generation = 0; break;
        case 8: s.loop_revision = 0; break;
        case 9: s.loop_observed_ns = 0; break;
        case 10: s.loop_observed_ns = 4; break;
        case 11: s.loop.engaged = -1; break;
        case 12: s.loop.engaged = 2; break;
        case 13: s.size = 4; break;
        }
        engaged = 9;
        assert(!az_read_loop_engaged(&s, 0, 3, &engaged) && engaged == 9);
    }
    for (int i = 0; i < 8; ++i) {
        s = snapshot();
        switch (i) {
        case 0: s.valid &= ~AZ_LIVE_LOOP_BOUNDS; break;
        case 1: s.loop.valid &= ~AZ_LOOP_BOUNDS; break;
        case 2: s.loop.in_s = NAN; break;
        case 3: s.loop.out_s = INFINITY; break;
        case 4: s.loop.size = offsetof(az_loop_state, out_s); break;
        case 5: s.loop_unit_generation = 0; break;
        case 6: s.loop_revision = 0; break;
        case 7: s.loop_observed_ns = 0; break;
        }
        in = out = 9;
        assert(!az_read_loop_bounds(&s, 0, 3, &in, &out) && in == 9 && out == 9);
    }
    s = snapshot();
    s.valid &= ~AZ_LIVE_LOOP_ENGAGED;
    s.loop.valid &= ~AZ_LOOP_ENGAGED;
    assert(az_read_loop_bounds(&s, 0, 3, &in, &out));
    s = snapshot();
    s.valid &= ~AZ_LIVE_LOOP_BOUNDS;
    s.loop.valid &= ~AZ_LOOP_BOUNDS;
    assert(az_read_loop_engaged(&s, 0, 3, &engaged));
    for (int i = 0; i < 7; ++i) {
        s = snapshot();
        switch (i) {
        case 0:
            s.valid &= ~AZ_LIVE_NATIVE_LOAD;
            s.load_generation = 7;
            s.track.track_id = 8;
            break;
        case 1: s.native_load_unit_generation = 0; break;
        case 2: s.native_load_generation = 0; break;
        case 3: s.native_load_observed_ns = 0; break;
        case 4: s.native_load_observed_ns = 4; break;
        case 5: s.size = offsetof(az_deck_snapshot, native_load_observed_ns); break;
        case 6: s.deck = 1; break;
        }
        generation = 9;
        assert(!az_read_native_load_target(&s, 0, 3, &generation) && generation == 9);
    }
    s = snapshot();
    assert(!az_read_native_load_target(&s, 0, 0, &generation));
    assert(!az_read_loop_engaged(&s, 0, 0, &engaged));
    assert(!az_read_loop_bounds(&s, 0, 0, &in, &out));
    assert(!az_read_loop_engaged(NULL, 0, 3, &engaged));
    assert(!az_read_loop_bounds(&s, 0, 3, NULL, &out));
    assert(!az_read_native_load_target(&s, 4, 3, &generation));
    az_command_result r;
    memset(&r, 0, sizeof r);
    r.size = sizeof r;
    r.version = AZ_DATA_VERSION;
    r.request_id = 4;
    for (unsigned i = AZ_COMMAND_ACCEPTED; i <= AZ_COMMAND_UNCERTAIN; ++i) {
        r.outcome = i;
        assert(az_command_result_matches(&r, 4));
    }
    assert(!az_command_result_matches(&r, 0));
    assert(!az_command_result_matches(&r, 3));
    r.size--;
    assert(!az_command_result_matches(&r, 4));
    r.size++;
    r.outcome++;
    assert(!az_command_result_matches(&r, 4));
    /* Integral negatives are invalid before mixed-sign conversion; floats retain signed coordinates. */
    assert(compare_timestamps(-1, -1, 1, 1, 1) == AZ_TIME_INVALID);
    assert(compare_timestamps(-1, UINT64_MAX, 1, 1, 1) == AZ_TIME_INVALID);
    assert(compare_timestamps(UINT64_MAX, -1, 1, 1, 1) == AZ_TIME_INVALID);
    assert(compare_timestamps(UINT64_MAX, UINT64_MAX, 1, 1, 1) == AZ_TIME_EQUAL);
    assert(compare_timestamps(UINT64_MAX, UINT64_MAX - 1, 1, 1, 1) == AZ_TIME_AFTER);
    assert(compare_timestamps(UINT64_C(0), UINT64_MAX, 1, 1, 1) == AZ_TIME_BEFORE);
    assert(compare_timestamps(-1, -1., 1, 1, 1) == AZ_TIME_EQUAL);
    assert(compare_timestamps(-2., -1., 1, 1, 1) == AZ_TIME_BEFORE);
    assert(compare_timestamps(0, 0, 1, 1, 1) == AZ_TIME_EQUAL);
    assert(compare_timestamps(1, 2, 1, 1, 1) == AZ_TIME_BEFORE);
    assert(compare_timestamps(.5, 1., 1, 1, 1) == AZ_TIME_EQUAL);
    assert(compare_timestamps(INFINITY, 1, 1, 1, 1) == AZ_TIME_INVALID);
    assert(compare_timestamps(NAN, 1, 1, 1, 1) == AZ_TIME_INVALID);
    assert(compare_timestamps(0, 0, 0, 1, 1) == AZ_TIME_INVALID);
    int first = 0, second = 0, units = 1, rate = 1, window = 1;
    assert(compare_timestamps(first++, second++, units++, rate++, window++) == AZ_TIME_EQUAL);
    assert(first == 1 && second == 1 && units == 2 && rate == 2 && window == 2);
#ifdef __cplusplus
    __uint128_t wide = __uint128_t(UINT64_MAX) + 1;
    assert(compare_timestamps(wide, wide, 1, 1, 1) == AZ_TIME_EQUAL);
    assert(compare_timestamps(wide, __uint128_t(0), 1, UINT64_MAX, 1) == AZ_TIME_AFTER);
#endif
    az_command_completion completion;
    memset(&completion, 0, sizeof completion);
    completion.kind = AZ_COMMAND_COMPLETION_TERMINAL;
    az_command_completion_fn callback = NULL;
    (void)callback;
    az_library_v1 library;
    memset(&library, 0, sizeof library);
    library.abi = AZ_LIBRARY_ABI;
    library.size = offsetof(az_library_v1, command_submit_async);
    assert(!AZ_LIB_HAS(&library, command_submit_async));
    library.size = sizeof library;
    assert(library.size >= offsetof(az_library_v1, command_acknowledge) + sizeof library.command_acknowledge);
    assert(offsetof(az_library_v1, command_acknowledge) == offsetof(az_library_v1, command_submit_async) + sizeof library.command_submit_async);
    return 0;
}
