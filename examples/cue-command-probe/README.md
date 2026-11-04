# Native cue command emulator probe

This opt-in test mod uses the public async SDK to exercise commands 1–26. The emulator runner loads a real USB track and verifies command receipts against native cue storage and source-position observations. Its mutations stay in the disposable emulator USB overlay.

The worker waits for the first public SDK UI tick before reading request files.
File reads and result writes stay on the worker. Capability reads, target validation,
submissions, cancellation and acknowledgement run on UI. Completion callbacks enqueue
bounded copied log records without file I/O.

Valid requests renew a 30-minute idle lease. The worker has a fixed two-hour lifetime, an 8192-record budget and a 16 MiB output cap. At expiry it allows up to 30 seconds for an existing request before ending logging.
Accepted completion/cleanup callbacks remain active with process-lifetime context.
This interval never changes the request's original action deadline.

Requests are ASCII, at most 1023 bytes, newline terminated, and atomically renamed to `data/request`. The fields are ordered:

```
v=1 pid=123 action=submit id=1 deck=0 op=21 load=1 revision=0 timeout_ns=10000000000 flags=3 classification=2 entry_id=0 position=0 end=0 beats=0 rgb=0 comment_hex=-
```

`state` reads deck/load state without cue entries; `snapshot` also reads native cue storage. Both use zero request arguments. Comments use lowercase UTF-8 hex, or `-` for empty. Native Hot IDs are 1–8. Selected memory commands require the exact ordinal and revision from a native snapshot.

While a command is pending, the worker forwards snapshot and `cancel` requests to UI. A cancel request supplies the active `id` and `deck`, with other arguments zero. New submissions receive `AZ_BUSY` until the current result has been released.

The probe samples the monotonic clock on UI immediately before `command_submit_async`,
adds `timeout_ns`, and logs `submitted_ns` and `deadline_ns`. A validated `TERMINAL`
callback is copied and acknowledged once. Ownership remains until matching `RELEASED`;
no command polling, legacy release or cleanup retry is used.

`post_ui` initiation is best effort. Its void return can hide a dropped job and may
produce no visible result. Posting consumes no accepted command ID and establishes
no callback wait or cleanup ownership. Only a delivered UI job's actual submission
result can do that. Scheduling and callback contexts remain valid for process
lifetime, including failed initialization or suppressed retirement notifications.
This milestone provides no reliable worker handoff API.

Host tests run through `tests/sdk-cue-emulator.sh` in the core checkout, with `--sdk-root` pointing to this SDK checkout. Full firmware testing requires the explicitly owned emulator runner and normal mod Allow/Reload.
