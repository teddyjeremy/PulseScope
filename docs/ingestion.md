# Trace ingestion

## JSON fixtures (`pulsescope.session.v1`)

Not an ETL file. Required fields: `schema`, `synthetic` (true or false), `events`. Each event needs `kind` and either `us` or `t100ns`.

Kinds: `processStart`, `processEnd`, `threadStart`, `threadEnd`, `cswitch`, `readyThread`, `sampledCpu`, `memorySample`, `virtualAlloc`, `virtualFree`, `lostEvents`, `marker`.

`LoadFixtureJson` rejects a missing schema, a missing synthetic flag, overflowed timestamps, implausible CPU indexes, and event counts above the cap. A cancelled token aborts the load.

## Normalized events

`TraceSession::Ingest` assigns a sequence if the producer did not, counts timestamp reversals, and stops accepting events at the cap. `Finalize` is idempotent in the sense that it rebuilds identities from scratch after every sort, so a late process-start still wins if its timestamp is earlier.

## ETL

See [etw.md](etw.md). The portable engine does not parse ETL bytes itself. That format is consumed only through the Windows trace APIs.

## Cancellation

Long loops call `ShouldContinue` every 4096 steps. Fixture load checks the token on each event. A cancelled finalize leaves the session unfinalized so a partial index is not treated as complete.
