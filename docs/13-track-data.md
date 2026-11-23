# Track data and native commands

The mod ABI is 2. Rebuild ABI 1 packages for this header. Discover each service
with `AZ_HAS` and use the member guard before calling it.

| Getter | Member guard | Responsibility |
| --- | --- | --- |
| `library_service(AZ_LIBRARY_ABI)` | `AZ_LIB_HAS` | USB library enumeration, stored data and waveform tickets, events and timing |
| `observation_service(AZ_OBSERVATION_ABI)` | `AZ_OBSERVATION_HAS` | Copied deck state, cue snapshots, coherent command targets and status subscriptions |
| `command_service(AZ_COMMANDS_ABI)` | `AZ_COMMANDS_HAS` | Commands, cancellation, completion and result ownership |
| `analysis_service(AZ_ANALYSIS_ABI)` | `AZ_ANALYSIS_HAS` | [Phrase and vocal analysis](18-track-analysis.md) |

Service tables are core-owned for the process lifetime. Initialize records
with the required size and version. Check operation capabilities and field
validity for each source and load. See the [support matrix](14-track-data-support.md).

## Identity and stored data

Use `track_resolve(id, stick, content_id, &ref)` on a mod worker with a content
ID from the same USB library. Stick 0 is USB 1 and stick 1 is USB 2. The source
and connection generation distinguish the same content ID across ports and
reconnects. Keep the complete reference and resolve it again after invalidation
or a process restart.

`track_request` queues a component mask without file I/O. `track_acquire` returns
a borrowed immutable snapshot when ready. The pointer and nested arrays remain
valid until `track_release`, even after cancellation or invalidation. A new
acquisition on an invalidated ticket returns `AZ_STALE`. Release cancelled tickets
and any acquired tickets before returning an initialization failure.

Stored data describes the saved edition. The provider checks database, source
and analysis-file identity before publication and during freshness sweeps.
Use [loaded deck status](17-deck-status.md) for current native observations.

## Availability, provenance and timing

An absent tag is unsupported, an explicit zero-entry tag is empty, a pending ticket is
loading, and malformed/read failures return an error. A failed result exposes no partial
snapshot. Every parsed component has a revision and observation time. These describe
publication of saved data, not the exact instant a native user edited a track.

PCO2 lists take precedence over PCOB independently for hot and memory cues, including an
explicit empty extended list. Legacy hot PCOB lists merge across DAT and EXT by slot;
legacy memory lists use DAT when that classification exists, otherwise EXT. Memory ordering
is saved-file ordering, not a verified native UI ordering. Time zero is valid. Cue point versus loop comes from the entry
type. A saved loop does not imply engagement. Color and comment have separate validity bits.
`AZ_CUE_COLOR` describes optional hot-cue RGB; `AZ_CUE_COLOR_ID` describes the raw saved
memory palette ID, including zero, in `color_id`. Its union alias `reserved` preserves the
entry stride. Memory trailer bytes are not RGB, and no native palette is inferred.
PCPT loop statuses 0/1 expose saved `auto_active = 0`, and status 4 exposes 1, under
`AZ_CUE_AUTO_ACTIVE`. PCP2 can receive this property only from one exact, unambiguous legacy
loop counterpart matching classification, type, start/end and hot slot when applicable.
Unknown statuses, duplicates and mismatches leave activation unavailable. Native automatic
activation behavior remains unverified. Hot entry identity is its slot (1 through 8); memory entry
identity is scoped to that cue revision and may change after an edit or reload. Main cue,
in-memory modifications and associations with the live loop are unavailable.

PQTZ preserves every beat's original time, tempo and beat-in-bar value. No downbeat is
invented for a zero value. PSSI versions 0/1 preserve raw kind and mood; normalized category
remains unknown. Loaded native phrase maps use the same PSSI mood domain: 1 (high),
2 (mid), 3 (low), or 0 when unknown. The core converts the firmware's internal
zero-based mood enum before exposing `raw_mood`, so file and loaded-track selectors
use identical values. Version and stride are separate 16-bit fields. An unknown version marks
phrases unsupported while other valid requested components remain usable; malformed lengths
still fail the read. Beat positions are zero-based fractional indexes. Timing helpers interpolate
between adjacent grid times and reject extrapolation. They therefore handle changing tempo
and off-grid interval boundaries. Source seconds and beat length are separate from estimated
playback seconds at the caller's explicit positive rate. A zero rate leaves playback duration
unavailable. Bar length remains unavailable because no reliable meter model is present.

PWAV returns its saved overview height bytes, not RGB or PCM. The overview covers the track
and its approximate seconds-per-sample uses the library duration divided by sample count.
The database duration is rounded to seconds; this is not precise waveform timing. Without a
usable duration the overview waveform is unavailable. Raw mono/RGB/three-band detail and
all previews use the separate bounded-window service below.

## Detailed waveform windows

`waveform_request(id, track, query, &ticket)` and `waveform_acquire(id, ticket, &view)`
provide canonical detail/preview encodings and a derived layered format from local USB analysis. Check both entries
with `AZ_LIB_HAS`. The query names `AZ_WAVE_PWV3_MONO` (1), `AZ_WAVE_PWV5_RGB16` (2),
`AZ_WAVE_PWV7_BANDS` (3), `AZ_WAVE_PWV4_PREVIEW` (4), `AZ_WAVE_PWV6_PREVIEW` (5),
`AZ_WAVE_PWV2_TINY_PREVIEW` (6), `AZ_WAVE_PWAV_PREVIEW` (7) or `AZ_WAVE_PWV7_LAYERED_RAW` (8), a zero-based `first_column` and a positive
`column_count` of at most `AZ_WAVE_WINDOW_MAX` (4096). The last window is clipped to the
stored extent. For nonempty data a first column at or beyond the end is invalid; a valid
zero-column tag accepts only first zero and publishes `AZ_AVAIL_EMPTY`.

PWV3 uses one byte per column: height in bits0–4 and intensity in bits5–7. PWV5 uses
a big-endian16-bit word with three-bit red/green/blue fields starting at bits13/10/7 and
five-bit height at bits2–6; the low two bits are preserved without interpretation. PWV7
uses three uncalibrated bytes per column, ordered low/mid/high bands. `bytes_per_column`
is1/2/3 respectively; `data_bytes` is the returned column count times that stride. The source axis is
150 columns per second from origin zero: `start_s = first_column / 150.0`,
`seconds_per_column = 1 / 150.0`. This fixed analysis clock is independent of integer-second
library duration and pitched playback. It does not promise exact decoder, DAC or UI alignment.
These are lossless stored encodings. Native display gains, gamma, exact frequency cutoffs
and faithful player rendering remain unavailable. Unfamiliar variants return unsupported.

`AZ_WAVE_PWV7_LAYERED_RAW` derives six-byte columns from the canonical PWV7 data.
Bytes 0–1 contain a little-endian header: bits 12–15 hold the layer count (1–3);
the three low nibbles hold successive symbolic layer IDs. Bytes 2–4 hold the
corresponding absolute raw magnitudes in descending order, with unused ID and
magnitude fields zero. IDs 1/2/3/4/5/6/8 mean C/B/A/BC/AC/AB/ABC, where A/B/C
label PWV7 input bytes 0/1/2. Allzero input produces one ID 8 layer with magnitude 0.
Byte 5 is SDK-reserved zero; it does not expose the undefined padding in native
allocations. This format uses the same 150-column/s axis and
`info.provenance == AZ_PROV_DERIVED`. It supplies neither RGB values nor gain,
gamma, frequency cutoffs or player pixels. Raw encoding 3 remains available.
An older core can reject encoding 8 with `AZ_UNSUPPORTED`; check that result
before choosing a supported fallback.

PWV4 previews preserve six bytes per column; PWV6 previews preserve three. PWV2 tiny previews
and PWAV previews preserve one raw byte per column. The latter two use a 20-byte header
with count at +12 and the recognized opaque word `0x00010000` at +16; that word is not
interpreted as gain or a sample rate. These previews cover the
whole track and use the approximate axis `duration_s / total_columns`, derived from valid
positive finite library duration. They never inherit the detail clock of 150 Hz. Without
usable duration the preview request is unsupported. This time axis is rounded with the
database's duration, and these raw preview encodings have no native gain/pixel calibration.
Zero-entry tags publish empty when duration is usable. Query size/version/reserved/count
validation happens before queuing: zero count is invalid, count above 4096 is limited and
unknown encoding IDs are unsupported. A nonempty out-of-range first column fails the read.

PWAV/PWV2 are selected by tag in DAT, PWV3/PWV4/PWV5 in the sibling EXT container, and PWV6/PWV7 in 2EX. The derived layered format also reads PWV7 from 2EX; a file name
alone never selects its wire representation. The normal-priority provider streams the bounded PMAI/tag headers and reads only the selected
sample span. It validates later tag envelopes and rejects duplicate requested tags. An unfamiliar
canonical-header word or stride is unsupported; malformed lengths fail. Database, source and
analysis-file fingerprints are checked around publication and during freshness sweeps.

Windows share the eight cache slots and four tickets per mod with ordinary track snapshots.
Track and waveform ticket identifiers never wrap. Exhausting their identity counter
returns `AZ_LIMIT` without changing the output ticket or existing acquisitions.
Snapshot publication also reports `AZ_LIMIT` when no new revision identity can be
assigned; previously acquired snapshots stay valid until released.
The cache key includes encoding, first column and requested count. `track_release`,
`track_cancel` and `data_event` apply to both ticket kinds. Calling the wrong acquire function
returns `AZ_INVALID`. Arrays remain immutable and pinned until release even after invalidation.
Events identify the waveform component and ticket; asynchronous failures use the existing
status codes. No file work occurs in a draw, playback or native notification callback.

## Observations, events and commands

Use [status subscriptions](17-deck-status.md) for loaded deck changes and
`deck_snapshot` or `native_cues` for bounded copies. Unit identity, USB
source identity, snapshot revision and native load generation describe
separate lifetimes. A same-track reload produces a new load generation.

For snapshot reads, check both the outer live-field bit and nested validity.
For example, a loop can report engagement before `AZ_LOOP_BOUNDS` is valid.
Master Tempo and Sync enabled are independent settings. Raw play flags carry
native notification values. Use the status subscription for normalized transport
and check the freshness of each copied receipt before acting on it.

For a command based on a loaded status event, pass its native load epoch to
`command_target` on the observation table. The copied target qualifies the
current load, cue revision, source clock and command capabilities together.
A changed target returns `AZ_STALE` and a missing observation returns
`AZ_PENDING`. The command service verifies target identity during execution.

Saved writes require `AZ_COMMAND_PERSIST`. Commands that select native cue IDs
require `AZ_COMMAND_NATIVE_CUE_IDS` and the exact `native_cues` revision.

### Callback delivery

The optional `on_data_event` subscription delivers stored track and waveform events.
Register it once from initialization and check it with `AZ_LIB_HAS`. Registration returns
`AZ_INVALID` for an invalid owner, context or callback, `AZ_LIMIT` for a duplicate or
exhausted journal, and `AZ_BUSY` when the provider lock is contended. A refused registration
installs no listener. Its borrowed record is valid for the callback duration only.

Callbacks run on the UI thread outside provider locks. Failed initialization and owner
retirement suppress future invocations. Keep callback work bounded and nonblocking.

`on_data_event` receives the same owner-scoped records as `data_event`, using its own cursor
starting at registration. `AZ_CHANGE_TRACK_DATA` reports track **and** waveform readiness,
failure or cancellation; `ticket` correlates the request and `fields` identifies components.
`AZ_CHANGE_INVALIDATED` reports stale data. A cache hit also produces a completion event.
For success, call the matching acquire function to borrow the snapshot; the callback itself
does not acquire or release tickets. Events describe publication history: a ticket may have
been cancelled, released or invalidated before delivery, so check the acquire status.
Acquisition/release remain try-lock operations and can return `AZ_BUSY`; defer a contended
operation rather than blocking the UI. There is no need to repeatedly check `AZ_PENDING`
while waiting for completion.

Delivery is limited to 16 data records per mod per UI turn (about 30 turns/sec, rate varies).
Provider lock contention defers delivery to a later turn. The 64-record journal is shared:
`missed=1` means rebuild state for outstanding tickets, including when other owners overrun
history. A gap-only record has `kind=0`. Callback and polling cursors are independent;
calling `data_event` does not consume callback delivery. Sequence exhaustion still requires
handling `AZ_LIMIT` through the polling API; it cannot wrap into a fresh sequence.

### Asynchronous command ownership

New clients require both `command_submit_async` and `command_acknowledge` and
the operation capability. Check members with `AZ_COMMANDS_HAS`. A missing
member leaves the action unavailable. There is no
fallback to command-status polling, the command journal, or legacy release retries.
The older `command_submit`, `command_poll`, `command_cancel`, `command_release` and
`on_command_complete` slots remain for binary compatibility; they do not implement the
new acknowledgement and cleanup contract.

Submit directly from initialization or a UI callback. The callback must be non-null and belong to that mod's
executable code. Requests and synchronous outputs are copied; they need complete v1 capacity,
and a larger caller retains its size and unknown tail. Unknown flags, reserved fields,
non-finite coordinates and unterminated comments are invalid.

An `AZ_OK` return for a new ID establishes ownership of the **submitted** ID even when the
synchronous reply is malformed. Enter `WAIT_TERMINAL`. An immediate terminal synchronous
reply also waits for its deferred terminal callback. Never obtain a new ID from an invalid
reply. `AZ_INVALID` and `AZ_BUSY` preserve output and create no ownership. Other refusal
codes supply a correlated rejected result and create no callback or acknowledgement duty.

Each accepted asynchronous ID receives two serialized UI notifications outside service
calls and store locks:

| Notification | Client action |
| --- | --- |
| `TERMINAL` | Validate size/version/kind/settled and the complete nested result against the submitted ID. Copy its immutable outcome, reason, revision and producer observation time, then acknowledge once. |
| `RELEASED` | Require the saved terminal fields and `settled=1`. Cleanup has completed; return to `IDLE`. Context may be discarded after this invocation returns. |

The sequence is `IDLE -> WAIT_TERMINAL -> WAIT_RELEASED -> IDLE`. Applied, rejected and
cancelled terminal results require `settled=1`. Only uncertainty may carry `settled=0`;
acknowledge it and wait for native settlement and cleanup without checking status.
`APPLIED` also requires reason `AZ_OK`, a nonzero confirmation revision and a valid producer
monotonic observation time. Completion records and their nested results are borrowed.
Wrong IDs, malformed records, duplicates, out-of-order events and mismatched released
fields never authorize acknowledgement or reclamation.

Acknowledge from `TERMINAL` is allowed and cannot invoke `RELEASED` recursively. A valid
terminal acknowledgement returns `AZ_OK`, including repeated calls while the pending
delivery identity exists. There is no `AZ_BUSY`/`AZ_PENDING` retry cycle. Unexpected statuses
are protocol errors: retain context and surface the error. Capability loss, hidden pages,
missing mod ticks and delayed UI repaint cannot revoke accepted delivery obligations.
Native settlement and native slot reclamation proceed independently of acknowledgement.

Use static/process-lifetime context when retirement could suppress `RELEASED`. Retirement
closes admission, suppresses callbacks that have not started, and implicitly acknowledges
retained work. Its internal callback-quiescence barrier is not a public storage-reclamation
notification. Failed initialization, cancellation and return from `init` do not authorize
freeing unreleased callback context. Keep that context until process exit when released
delivery is suppressed.

Equal retries preserve the original callback/user binding and delivery phase. Compare all
defined request fields exactly, including deadline and finite coordinates; signed zeros are
equal. Ignore size, padding, unknown tail and bytes following the first comment NUL. Changing
any defined field, callback, user pointer or submission mode is `AZ_INVALID` with output
unchanged. Equal retained retries precede current endpoint/load/capability/clock checks and
cannot execute twice or replay a delivered callback. After acknowledgement, remain in
`WAIT_RELEASED`, including after the command-store record is reclaimed. An ID becomes stale
only after released delivery returns. Legacy requests instead retain explicit release.

Worker `post_ui` is only a best-effort request to initiate later. Its void return cannot
establish admission, refusal, accepted ID ownership or a future command callback. The queue
can drop a job and report no application-visible result. Keep an unconfirmed scheduling
context valid for process lifetime. A delivered UI job validates the current target and
publishes the actual async call result using thread-safe mod state. Submit from initialization or UI input when
immediate confirmation of admission is needed.

The [inspector Control page](../examples/library-inspector/control.c) demonstrates validation,
refusal handling, malformed successful replies, one acknowledgement and released cleanup.

### Create cues at explicit source positions

`az_native_cue_snapshot` appends an optional `source_rate_hz` clock. It is the
owned native PlayerState source-position clock used by these creation
operations. Its Hertz value is read independently of `POSITION.head.rate_hz`
and the media file's metadata rate; equality is never assumed. Require a nonzero clock on
the matching native load before planning or comparing newly created cue times;
zero means unknown or an older provider. Do not substitute a fixed rate or the
position clock. Saved entries are exposed in seconds, while the core retains
and validates their native encoded sample clocks internally.

The original v1 record prefix remains accepted as
`AZ_NATIVE_CUE_SNAPSHOT_V1_SIZE`. Readers preserve caller size, copy at most the
supplied capacity and leave unknown caller tails untouched. Initialize the full
record and optional fields to zero before requesting it from an older core.

`AZ_CMD_CUE_POINT_SET` (27) and `AZ_CMD_CUE_LOOP_SET` (28) create saved Hot or
Memory cues without seeking or changing the working loop or retained main cue.
Check their command capability bits before exposing automatic placement.

Use `AZ_COMMAND_PERSIST | AZ_COMMAND_NATIVE_LOAD | AZ_COMMAND_NATIVE_CUE_IDS`,
`AZ_QUANTIZE_NATIVE`, the current native load generation and the exact revision
from `native_cues`. For Hot cues select `AZ_CUE_HOT` and `entry_id` 1–8; for Memory
select `AZ_CUE_MEMORY` and `entry_id` 0. `position_s` is zero-based original-track
seconds and can be zero. Loop creation also takes `end_s` after IN. Point creation
leaves `end_s` zero. Resolve bar/beat quantization before submission. Color and a
UTF-8 comment are saved with the cue; Memory colors use the native palette.
Leave `beats` and `reserved` zero.
Creation selects the nearest frame in the actual source-position clock, rounding
half-frame ties forward. The native microsecond cache preserves that source frame.

Creation refuses occupied Hot pads, duplicate Memory positions and more than
ten Memory entries. A revision/load change refuses before dispatch. After native
dispatch, failure can be `UNCERTAIN`: writes are not rolled back. Hot completion
also waits for the native playable slot to hold the saved point/loop, with main
cue, working loop and transport preserved. Refresh `native_cues` after each
terminal callback before submitting another revision-fenced write.

For a complete cue plan, check `AZ_COMMANDS_HAS(commands, cue_batch_submit_async)`.
Submit an `az_cue_batch_request` with the native load and revision and up to
eight Hot and ten Memory definitions. Use `AZ_CUE_BATCH_IF_EMPTY` for
automatic placement or `AZ_CUE_BATCH_REPLACE` after confirming replacement.
The core copies the request and holds cue-driven waveform updates until the
native writes and Hot slot updates settle. Acknowledge the terminal result and
retain callback context through `RELEASED` as for other async commands.
An unsupported provider returns `AZ_UNSUPPORTED` before accepting the ID.

`HOT_CLEAR` additionally accepts the native-cue-ID flag and exact revision for
an explicitly approved replacement workflow. Its original unfenced calling form
remains supported. Application-level overwrite approval and automatic-placement
policy belong to the mod; the SDK does not silently replace occupied pads.

### Transient loop commands

Use `AZ_COMMAND_NATIVE_LOAD`, a valid native load generation, zero
`expected_revision` and `AZ_QUANTIZE_NATIVE`. Leave unused arguments and
selectors zero or empty. Boundary adjustments require an engaged loop.
IN and MOVE can also update the selected retained cue. Dispatch may
reposition playback.

| Operation | Arguments and behavior |
| --- | --- |
| Loop IN | All numeric arguments zero. Capture current playhead into native default cue/loop IN; use device-configured quantization. Arbitrary absolute IN is unsupported. |
| Loop OUT | All numeric arguments zero. Capture current playhead and enter native loop; requires inactive working loop and valid retained IN. Arbitrary absolute OUT is unsupported. |
| Loop ENTER | All numeric arguments zero. Native RELOOP alias, including seek to retained IN; already-engaged requests finish uncertain. |
| Loop EXIT | All numeric arguments zero; native exit, with inactive postcondition. |
| RELOOP | All numeric arguments zero; native reloop, requiring inactive-to-engaged transition for confirmation. |
| Clear retained points | All numeric arguments zero. Clear native kind0 retained cue, including the temporary main cue; this does not directly clear the separate working loop. |
| Double / halve | All numeric arguments zero. Scale existing engaged working loop; confirmation checks unchanged IN and the scaled beat ratio (beat mode) or sample length (time mode). |
| Beat-based length | Positive `beats` exactly representable as a nonzero unsigned16 numerator/denominator rational; other numeric arguments zero. Change existing engaged loop, preserving IN. |
| Move loop | Signed nonzero `position_s` delta in source seconds; other numeric arguments zero. Two native boundary adjustments, outward boundary first. Non-atomic; partial application is uncertain, with no rollback. |
| Adjust IN | Signed nonzero `position_s` delta in source seconds; other numeric arguments zero. |
| Adjust OUT | Signed nonzero `end_s` delta in source seconds; other numeric arguments zero. |

## Timing precision and invalid clocks

Use `compare_timestamps()` from `azmod-timestamps.h` for equality, ordering,
ranges and deadlines. Handle `AZ_TIME_INVALID` explicitly. Retried command
payloads require exact normalized field equality.

The function returns `AZ_TIME_BEFORE`, `AZ_TIME_EQUAL`, `AZ_TIME_AFTER`, or
`AZ_TIME_INVALID` for invalid parameters, non-finite floating timestamps, or a negative
integral operand when both operands are integral (including mixed signed/unsigned calls).
Each argument is evaluated once. The full unsigned 64-bit range is preserved. If either
operand is floating point, signed source coordinates remain valid. Zero is a valid
comparator input; receipt readers separately reject a missing monotonic timestamp. Equality means
`abs(second - first) < window_samples / sample_rate` seconds. A difference exactly at
the window boundary is ordered. Equivalence works across fixed bucket boundaries and
is symmetric, but is not transitive; do not use it as a sorting predicate.

Both timestamps must use the same clock and units. For source seconds, pass units `1`
and the actual source rate and native processing window. For source samples, pass the
actual rate for both units and sample rate. Monotonic nanoseconds use their own clock
resolution (units/rate `1000000000`, window `1`); convert clock domains explicitly.
Source, load, request and cue identity checks still apply independently.

Native persistence serializes cached microseconds and reconstructs source samples.
Compare saved cues against that native round trip before applying the sample window;
do not widen the window to hide a format conversion. A reconstructed loop can own
separate IN and OUT source objects with the same source identity and rate.

Grid interpolation preserves the stored millisecond time axis and fractional beats.
It rejects non-finite, window-equivalent or reversed selected grid endpoints without
modifying the output. Stored analysis, native samples and receipt clocks each retain
their own rate and window.

A positive source interval whose playback-duration calculation underflows to zero leaves
`AZ_INTERVAL_PLAYBACK` unavailable. Endpoints equivalent within the stored time window
produce a valid zero source/playback duration. Failed or invalid monotonic clock reads yield an unavailable timestamp
of zero instead of uninitialized or wrapped nanoseconds.
