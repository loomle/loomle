# Structured Agent Language

This directory contains the normative contract and TypeScript SDK for
Structured Agent Language (SAL). Loomle's Unreal Engine Bridge implements the
same normalized executor contract through `sal.query` and `sal.patch`.

SAL is compact, ordered text for humans and agents to inspect and modify
complex non-text objects. It keeps native UE names and values, is directly
copyable in human-agent collaboration, and reduces token cost across discovery,
reading, editing, and verification.

## Implemented Contract

The package implements one shared language and executor model. Products inject
their own named interface catalog instead of compiling domain knowledge into
SAL Core:

```txt
SAL Text
  -> parse and normalize
  -> schema-valid SalObject
  -> SalExecutor
  -> schema-valid ObjectResult
  -> ordered SAL Object Text
```

- `createSal({ executor, catalog })` exposes `query`, `patch`, and `schema`.
- `catalog` supplies static interface descriptions and Text; the executor's
  `interfaces` property selects the names active for its current target.
- Query and Patch use one generic `Target`; there is no public `domain` field.
- Query has one primary `operation` plus optional `where`, `with`, `orderBy`,
  and `page`.
- Patch contains one ordered `statements` array. Bindings and operations are
  not regrouped.
- Query and Patch return the same ordered `ObjectText` made from bindings,
  edges, and comments.
- Patch results add mutation state around that ordinary Object Text.
- Typed stable references use `object@id`; new objects use local aliases.
- Constructors and fields preserve UE-native text. SAL does not define a
  parallel UE type system.
- `with schema` remains an ordinary exact-object or exact-Palette query.

## Layout

- `src/parser.ts`: unified SAL parser and normalizer.
- `src/formatter.ts`: unified canonical formatter.
- `src/sdk.ts`: public facade and executor boundary.
- `src/interface-schema.ts`: injected catalog selection and index formatting.
- `schema/sal-object.schema.json`: normalized request/result contract.
- `src/generated/sal-object-schema.ts`: generated TypeScript model.
- `../engine/LoomleBridge/Source/LoomleBridge/Private/Sal/`: UE-backed
  executor, target resolution, ordered Object Text, and interface adapters.
- `fixtures/`: valid and rejected normalized JSON boundaries.
- `tests/fixtures/`: test-only in-memory executor.
- `examples/blueprint/`: current SAL Text examples with possible responses.
- `diagnostics/catalog.json`: current public diagnostic codes.

## Documents

- [`docs/OVERVIEW.md`](docs/OVERVIEW.md): SAL intent and mental model.
- [`docs/LANGUAGE_CORE.md`](docs/LANGUAGE_CORE.md): shared text and normalized
  object contract.
- [`docs/SDK_DESIGN.md`](docs/SDK_DESIGN.md): implemented SDK and executor
  boundary.
- [`docs/EDITOR_CONTEXT.md`](docs/EDITOR_CONTEXT.md): confirmed design for
  exact Unreal Editor interaction discovery and SAL handoff.
- [`docs/DIAGNOSTICS.md`](docs/DIAGNOSTICS.md): diagnostic layers and repair
  guidance.
- [`docs/DOMAINS.md`](docs/DOMAINS.md): domain ownership rules.
- `docs/domains/`: complete domain semantics.
- [`../interfaces/`](../interfaces/): Loomle's UE guide and injected static
  interface catalog.

- [`docs/BRIDGE_ARCHITECTURE.md`](docs/BRIDGE_ARCHITECTURE.md): implemented UE
  executor boundary and native interface mapping.
- [`docs/BRIDGE_IMPLEMENTATION_REPORT.md`](docs/BRIDGE_IMPLEMENTATION_REPORT.md):
  implemented surface, source mapping, autonomous decisions, verification, and
  explicit boundaries.

## Commands

```sh
npm run build
npm test
```

`npm test` checks generated types, JSON fixtures, diagnostic registration,
invalid syntax, parser/formatter round trips, examples, facade behavior, and
the generic memory executor across interface fixtures.

Regenerate schema-derived TypeScript after a schema change:

```sh
npm run generate:types
```

## Boundaries

- The test-only memory executor is a contract fixture, not a UE semantic model.
- The SDK does not mutate Unreal assets.
- UE-backed target resolution, Reflection, Palette execution, graph legality,
  transactions, compile, and save belong to the Bridge implementation.
- SAL is not a Blueprint replacement language or a public Graph IR.
