# SDK architecture

AZ-MODS is split into three layers:

- The public headers in `include/` define the ABI-v1 records, service tables and
  helper contracts available to mods.
- The core host owns authentication, bounded storage, native observation and
  command delivery. It publishes copied values rather than native pointers.
- The examples and test harnesses exercise the public tables through the same
  size/version checks used by a real mod.

Track and waveform data use an owner-scoped ticket lifetime. A ticket pins its
immutable snapshot until release, while the event journal reports publication
and invalidation independently. Native commands use a copied request, a
globally unique dispatch token and asynchronous `TERMINAL`/`RELEASED`
notifications. The legacy query and release entries remain for ABI
compatibility, but the asynchronous path does not depend on them.

Admission is bounded by 32 delivery reservations globally and four per owner. After a
terminal callback returns and acknowledgement is accepted, the primary command record may
be reclaimed while a pending-delivery identity retains the callback, context, immutable
result and retry identity until `RELEASED` returns. Retirement clears those identities only
after callback quiescence.

See the [shared library service](13-track-data.md), the [API reference](API.md)
and the [support matrix](14-track-data-support.md) for the public contracts.
