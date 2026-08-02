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
| Public Patch | Smoke with representative normalized failure | All mutable interfaces route through normalized dry-run; Graph `by` rejection | Normalized live apply/RPC and broader failure matrix |
| Asset | Persistent | Save dry/live, unload/reload, zero-load Query | I/O failure behavior |
| Class | Contract | Fixed-array save/unload/reload | Failure after live mutation begins |
| Blueprint | Persistent for representative operations | Declaration, Graph, Component, rollback | Compound Interface/Component operations |
| Graph Query | Contract | Flow, context, Palette, schema, stored layout fallback, synthetic headless Node/Pin Slate geometry, rendered standalone Blueprint Editor geometry at low LOD, response-wide fallback | `nodes` condition/order/cursor, normalized public layout path, and complete live-surface matrix |
| Graph Patch | Smoke with broad Lifecycle anchors | Add, connect, insert, break, dynamic Pin, absolute move plan/diff/precision/no-op/readback/parity rollback, Undo, persistent native topology | Mixed-operation diff contract, broader invalid-target matrix, reset, SAL-authored persistent topology |
| Widget Query | Smoke | Tree depth, detached objects, Palette/schema | `widgets` condition/order/cursor and failure matrix |
| Widget Patch | Smoke with a Persistent anchor | Add, Slot, wrap, rename, duplicate, replace, save/reload | Move, Named Slot, invalid Patch, live rollback |
| Reference | Contract | Six local declaration kinds, Blueprint/Graph scope, pagination, zero-load project index | Widget, Macro, native member, and project parity |
| StateTree | Contract with a Persistent anchor | Class-backed Node/Binding and compile/save/reload | Failure after live mutation begins |
| Editor Context | Smoke with rendered Blueprint anchor | Modal, Content Browser, Level Editor, unknown fallback, standalone Blueprint Graph focus/window recovery | Real Widget/Details focus and selection recovery |
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

## Variable Palette Identity Audit

The July 24 macOS arm64 run raises the suite from 128 to 129 tests. The added
Graph regression proves that member-variable Getter and Setter Palette
identities distinguish two variables, exact reads replay all four identities,
and both dry-run and live Patch create Set Nodes for the requested `VarGuid` in
ordinary Blueprints and Animation Blueprints. The test removes the temporary
Nodes before fixture cleanup. The complete 129-test run passed without a
failure, crash report, or runner-classified log hazard.

## SAL v3 Object And Target Migration Audit

The SAL v3 migration raises the source suite from 129 to 135 tests. The added
public-path and result-context coverage proves:

- all six Domains route through one flat `target { domain: ... }` model;
- ordinary object fields cannot tunnel into the retired Call or `kind + id`
  executor shapes;
- tag erasure preserves Blueprint, Graph, Widget, and StateTree creation;
- Graph Pins and function-local Variables resolve through owner-relative native
  identity paths;
- exact, Domain-root, and unresolved results enforce their closed Target
  contexts; and
- related Targets and handoffs remain explicit and independently exact;
- structural `target_self` relationship subjects are accepted only by the
  References operation, with callable Graph support and explicit capability
  errors elsewhere; and
- reserved or otherwise non-local UE names fall back to SAL strings, while
  ordinary objects whose fields exactly resemble Name or reference shapes
  remain ordinary ObjectExpr data.

The final same-source macOS arm64 Development BuildPlugin candidate compiled
successfully. Its isolated `UnrealEditor-Cmd` run discovered and passed all 135
tests: 121 reported success and 14 success with ordinary warning entries. No
test failed, timed out, remained unrun, or was missing; the runner found no log
hazard or new crash report.

The earlier 133-test diagnostic run had exposed one fixture-lifecycle defect:
the PublicPath StateTree fixture installed a native Schema without first
calling UE's `ValidateStateTree`, leaving `EditorSchema` initialization
pending. The corrected fixture performs that native validation and reacquires
`EditorData`, Schema, and root identity afterward. The complete 135-test result
closes that repair and the SAL v3 native audit.

## Graph Layout And Absolute Move Audit

The July 31 macOS arm64 run raises the native suite from 135 to 138 tests. The
final same-source plugin candidate built successfully with the Installed UE 5.7
Arm64 toolchain, and its isolated `UnrealEditor-Cmd` run passed all 138 tests.
No test failed, timed out, remained unrun, or was missing; the runner found no
new crash report or runner-classified log hazard.

The three added tests and the expanded live-move release blocker prove:

- a closed Graph preserves stored `at` and optional `size`, emits no visual
  geometry, and returns the response-level
  `capability.layout_geometry_unavailable` warning with `graph_not_open`;
- one synthetic headless `SGraphEditor` surface returns measured graph-space
  Node bounds and Pin row, center, placement-anchor, visibility-state, and
  ordered-reason fields without changing the viewport, Package dirty state, or
  transaction history;
- desynchronizing UObject Pin presentation from the already-built Slate widget
  inventory removes every visual field and reports `pin_widget_unavailable`
  instead of mixing measured and fallback objects;
- absolute `move ... to (...)` planning preserves statement order and exact
  `before.at`, `after.at`, and `changed` effects, including precision-boundary
  rejection and no-op omission from the rich move-only diff; and
- live movement reads back exact stored positions, skips mutation for no-op
  statements, retains atomic Graph Patch behavior, and creates one reversible
  Undo step for actual changes; and
- a live-only Schema readback mismatch returns
  `validation.layout_apply_failed`, retains the attempted plan, omits partial
  object and diff output, and restores Node position, Package dirty state, and
  Undo history atomically.

That 138-test `UnrealEditor-Cmd -NullRHI` run does not prove geometry capture
from a real Blueprint asset editor. Layout acceptance therefore has two
separate layers: the retained headless synthetic fixture checks measurement and
fallback logic, while a rendered Editor test must open a standalone
`FBlueprintEditor`, foreground the exact Graph document through UE's native
path, verify the normal native window and focus path, and run the same exact-Node
`with layout` Query. Ambiguous surfaces, active drag or relink interactions,
off-viewport and custom widgets, second-pass-dependent Nodes, Comments, Knots,
the remaining hidden-Pin reasons, row-edge anchors, and every Query projection
still need dedicated variants even after that rendered gate passes.

The August 1 corrective run exercises those layers separately. The exact UE 5.7
arm64 `UnrealEditor` executable, without `-NullRHI`, passed
`Loomle.Sal.Graph.Layout.LiveGeometry` 1/1 after opening a real standalone
Blueprint Editor, foregrounding its exact Graph document, and measuring its
compact low-LOD Pin presentation. The full isolated
`UnrealEditor-Cmd -NullRHI` suite then passed 140/140 tests: 126 succeeded and 14
succeeded with existing ordinary warning entries, with zero failures, unrun or
in-process tests, timeout, runner-classified log hazard, or new crash report.
That headless run executed `HeadlessSyntheticGeometry`; `LiveGeometry` recorded
an explicit non-rendering skip and relies on the separate rendered result above.

## Editor Context Window And Unsaved-Level Audit

The August 1 rendered UE 5.7 arm64 run passed all 5
`Loomle.EditorContext.BuiltIn` tests without warnings or failures. The expanded
fixtures prove:

- a standalone Blueprint Editor can recover its exact native Asset Editor and
  registered Major Tab from the root window even when the focused child has no
  usable DockTab path;
- native `SGraphPanel` focus resolves the exact focused Graph, while a stale
  Graph UI state without an owned focused Graph cannot invent an EventGraph;
- an exact Blueprint Editor without native Graph evidence falls back to its
  exact Blueprint Target; and
- a tabless native `SLevelViewport` still establishes Level Editor ownership,
  while an unsaved temporary World returns `unresolved_target`, explains the
  missing persistent Asset identity, and directs the agent to save the map.

The test uses a real rendered standalone Blueprint window and verifies fixture
cleanup after closing it. Widget Designer and Details-panel focus/selection
recovery remain the primary Editor Context coverage gaps.

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
transport behavior in item 6. Item 5 now has a real standalone Blueprint
focus/window-recovery anchor but remains partial until Widget and Details
focus/selection paths are exercised. The stricter per-surface
boundaries in the table above remain release risks even though the current
138-test matrix is green.

Synthetic fixtures remain useful for deterministic edge cases. At least one
authored, compiled asset fixture must protect each Blueprint-owned interface so
GeneratedClass, CDO, subobject, stable-identity, and PostLoad behavior are not
replaced by hand-built transient state.
