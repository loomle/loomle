# Structured Agent Language SDK Experiment

This directory is an experimental TypeScript package for Structured Agent
Language (SAL). It is not part of Loomle's public protocol, release package,
MCP manifest, or Unreal Engine bridge.

SAL is a token-efficient shared text language for humans and agents to inspect,
exchange, and modify complex non-text objects. It expresses object structure,
relationships, capabilities, and edits as compact, ordered, copyable text while
preserving the native system's semantics.

Its three primary goals are faithful textualization, direct human-agent
collaboration, and lower total token cost across the complete discovery, read,
reasoning, modification, and verification loop.

The experiment started as a compact graph text format. The current design is
broader: SAL is a line-oriented, agent-facing object language for UE work.
Graph is the first proven domain; asset, Blueprint, and widget now use the same
language core instead of inventing separate text formats.

The design separates three layers:

```txt
sugar text
  -> canonical text
  -> normalized JSON
```

Agents use SAL text. The SDK parses and normalizes that text. The bridge and
schema contract use normalized JSON.

## Current Scope

- Provide the `createSal({ adapters })` facade used as `sal.query(text)`,
  `sal.patch(text)`, and `sal.schema(module?)`.
- Parse accepted SAL text into normalized `SalObject` values.
- Format normalized objects back to SAL text.
- Validate normalized objects against `schema/sal-object.schema.json`.
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

- `docs/OVERVIEW.md`: Next SAL direction, representation layers, and core
  design rules.
- `docs/INTERFACE_SCHEMA.md`: Resident Agent context and the progressive
  interface-schema workflow.
- `docs/LANGUAGE_CORE.md`: Shared statement, constructor, value, and reference
  syntax.
- `docs/DIAGNOSTICS.md`: Shared diagnostic shape, error layers, and repair
  guidance.
- `diagnostics/catalog.json`: Lightweight registry for public diagnostic codes.
- `docs/DOMAINS.md`: How domains own their syntax, normalization, object
  model, query, patch, diagnostics, and examples.
- `docs/SDK_DESIGN.md`: SDK facade, adapter contract, diagnostics, and result
  types for the current experiment.
- `docs/interfaces/`: Static Text returned by `sal.schema(module?)`.
- `schema/sal-object.schema.json`: JSON Schema contract for normalized SAL
  objects and adapter/RPC results.

Domain docs:

- `docs/domains/graph.md`: Graph domain from sugar to canonical text
  to normalized JSON.
- `docs/domains/asset.md`: Asset domain for Asset Registry-backed search,
  resolve, registry tags, and asset result text.
- `docs/domains/blueprint.md`: Blueprint domain for class contract, variables,
  dispatchers, graphs, component trees, and compound Timeline Node state.
- `docs/domains/widget.md`: Widget domain for UMG tree constructors, tree
  queries, palette entries, and widget patching.

Bridge planning and reference docs:

- `docs/BRIDGE_ARCHITECTURE.md`: Concise UE bridge architecture for
  `sal.query` and `sal.patch`.
- `docs/BRIDGE_ASSET_ADAPTER.md`: UE Asset Registry-backed asset query
  adapter responsibilities.
- `docs/BRIDGE_BLUEPRINT_ADAPTER.md`: Blueprint domain adapter
  responsibilities.
- `docs/BRIDGE_QUERY_SPIKE.md`: First UE-backed `sal.query` spike
  contract.
- `docs/BRIDGE_QUERY_IMPLEMENTATION_NOTES.md`: UE bridge code references
  and implementation notes for the query spike.
- `docs/EXAMPLE_SOURCES.md`: Sources behind the Blueprint example set.

The existing UE Bridge still uses the legacy `lgl.query`, `lgl.patch`,
`Private/Lgl`, and `FLgl*` implementation names. Bridge migration is deferred
until the SAL SDK implements the confirmed language contract; legacy bridge
reference documents retain those real identifiers intentionally.

## Commands

Install dependencies from this directory, then run:

```sh
npm run build
npm test
```

`npm test` validates the normalized object fixtures in `fixtures/object` against
`schema/sal-object.schema.json` and verifies that rejected boundary examples in
`fixtures/object-invalid` fail schema validation.

`npm run generate:types` regenerates TypeScript object-model types from
`schema/sal-object.schema.json`. `npm test` runs `npm run check:generated` first
so schema changes fail unless the generated types are updated.

`npm run test:examples:extended` runs the extended Blueprint example corpus on
its own; the same check is included in the default `npm test` gate.

`npm run test:examples:reference` audits the larger Blueprint reference examples
on its own; the same check is included in the default `npm test` gate.

The parser/formatter covers statement-list object, query, patch, and palette
entry forms for the current graph, asset, Blueprint, and widget
domains.

The target facade is `createSal({ adapters })`, returning an object used as
`sal.query(text)`, `sal.patch(text)`, and `sal.schema(module?)`. Query and patch
parse SAL text, dispatch by target domain to an adapter or bridge RPC, and
format adapter `ObjectResult` values back to SAL text. Static interface cards
are served by the SDK; exact current-object schema is queried through the
owning adapter. The current experiment still exposes the normalized Object
JSON Schema from `schema()` and must migrate to this documented Text contract.

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

The examples still use the earlier implemented statement-list Graph identity.
They must migrate to the documented owner locator plus GraphGuid when the
schema and parser are updated:

```txt
bp = blueprint(asset: "/Game/BP_SALExample.BP_SALExample", id: "blueprint-guid")
g = graph(asset: bp, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)

begin = node(graph: g, type: "/Script/BlueprintGraph.K2Node_Event", id: "7A9D", at: [0, 0], size: [180, 80])
delay = node(graph: g, type: "/Script/BlueprintGraph.K2Node_CallFunction", id: "81EF", FunctionReference: "<FMemberReference native text>", at: [320, 0], size: [200, 100])
print = node(graph: g, type: "/Script/BlueprintGraph.K2Node_CallFunction", id: "C2B0", FunctionReference: "<FMemberReference native text>", at: [640, 0], size: [220, 120])

delay.Duration = pin(id: "duration-pin-guid", type: "<FEdGraphPinType native text>", direction: in, DefaultValue: "1.0")
print.InString = pin(id: "string-pin-guid", type: "<FEdGraphPinType native text>", direction: in, DefaultValue: "Ready")

begin.Then -> delay.Exec/Completed -> print.Exec
```

Compact `graph` examples are readback snapshots and do not include Palette
creation entries. Patch examples use stable Palette Entry ids when they create
new nodes.

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
