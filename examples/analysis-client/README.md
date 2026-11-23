# Core Analysis Client

This example requests phrase and vocal computation for the first qualified loaded
deck it observes. Core performs the computation, schedules workers, validates the
source and publishes the result. The client carries no decoder, worker or model.

It reads request status from `tick` only to display the outcome and collect a
copied result. Polling does not advance the core computation or linking. Closing
the mod cancels its request; owner retirement also releases abandoned work.

The request uses sequential 1/1 execution. Change the explicit request settings
to any supported sequential or concurrent configuration; unsupported choices
are rejected. See [the service contract](../../docs/18-track-analysis.md).

Build with `../../tools/azmod-pack build .`. The core must expose the analysis and
observation services and have its private vocal model provisioned on the mods USB.
