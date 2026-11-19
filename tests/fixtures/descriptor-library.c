/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
#include <stddef.h>

#ifdef NO_ENTRY
int descriptor_placeholder(void) { return 0; }
#else
static int scenario;
void select_descriptor_case(int value) { scenario = value; }
static int init(const az_core_v1 *core)
{
    (void)core;
    return 0;
}
const az_mod_v1 *az_mod_v1_entry(void)
{
    static az_mod_v1 value;
    static const char *const any[] = { "any", NULL };
    static const char *const empty[] = { NULL };
    static const char *const firmware[] = { "ep147-test", NULL };
    static const char *const extra[] = { "any", "ep147-test", NULL };
    value = (az_mod_v1){ .abi = AZ_MOD_ABI, .size = sizeof value, .id = "descriptor-test", .fw_tags = any, .init = init };
    switch (scenario) {
    case 1: return NULL;
    case 2: ++value.abi; break;
    case 3: value.size = offsetof(az_mod_v1, on_frame); break;
    case 4: value.init = NULL; break;
    case 5: value.id = NULL; break;
    case 6: value.id = "wrong-id"; break;
    case 7: value.fw_tags = NULL; break;
    case 8: value.fw_tags = empty; break;
    case 9: value.fw_tags = firmware; break;
    case 10: value.fw_tags = extra; break;
    }
    return &value;
}
#endif
