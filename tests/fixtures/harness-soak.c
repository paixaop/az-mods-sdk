/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>

/* Run the production child body directly so compiler coverage is flushed on
 * normal process exit. The CLI's crash isolation remains tested by sanitize.sh. */
int main(int argc, char **argv)
{
    assert(argc == 3);
    mod_path = argv[1];
    mod_id = argv[2];
    return run(1, 0);
}
