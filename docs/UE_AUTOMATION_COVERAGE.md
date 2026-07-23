# UE Automation Coverage

## Intent

UE Automation is Loomle's broad Bridge verification layer. Coverage is tracked
by public capability and UE state transition, not by raw test count. A test that
calls a domain interface directly does not cover normalized JSON decoding,
target resolution, public routing, result validation, or RPC dispatch.

For each active Query family, representative coverage requires:

- one successful public request with semantic result assertions;
- invalid identity or invalid clause failure;
- proof that the Query preserves authored state; and
- pagination, depth, schema, or result-budget behavior when the operation owns
  that boundary.

For each active Patch family, representative coverage requires:

- dry-run through the real validation and planning path;
- live apply followed by native and SAL Query readback;
- one-step Undo with state verification;
- invalid identity or value rejection;
- rollback after a failure that occurs after live mutation has begun; and
- save, unload, reload, and Query verification when the edit is persistent.

## Coverage Levels

| Level | Meaning |
|---|---|
| None | No UE Automation reaches the public capability. |
| Smoke | One narrow success or regression path; the family is not closed. |
| Contract | Public decoding, resolution, routing, result shape, success, and representative failure are covered. |
| Lifecycle | Contract plus dry-run, live apply, readback, Undo, and rollback where applicable. |
| Persistent | Lifecycle plus save, unload, reload, and post-load readback. |

## July 2026 Baseline

The audit baseline contains 81 tests, of which 54 belong to StateTree.
Operation-touch analysis finds approximately 21 of 51 Query operations and 15
of 39 Patch kinds reached at least once. This is not line or branch coverage.

| Surface | Query | Patch | Principal missing boundary |
|---|---|---|---|
| Public SAL/RPC entry | None | None | Successful `rpc.invoke`, decode, target resolution, routing, output validation |
| Asset | Smoke | None | Save and persistence |
| Class | Contract, except defaults | Lifecycle, except save | Defaults collections and persistence |
| Blueprint | None | Smoke | Queries, structural edits, compile/save, rollback |
| Graph | None | Smoke | Queries, live structural edits, links, dynamic Pins, rollback |
| Widget | None | Smoke, dry-run only | Queries, live tree/Slot edits, Undo, rollback |
| Reference | Smoke | — | Declaration-kind matrix and local/project parity |
| StateTree | Contract | Lifecycle | Class-backed nodes and persistent save/reload |
| Editor Context | None | — | Provider recognition, focus recovery, stale surface lifecycle |
| Pipe lifecycle | Smoke | — | In-flight cancel, disconnect, shutdown, and concurrent admission |

## First Remediation

The first coverage pass raises the suite from 81 to 97 native tests. A packaged
Mac Development plugin build and an isolated `UnrealEditor-Cmd` run completed
all 97 tests without failure, timeout, crash, or log hazard.

The added release anchors cover:

- normalized Query and Patch decode, target resolution, interface dispatch, and
  outgoing validation, plus one successful public `rpc.invoke`;
- Blueprint and Graph summaries, collections, exact objects, Nodes, and Pins;
- Widget summary, tree, collection, and exact-object reads;
- live Graph and Widget edits with native/SAL readback and one-step Undo;
- Blueprint save, unload, garbage collection, reload, stable identity, native
  readback, and SAL readback;
- a live Blueprint failure after an earlier statement has mutated the asset,
  including atomic rollback, dirty-state restoration, and preservation of the
  user's existing redo entry;
- RPC cancellation and shutdown before queued Game Thread admission, plus
  concurrent admission's single-winner invariant; and
- Editor Context Provider priority, focus-loss retention, and rejection of a
  stale tracked DockTab.

This pass deliberately does not promote every surface to `Contract` or
`Lifecycle`: most new domain tests call the C++ interface directly, while the
public normalized path currently has one representative Class target.

| Surface | Current level | Highest-priority remaining boundary |
|---|---|---|
| Public SAL/RPC entry | Contract, representative target only | Repeat normalized resolution and routing for every active target kind |
| Asset | Smoke | Mutation terminal and persistence behavior |
| Class | Lifecycle, except save | Defaults collections and persistence |
| Blueprint | Smoke with persistent and rollback anchors | Public-path matrix and structural create/remove/compile lifecycle |
| Graph | Smoke with Query and live-move lifecycle | Create/connect/dynamic-Pin lifecycle, rollback, and persistence |
| Widget | Smoke with Query and live-field lifecycle | Tree/Slot add, move, wrap, replace, rollback, and persistence |
| Reference | Smoke | Declaration-kind matrix and local/project parity |
| StateTree | Lifecycle | Class-backed Nodes and persistent save/reload |
| Editor Context | Smoke | Deterministic built-in Blueprint, Widget, Content Browser, and Level Provider recognition |
| Pipe lifecycle | RPC contract | Real connection close, busy limit, blocked I/O, and server-stop integration |

## Release Gate

An active public operation must have a matrix entry and a named native test.
Adding an operation without its representative success, failure, and relevant
state-transition coverage is incomplete implementation.

The 0.7 native release gate must at minimum close:

1. one normalized public Query and Patch path for every active target kind;
2. Blueprint, Graph, and Widget Query families;
3. representative Blueprint, Graph, and Widget structural Patch lifecycles;
4. one real persistent save/unload/reload round trip;
5. Editor Context recognition for its primary editor surfaces; and
6. in-flight cancellation and shutdown behavior at the transport boundary.

Synthetic fixtures remain useful for deterministic edge cases. At least one
authored, compiled asset fixture must protect each Blueprint-owned interface so
GeneratedClass, CDO, subobject, stable-identity, and PostLoad behavior are not
replaced by hand-built transient state.
