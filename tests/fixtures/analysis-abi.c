/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>

/* Frozen ABI v1 positions before raw publication was retired. The two u32
 * header words precede 37 function slots; removing slot 27 would move every
 * later call made by an already compiled mod. */
#define LEGACY_SLOT(index) (8 + (index) * sizeof(void (*)(void)))
_Static_assert(offsetof(az_library_v1, analysis_open) == LEGACY_SLOT(26), "analysis open moved");
_Static_assert(offsetof(az_library_v1, reserved_analysis_commit) == LEGACY_SLOT(27), "raw slot moved");
_Static_assert(offsetof(az_library_v1, analysis_close) == LEGACY_SLOT(28), "analysis close moved");
_Static_assert(offsetof(az_library_v1, analysis_grid) == LEGACY_SLOT(35), "typed grid moved");
_Static_assert(offsetof(az_library_v1, analysis_commit_result) == LEGACY_SLOT(36), "typed commit moved");
_Static_assert(sizeof(az_library_v1) == LEGACY_SLOT(37), "library size changed");

static int close_job(const char *id, uint64_t job) {
    assert(!strcmp(id, "layout-test") && job == 42);
    return AZ_OK;
}

int main(void) {
    const az_library_v1 library = {.abi = AZ_LIBRARY_ABI, .size = sizeof library,
                                  .analysis_close = close_job};
    const az_library_v1 *service = &library;
    int (*legacy_commit)(const char *, uint64_t, const void *, uint32_t);
    int (*legacy_close)(const char *, uint64_t);
    memcpy(&legacy_commit, (const unsigned char *)&library + LEGACY_SLOT(27), sizeof legacy_commit);
    memcpy(&legacy_close, (const unsigned char *)&library + LEGACY_SLOT(28), sizeof legacy_close);
    assert(!legacy_commit);
    assert(legacy_close("layout-test", 42) == AZ_OK);
    assert(AZ_LIB_HAS(service, analysis_close));
    return 0;
}
