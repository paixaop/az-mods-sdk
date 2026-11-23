/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AZMOD_PHRASES_H
#define AZMOD_PHRASES_H
#include <stdint.h>

/* Decodes only the shared, known PSSI mood/kind meanings. Unclassified is not
 * an operational error and does not replace a client's musical selection policy.
 * Raw fields remain unchanged in az_phrase. */
enum az_phrase_kind {
    AZ_PHRASE_UNCLASSIFIED = 0, AZ_PHRASE_INTRO, AZ_PHRASE_BUILD,
    AZ_PHRASE_BREAK, AZ_PHRASE_CHORUS, AZ_PHRASE_OUTRO,
    AZ_PHRASE_VERSE, AZ_PHRASE_BRIDGE
};

static inline enum az_phrase_kind az_phrase_decode(uint32_t mood, uint32_t kind)
{
    static const unsigned char mapping[3][11] = {
        { 0, 1, 2, 3, 0, 4, 5, 0, 0, 0, 0 },
        { 0, 1, 6, 6, 6, 6, 6, 6, 7, 4, 5 },
        { 0, 1, 6, 6, 6, 6, 6, 6, 7, 4, 5 }
    };
    if (mood < 1 || mood > 3 || kind >= 11) return AZ_PHRASE_UNCLASSIFIED;
    return (enum az_phrase_kind)mapping[mood - 1][kind];
}
#endif
