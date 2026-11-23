/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
#include <assert.h>
#include <string.h>
static int request(const char *id, const az_analysis_request_v1 *r, uint64_t *out) {
    assert(!strcmp(id, "analysis-client"));
    if (r->version != AZ_ANALYSIS_REQUEST_VERSION) return AZ_UNSUPPORTED;
    *out = 42;
    return AZ_OK;
}
int main(void) {
    az_analysis_v1 table = {.abi = AZ_ANALYSIS_ABI, .size = sizeof table, .request = request};
    const az_analysis_v1 *service = &table;
    assert(AZ_ANALYSIS_HAS(service, request));
    assert(!AZ_ANALYSIS_HAS(service, result));
    table.abi++;
    assert(!AZ_ANALYSIS_HAS(service, request));
    table.abi = AZ_ANALYSIS_ABI;
    table.size = offsetof(az_analysis_v1, request);
    assert(!AZ_ANALYSIS_HAS(service, request));
    table.size = sizeof table;
    az_analysis_request_v1 r = {.size = sizeof r, .version = AZ_ANALYSIS_REQUEST_VERSION};
    uint64_t id = 0;
    assert(service->request("analysis-client", &r, &id) == AZ_OK && id == 42);
    r.version++;
    assert(service->request("analysis-client", &r, &id) == AZ_UNSUPPORTED);
    return 0;
}
