/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>
int main(void)
{
    const az_observation_v1 *library = ht_observation_service(AZ_OBSERVATION_ABI);
    assert(AZ_OBSERVATION_HAS(library, native_cues));
    az_native_cue_snapshot out = { .size = sizeof out, .version = AZ_DATA_VERSION, .revision = 123 };
    for (int deck = 0; deck < 4; ++deck) {
        assert(library->native_cues(deck, &out) == AZ_UNSUPPORTED);
        assert(out.size == sizeof out && out.revision == 123);
    }
    out.size = AZ_NATIVE_CUE_SNAPSHOT_V1_SIZE;
    assert(library->native_cues(0, &out) == AZ_UNSUPPORTED);
    assert(out.size == AZ_NATIVE_CUE_SNAPSHOT_V1_SIZE && out.revision == 123);
    assert(library->native_cues(-1, &out) == AZ_INVALID);
    assert(library->native_cues(4, &out) == AZ_INVALID);
    assert(library->native_cues(0, NULL) == AZ_INVALID);
    out.size = 0;
    assert(library->native_cues(0, &out) == AZ_INVALID);
    az_command_target_v1 target = { .size = sizeof target, .version = 99 };
    assert(AZ_OBSERVATION_HAS(library, command_target));
    assert(library->command_target(0, 1, &target) == AZ_UNSUPPORTED && target.version == 99);
    assert(library->command_target(-1, 1, &target) == AZ_INVALID);
    assert(library->command_target(4, 1, &target) == AZ_INVALID);
    assert(library->command_target(0, 0, &target) == AZ_INVALID);
    assert(library->command_target(0, 1, NULL) == AZ_INVALID);
    target.size = 0;
    assert(library->command_target(0, 1, &target) == AZ_INVALID);
    puts("Host native cue service reports unsupported without inventing native data");
}
