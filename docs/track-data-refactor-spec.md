# Track data refactor proposal

This page records the public shape of the track-data and asynchronous command
refactor. The implementation must preserve ABI-v1 offsets, size/version guards,
owner authentication and ticket lifetimes while adding callback-based command
completion.

The public command sequence is:

`command_submit_async → TERMINAL callback → command_acknowledge → RELEASED callback`

Native completion and cleanup are autonomous. They must not depend on mod ticks,
painting, capability refreshes or command-status polling. Older service tables
remain loadable; clients check `AZ_LIB_HAS` before using the appended async
members and treat an unavailable delivery binding as unsupported.

The working specification and acceptance checklist are maintained outside the
published repositories in `temp/specs/track-data-refactor-spec.md`. The
[shared library service guide](13-track-data.md) is the normative public usage
documentation.
