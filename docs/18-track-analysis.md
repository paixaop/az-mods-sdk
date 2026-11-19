# Core track analysis

Core owns audio decoding, phrase classification, vocal inference, models,
background scheduling and publication. A community mod requests computation
through `core->analysis_service(AZ_ANALYSIS_ABI)`. It does not carry an analysis
engine or launch a worker. Check `AZ_HAS(core, analysis_service)` and
`AZ_ANALYSIS_HAS(service, member)` before using each operation. An unsupported
service or capability is an explicit failure; do not retry through an older API.

The built-in Track Analysis setting controls automatic loaded-track processing.
An explicit SDK request has its own lifetime and is unaffected by that toggle.
Both paths share one active computation and at most three analysis workers on
A53 CPUs 1–3 at nice 19. Scheduling-policy failure prevents computation. Each
computation has a ten-minute deadline and a shared 512 MiB scratch budget;
codec state and returned arrays are additional bounded allocations.

## Request and observe

Initialize `az_analysis_request_v1.size` and `version`. Select a qualified USB
library reference with `AZ_ANALYSIS_TRACK`, or a logical deck and accepted
native load epoch with `AZ_ANALYSIS_DECK`. Select `AZ_DATA_PHRASES`,
`AZ_ANALYSIS_VOCALS`, or both. Existing populated components and unrelated
stored data remain intact. Loaded phrase analysis requires the existing native
grid. Vocal-only work does not require a grid. The service does not invent a
standalone beat-grid detector.

Set sequential or concurrent stage mode and phrase/vocal thread counts from
one to three. Sequential stages accept all nine combinations; concurrent
combined work accepts 1/1, 1/2 and 2/1. Invalid or oversubscribed choices fail
before decoding. These explicit request settings use the same execution engine
as the built-in feature's `TRACK_ANALYSIS_*` environment controls.

`request` returns a retained request ID on `AZ_OK`. This acknowledges admission,
not successful analysis. Eight SDK requests may be retained globally, at most
four per owner. `status` returns queued, running, saved, complete, failed or
cancelled state, the qualified source, completed components, and an explicit
result and diagnostic. Saved components may be waiting for the native library
transaction's playback gate. Only the final outcome establishes successful
linking. Transaction failure can leave durable reusable components; a later
request can reuse them. There is no crash-recovery journal or atomic multi-file
publication guarantee.

Progress does not depend on status polling, mod ticks, painting or callbacks.
`cancel` revokes pending/running work; `release` also discards the retained
request. Both may return `AZ_BUSY` under bounded contention. A publication that
already passed its commit point cannot be rolled back by cancellation.

## Copied results and vocals

For a completed computation, `result` copies typed phrase and vocal arrays into
caller buffers. Initialize the result's `size`. A NULL/zero-capacity sizing read
returns `AZ_LIMIT` with counts and NULL array pointers. Provide enough capacity
for both arrays to obtain `AZ_OK`; there is no partial array copy. `AZ_PENDING`
means computation/publication is still pending. An already-populated source can
complete without producing new arrays; use the stored track-data and interval
readers for that source.

Copied bytes belong to the caller and remain valid after release or owner
retirement. Phrase entries use one-based native grid ordinals, not timestamps.
Source rate and frame count preserve the actual 44.1 or 48 kHz clock. Vocal
frames use a 22050 Hz analysis clock and 1024-sample hop with `AZ_VOCAL_LSTM`
provenance. Activity ranges from 0 to 4; values at least 3 are vocal. All-zero
activity is valid completed instrumental analysis.

`vocal_intervals` and `vocal_intervals_loaded` return copied half-open original
track-time intervals. They also read existing PVDI data and matching spectral or
LSTM provenance. NULL/zero-capacity sizing reads return `AZ_LIMIT`; a valid
instrumental timeline returns `AZ_OK` with zero intervals. Unknown formats,
malformed data, stale sources and unsupported firmware remain explicit errors.
Use `compare_timestamps()` with the source's actual clock for comparisons.

## Ownership and failure

Requests retain copied input records only. No mod pointer or callback remains
borrowed asynchronously. Failed initialization and live disable retire the
owner lifetime: new requests and late publication are refused, pending work is
cancelled, and active decoder children are joined before their owned resources
are reclaimed. Re-enable creates a distinct lifetime; old request IDs cannot
become another owner's work. One owner's retirement does not stop built-in work
or another owner's request.

The SDK exposes no audio locator lease, raw publication packet, model file or
native pointer. Source qualification, stable decoder inputs, native maps,
persistence and resource cleanup remain private core responsibilities. Public
clients must handle unavailable models/services, stale loads, failed I/O and
permanent errors directly.

## Raw phrase decoding

`azmod-phrases.h` provides `az_phrase_decode(raw_mood, raw_kind)` for the shared
PSSI meanings of known moods. `AZ_PHRASE_UNCLASSIFIED` identifies a pair without
a known category; the helper does not alter the raw fields or establish that an
analysis component is available. Selection, grouping and any policy for unknown
moods belong to the consuming feature. In particular, Anchor retains its
kind-based unknown-mood policy, while Cues keeps unclassified phrases eligible
for its Any selector.
