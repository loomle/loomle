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

## Second Remediation

The second coverage pass raises the native suite from 97 to 127 tests. A
packaged macOS arm64 Development plugin build succeeded, and an isolated
`UnrealEditor-Cmd` run reported all 127 tests as `Success`; 14 contained
ordinary warning entries. The run had zero failure, timeout, missing test,
crash report, or runner-classified log hazard.

The 30 added tests cover:

- normalized Query routing across every resolved SAL target kind, composed
  Widget and StateTree interfaces, normalized dry-run Patch routing, and the
  final 128 KiB result gate;
- Asset and Class filtering, schema, native field shapes, save, unload, reload,
  and zero-load reads;
- Blueprint declarations, Graphs, Components, Graph traversal, Palette schema,
  structural Nodes, Edges, insertion, dynamic Pins, Undo, and PostLoad
  topology;
- Widget tree depth, detached discovery, Panel/Slot placement, compound
  operations, and stable identity across compile/save/reload;
- Reference declaration kinds, Blueprint/Graph scopes, pagination, and
  deduplication;
- class-backed StateTree Nodes and Bindings through Palette, schema, Undo,
  compile, save, and reload;
- built-in Editor Context recognition for modal, Content Browser, Level
  Editor, and unknown surfaces; and
- real native Pipe round trips, synchronous control messages, stale-response
  isolation, and shutdown while a request worker is active.

The pass exposed and fixed five implementation defects:

- Darwin could leave the listener blocked in `accept()` during shutdown;
- the Pipe busy response used pretty JSON even though newline is the transport
  frame boundary;
- Blueprint Variable reset used the struct constructor instead of UE's visible
  declaration-category default;
- Graph `NodeComment` was incorrectly excluded from Graph-editor-writable
  state; and
- exact Graph Palette schema could inspect an unprimed Node template before UE
  had allocated its future Pins.

This is a green result for the current matrix, not a claim that every active
operation has reached Lifecycle or Persistent coverage. Applying the level
definitions strictly gives the following state:

| Surface | Current level | Verified higher-level anchors | Principal remaining boundary |
|---|---|---|---|
| Public Query | Contract | All target routing, composed interfaces, result-size gate | — |
| Public Patch | Smoke | All mutable interfaces route through normalized dry-run | Normalized failure and real apply/RPC |
| Asset | Persistent | Save dry/live, unload/reload, zero-load Query | I/O failure behavior |
| Class | Contract | Fixed-array save/unload/reload | Failure after live mutation begins |
| Blueprint | Persistent for representative operations | Declaration, Graph, Component, rollback | Compound Interface/Component operations |
| Graph Query | Contract | Flow, context, Palette, schema | `nodes` condition/order/cursor matrix |
| Graph Patch | Smoke with broad Lifecycle anchors | Add, connect, insert, break, dynamic Pin, Undo, persistent native topology | Invalid Patch, live rollback, reset, SAL-authored persistent topology |
| Widget Query | Smoke | Tree depth, detached objects, Palette/schema | `widgets` condition/order/cursor and failure matrix |
| Widget Patch | Smoke with a Persistent anchor | Add, Slot, wrap, rename, duplicate, replace, save/reload | Move, Named Slot, invalid Patch, live rollback |
| Reference | Contract | Six local declaration kinds, Blueprint/Graph scope, pagination, zero-load project index | Widget, Macro, native member, and project parity |
| StateTree | Contract with a Persistent anchor | Class-backed Node/Binding and compile/save/reload | Failure after live mutation begins |
| Editor Context | Smoke | Modal, Content Browser, Level Editor, unknown fallback | Real Blueprint/Widget/Details focus and selection recovery |
| Pipe | Real transport contract with Lifecycle anchors | Windows overlapped round trip, control, stale isolation, worker-aware stop | Busy saturation, partial frames, and multiple simultaneous clients |

## Windows Repair Audit

The July 24 Windows run kept the 127-test matrix and exercised it against UE
5.7.4. It closed three host-specific regressions without weakening the
existing assertions:

- Windows Pipe instances now use independent overlapped connect, read, and
  serialized write operations, so a pending read cannot hold the preceding
  response until another client frame arrives;
- Graph Palette searches use UE's complete localized/source search text and
  native Schema weight, with exact source or localized titles kept ahead of
  related keyword matches for stable pagination; and
- the `Branch` first-page, `Sequence` insertion, real Pipe round-trip, stale
  response isolation, packaged Client-to-Editor named-pipe, and stripped
  archive boundaries all passed in the same local audit.

The repair also confirmed that UE's native action weight chooses a suggestion
but does not itself define a paged result order. The exact-title tier is
therefore part of Loomle's agent-facing pagination contract, while candidate
eligibility and within-tier relevance remain UE-owned.

## Pin Identity Scope Audit

The July 24 macOS arm64 run raises the suite from 127 to 128 tests. The added
Graph regression proves that PinId reuse in another Graph does not affect an
exact read, PinId reuse on another Node in the bound Graph reports
`resolution.pin_ambiguous`, Pin-targeted mutation fails closed, and an
unrelated Node dry run remains valid. The complete 128-test run passed without
a failure, crash report, or runner-classified log hazard.

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

The second remediation provides representative anchors for items 1–4 and the
transport behavior in item 6. Item 5 remains partial until real Blueprint and
Widget editor focus/selection paths are exercised. The stricter per-surface
boundaries in the table above remain release risks even though the current
128-test matrix is green.

Synthetic fixtures remain useful for deterministic edge cases. At least one
authored, compiled asset fixture must protect each Blueprint-owned interface so
GeneratedClass, CDO, subobject, stable-identity, and PostLoad behavior are not
replaced by hand-built transient state.
