# Loomle Graph Lang SDK Experiment

This directory is an experimental TypeScript package for a Loomle Graph Lang
(LGL) SDK. It is not part of Loomle's public protocol, release package, MCP
manifest, or Unreal Engine bridge.

The experiment started as a compact graph text format. The current design is
broader: LGL is a line-oriented, agent-facing object language for UE work.
Graph is the first proven domain; asset, Blueprint, and widget now use the same
language core instead of inventing separate text formats.

The design separates three layers:

```txt
sugar text
  -> canonical text
  -> normalized JSON
```

Agents use LGL text. The SDK parses and normalizes that text. The bridge and
schema contract use normalized JSON.

## Current Scope

- Provide the `createLgl({ adapters })` facade used as `lgl.query(text)`,
  `lgl.patch(text)`, and `lgl.schema()`.
- Parse accepted LGL text into normalized `LglObject` values.
- Format normalized objects back to LGL text.
- Validate normalized objects against `schema/lgl-object.schema.json`.
- Generate TypeScript object-model types from the schema.
- Exercise the adapter contract with the in-memory graph, asset, Blueprint, and
  widget adapters.
- Keep graph, asset, Blueprint, and widget examples under parser/formatter and
  memory-adapter tests.
- Keep Unreal-specific behavior behind adapters; the SDK surface should not
  expose UE bridge internals directly.

## Implementation Layout

- `src/parser.ts`, `src/formatter.ts`, and `src/memory-adapter.ts` are stable
  public entry shims.
- `src/core/` contains shared text parsing helpers that are not domain-specific.
- `src/graph/` contains graph text parsing, graph patch parsing, graph
  formatting, and the in-memory graph adapter.
- `src/asset/` contains asset query parsing, asset result formatting, and the
  in-memory asset adapter.
- `src/blueprint/` contains Blueprint query parsing, Blueprint result
  formatting, and the in-memory Blueprint adapter.
- `src/widget/` contains widget query parsing, widget result formatting, and
  the in-memory widget adapter.

## Documents

The docs are organized around the shared language core plus UE domains. The
graph, asset, Blueprint, and widget TypeScript adapters exercise the SDK
contract. UE-backed adapters remain future bridge work.

Language and SDK docs:

- `docs/OVERVIEW.md`: Next LGL direction, representation layers, and core
  design rules.
- `docs/LANGUAGE_CORE.md`: Shared statement, constructor, value, and reference
  syntax.
- `docs/DIAGNOSTICS.md`: Shared diagnostic shape, error layers, and repair
  guidance.
- `diagnostics/catalog.json`: Lightweight registry for public diagnostic codes.
- `docs/DOMAINS.md`: How domains own their syntax, normalization, object
  model, query, patch, diagnostics, and examples.
- `docs/SDK_DESIGN.md`: SDK facade, adapter contract, diagnostics, and result
  types for the current experiment.
- `schema/lgl-object.schema.json`: JSON Schema contract for normalized LGL
  objects and adapter/RPC results.

Domain docs:

- `docs/domains/graph.md`: Graph domain from sugar to canonical text
  to normalized JSON.
- `docs/domains/asset.md`: Asset domain for Asset Registry-backed search,
  resolve, registry tags, and asset result text.
- `docs/domains/blueprint.md`: Blueprint domain for class contract, member
  declarations, custom events, and component tree structure.
- `docs/domains/widget.md`: Widget domain for UMG tree constructors, tree
  queries, palette entries, and widget patching.

Bridge planning and reference docs:

- `docs/BRIDGE_ARCHITECTURE.md`: Concise UE bridge architecture for
  `lgl.query` and `lgl.patch`.
- `docs/BRIDGE_ASSET_ADAPTER.md`: UE Asset Registry-backed asset query
  adapter responsibilities.
- `docs/BRIDGE_BLUEPRINT_ADAPTER.md`: Blueprint domain adapter
  responsibilities.
- `docs/BRIDGE_QUERY_SPIKE.md`: First UE-backed `lgl.query` spike
  contract.
- `docs/BRIDGE_QUERY_IMPLEMENTATION_NOTES.md`: UE bridge code references
  and implementation notes for the query spike.
- `docs/EXAMPLE_SOURCES.md`: Sources behind the Blueprint example set.

## Commands

Install dependencies from this directory, then run:

```sh
npm run build
npm test
```

`npm test` validates the normalized object fixtures in `fixtures/object` against
`schema/lgl-object.schema.json` and verifies that rejected boundary examples in
`fixtures/object-invalid` fail schema validation.

`npm run generate:types` regenerates TypeScript object-model types from
`schema/lgl-object.schema.json`. `npm test` runs `npm run check:generated` first
so schema changes fail unless the generated types are updated.

`npm run test:examples:extended` runs the extended Blueprint example corpus on
its own; the same check is included in the default `npm test` gate.

`npm run test:examples:reference` audits the larger Blueprint reference examples
on its own; the same check is included in the default `npm test` gate.

The parser/formatter covers statement-list object, query, patch, and palette
entry forms for the current graph, asset, Blueprint, and widget
domains.

The current facade is `createLgl({ adapters })`, returning an object used as
`lgl.query(text)`, `lgl.patch(text)`, and `lgl.schema()`. Query and patch parse
LGL text, dispatch by target domain to an adapter or bridge RPC, and format
adapter `ObjectResult` values back to LGL text. Schema is served by the
TypeScript SDK from the local object contract. Capability metadata needs
separate design before it becomes part of this API.

`createMemoryGraphAdapter` is an in-memory SDK test fixture for exercising the
adapter contract without Unreal Editor. It is useful for query/patch closed-loop
tests, but it is not a Blueprint semantics implementation.

## Example Set

Blueprint examples currently cover:

- BeginPlay -> Delay -> PrintString
- Branch with data-flow condition
- overlap-gated actor input
- launchpad overlap -> Cast -> LaunchCharacter
- per-tick LineTraceByChannel -> PrintString
- overlap-driven door Timeline
- input action -> socket transform -> SpawnActorFromClass
- small patch examples for inserting Delay and Branch guard nodes
- query examples for nodes, paths, surrounding context, palette discovery, and
  detailed node output

The examples use the implemented statement-list graph form:

```txt
bp = asset(path: "/Game/BP_LGLExample.BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)

begin = node(graph: g, type: EventBeginPlay, id: "7A9D", at: [0, 0], size: [180, 80])
delay = node(graph: g, type: Delay, id: "81EF", Duration: 1.0, at: [320, 0], size: [200, 100])
print = node(graph: g, type: PrintString, id: "C2B0", InString: "Ready", at: [640, 0], size: [220, 120])

begin.Then -> delay.Exec/Completed -> print.Exec
```

Compact `graph` examples are readback snapshots and do not include palette
creation entries. Patch examples use stable palette entry ids or shortcut
constructors when they create new nodes.

Layout readback appears as named node and pin fields such as `at: [x, y]`,
`size: [w, h]`, and `anchor: [x, y]`. Patch layout mutation is currently
limited to `move node to (...)` and `move node by (...)`.

Creation examples:

```txt
print = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
delay = delay(duration: 1.0)
overlap = event(component: Trigger, event: OnComponentBeginOverlap)
```

## Non-Goals

- This is not a Blueprint DSL.
- This does not expose a public Graph IR.
- This does not require callers to understand parser internals.
- This does not require maintaining Rust and TypeScript versions of the same
  client.
- This does not mutate Unreal assets.
- This does not define a stable public Loomle interface.
