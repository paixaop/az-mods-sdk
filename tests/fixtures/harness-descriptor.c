/* SPDX-License-Identifier: GPL-3.0-only */
#define main harness_program_main
#include "../../tools/harness/harness.c"
#undef main
#include <assert.h>

int main(int argc, char **argv)
{
    assert(argc == 3);
    mod_id = "descriptor-test";
    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(handle);
    assert(!load_descriptor() && kfails[K_INIT] == 1);
    assert(!strcmp(kfirst[K_INIT], "no az_mod_v1_entry"));
    assert(!dlclose(handle));
    handle = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    assert(handle);
    void (*select_case)(int) = (void (*)(int))dlsym(handle, "select_descriptor_case");
    assert(select_case);
    for (int scenario = 0; scenario <= 10; ++scenario) {
        select_case(scenario);
        kfails[K_INIT] = 0;
        kfirst[K_INIT][0] = 0;
        int accepted = load_descriptor();
        assert(accepted == (scenario == 0 || scenario >= 7));
        assert(kfails[K_INIT] == (unsigned long)(scenario != 0));
        const char *expected = scenario <= 4 ? "bad descriptor" : scenario <= 6 ? "descriptor id is not descriptor-test"
                                                                                : "fw_tags is not { \"any\", NULL }";
        if (scenario) assert(!strcmp(kfirst[K_INIT], expected));
        else assert(mod && mod_base && mod_descriptor_valid(mod) && mod_tags_valid(mod));
    }
    assert(!dlclose(handle));
    puts("PASS descriptor loader: missing entry, ABI/size/init/id rejection and firmware tag diagnostics");
    return 0;
}
