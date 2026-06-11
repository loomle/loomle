# Observability Tail Design

## Intent

`diagnostic.tail` and `log.tail` expose persisted runtime evidence in the shape
agents naturally need while debugging Unreal Editor state.

The default read should answer "what just happened?" Omitting `fromSeq` returns
the latest matching events. Incremental polling remains explicit: supplying
`fromSeq` returns events after that cursor.

## Tool Boundary

The tools are read-only observability surfaces:

- `diagnostic.tail` reads structured Loomle diagnostic events such as tool
  errors, validation failures, bridge state, and asset-specific diagnostics.
- `log.tail` reads captured Unreal output log events.

Both tools read Loomle's persisted evidence stores under the Unreal project's
`Saved/Loomle` directory. They do not query or mutate UE editor objects.

## Input Schema

Shared fields:

- `fromSeq`: optional non-negative integer. When omitted, return the latest
  matching events. When supplied, return matching events with `seq > fromSeq`.
- `limit`: optional positive integer, default `200`, maximum `1000`.
- `filters`: optional tool-specific filter object.

`diagnostic.tail` filters:

- `severity`
- `category`
- `source`
- `assetPathPrefix`

`log.tail` filters:

- `minVerbosity`
- `category`
- `categories`
- `source`
- `contains`

## Output Schema

Both tools return:

- `items`: matching events in chronological order, oldest to newest.
- `fromSeq`: the supplied cursor, or `0` when omitted.
- `nextSeq`: the highest sequence returned, or `fromSeq`/`0` when no item was
  returned.
- `nextFromSeq`: the recommended cursor for the next polling call.
- `hasMore`: true when additional matching events exist beyond the returned
  page. In latest mode, this means older matching events exist.
- `latestSeq`: latest sequence observed at read time.
- `highWatermark`: alias of `latestSeq`.

For omitted `fromSeq`, `nextFromSeq` advances to `highWatermark` so the next
call can poll only newly written events.

For explicit `fromSeq`, `nextFromSeq` equals `nextSeq` when a forward page is
truncated, otherwise it advances to `highWatermark`.

## Error Responses

- `INVALID_ARGUMENT`: `fromSeq` is not a non-negative integer.
- `INVALID_ARGUMENT`: `limit < 1`.

Limits above `1000` are clamped to `1000`.

## UE Mapping

`log.tail` captures UE output through an `FOutputDevice` registered with
`GLog`. The bridge appends each captured line as JSONL with a monotonically
increasing `seq`.

`diagnostic.tail` appends structured Loomle events through bridge code paths
that already know the diagnostic category, source, severity, message, and
optional asset context.

At startup the bridge initializes each store by reading the last valid JSONL
record and setting the next sequence to `last_seq + 1`.

## Audit

The latest-default behavior matches agent debugging demand: an empty tail call
now returns recent evidence instead of the oldest persisted page. Explicit
cursor reads preserve the existing incremental polling contract and keep
`nextFromSeq` as the handoff cursor.

The remaining gap is historical pagination before the latest page. Latest mode
reports older matches through `hasMore`, but the public contract does not yet
include a backward cursor. Agents should use explicit `fromSeq` for forward
polling and omit `fromSeq` for recent context.
