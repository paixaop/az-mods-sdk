/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-phrases.h"
#include <assert.h>
#include <limits.h>
int main(void)
{
    static const unsigned char high[] = { 0, 1, 2, 3, 0, 4, 5, 0, 0, 0, 0 };
    static const unsigned char other[] = { 0, 1, 6, 6, 6, 6, 6, 6, 7, 4, 5 };
    for (unsigned kind = 0; kind < 11; ++kind) {
        assert(az_phrase_decode(1, kind) == high[kind]);
        assert(az_phrase_decode(2, kind) == other[kind]);
        assert(az_phrase_decode(3, kind) == other[kind]);
        assert(az_phrase_decode(0, kind) == AZ_PHRASE_UNCLASSIFIED);
        assert(az_phrase_decode(4, kind) == AZ_PHRASE_UNCLASSIFIED);
    }
    assert(az_phrase_decode(1, 11) == AZ_PHRASE_UNCLASSIFIED);
    assert(az_phrase_decode(2, UINT_MAX) == AZ_PHRASE_UNCLASSIFIED);
    assert(az_phrase_decode(UINT_MAX, 5) == AZ_PHRASE_UNCLASSIFIED);
    return 0;
}
