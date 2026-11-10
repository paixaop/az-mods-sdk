# Background track analysis

The append-only `az_library_v1` tail provides `analysis_open`, `analysis_commit` and
`analysis_close`. Check **all three** with `AZ_LIB_HAS`. These calls belong on a mod
worker, perform I/O, and refuse UI, frame and audio contexts. They are independent
of the legacy track-data development flag. They do not install firmware hooks.

`analysis_open(id, &track, &job)` accepts a source-qualified USB OneLibrary reference
from a loaded status view or `track_resolve`. Initialize `job.size`. On `AZ_OK`,
`job.needed` names GRID and/or PHRASES, and the record supplies read-only audio and DAT
locators plus the library's original global BPM. The opaque job belongs to that mod.
At most four jobs per mod and eight globally are retained. `AZ_NOT_FOUND` means the
saved container already has phrases; `AZ_UNSUPPORTED` means the source has no supported
audio source. Unknown references and source changes fail closed.

Rows without an ANLZ locator receive a stable path under `PIONEER/USBANLZ/AZMODS`.
`AZ_ANALYSIS_NEEDS_LINK` in `job.flags` means commit queues the new locator through
the native library transaction spool. The mod calls `lib_spool_kick` on its worker
and retries until admitted; the existing playback gate controls application.
An already populated generated location returns `AZ_NOT_FOUND` with `job.job=0`
and this flag, repairing a previously interrupted link without recomputing phrases.
The SQL update changes only an empty locator on the matching content ID and audio
path; it preserves a later DJ edit. Text is encoded as hex literals for the spool.

The mod can read the locators and launch its own analysis helper. It must not write
the ANLZ paths itself. `analysis_commit` accepts a bounded byte buffer:

| Offset | Value |
| --- | --- |
| 0 | Four bytes `TA01` |
| 4 | Big-endian u32 length of PQTZ, or zero to retain the existing grid |
| 8 | Big-endian u32 length of PSSI |
| 12 | Big-endian u32 actual analysis PCM sample rate: 44100 or 48000 |
| 16 | Optional complete PQTZ tag, then complete PSSI tag |

PQTZ uses its 24-byte header and eight-byte rows, limited to 32768 beats. PSSI uses
version zero, a 32-byte header and 24-byte rows, limited to 1024 phrases. The core checks
framing, timestamps, beat positions/tempos, mood, ordered phrase boundaries, counts and
the terminal beat against the saved/generated grid. Future layouts are refused.

Publication rechecks the physical USB source, library incarnation and DB/audio/ANLZ
fingerprints. It preserves unrelated container bytes, refuses populated maps and binds
temporary files and renames to directory descriptors under the USB. Successful commit
consumes the job. Otherwise close it, retrying `AZ_BUSY`. Fingerprints are best-effort
freshness witnesses, rather than transactional exclusion against every native editor.

DAT and EXT are individually synced and renamed; they are not an atomic pair. DAT is
published first so no saved phrase references a missing new grid. Failure after DAT
can leave a usable grid without phrases. Close the old lease and open another job.

This service does not provide streamed audio or publish active firmware map objects. File persistence
alone does not promise an immediate native display update. Those require separate core
capabilities and runtime qualification before a mod can claim them.
