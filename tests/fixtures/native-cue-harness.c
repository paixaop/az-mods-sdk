/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>
int main(void)
{
    const az_library_v1 *library = ht_library_service(AZ_LIBRARY_ABI);
    assert(AZ_LIB_HAS(library, native_cues));
    az_native_cue_snapshot out = {.size = sizeof out, .version = AZ_DATA_VERSION, .revision = 123};
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
    puts("Host native cue service reports unsupported without inventing native data");
}
