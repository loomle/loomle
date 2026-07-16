# Structured Agent Language SDK Experiment

This directory contains the TypeScript SDK experiment for Structured Agent
Language (SAL). It is not yet part of Loomle's public MCP surface or Unreal
Engine Bridge.

SAL is compact, ordered text for humans and agents to inspect and modify
complex non-text objects. It keeps native UE names and values, is directly
copyable in human-agent collaboration, and reduces token cost across discovery,
reading, editing, and verification.

## Implemented Contract

The package currently implements one shared model across asset, Blueprint,
class, graph, and widget interfaces:

```txt
SAL Text
  -> parse and normalize
  -> schema-valid SalObject
  -> SalExecutor
  -> schema-valid ObjectResult
  -> ordered SAL Object Text
```

- `createSal({ executor })` exposes `query`, `patch`, and `schema`.
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
- `src/memory-executor.ts`: generic in-memory contract fixture.
- `src/{asset,blueprint,widget}/memory-executor.ts`: thin interface wrappers.
- `src/interface-schema.ts`: resident static interface cards.
- `schema/sal-object.schema.json`: normalized request/result contract.
- `src/generated/sal-object-schema.ts`: generated TypeScript model.
- `fixtures/`: valid and rejected normalized JSON boundaries.
- `examples/blueprint/`: current SAL Text examples with possible responses.
- `diagnostics/catalog.json`: current public diagnostic codes.

## Documents

- [`docs/OVERVIEW.md`](docs/OVERVIEW.md): SAL intent and mental model.
- [`docs/INTERFACE_SCHEMA.md`](docs/INTERFACE_SCHEMA.md): three-layer schema
  discovery workflow.
- [`docs/LANGUAGE_CORE.md`](docs/LANGUAGE_CORE.md): shared text and normalized
  object contract.
- [`docs/SDK_DESIGN.md`](docs/SDK_DESIGN.md): implemented SDK and executor
  boundary.
- [`docs/DIAGNOSTICS.md`](docs/DIAGNOSTICS.md): diagnostic layers and repair
  guidance.
- [`docs/DOMAINS.md`](docs/DOMAINS.md): domain ownership rules.
- `docs/domains/`: complete domain semantics.
- `docs/interfaces/`: compact Text returned by `sal.schema(module)`.

Bridge documents remain historical or forward-looking implementation context.
The existing UE Bridge still uses legacy `lgl.query`, `lgl.patch`, `Private/Lgl`,
and `FLgl*` names. Bridge migration is intentionally deferred until the SAL SDK
contract is complete.

## Commands

```sh
npm run build
npm test
```

`npm test` checks generated types, JSON fixtures, diagnostic registration,
invalid syntax, parser/formatter round trips, examples, facade behavior, and
the generic/domain memory executors.

Regenerate schema-derived TypeScript after a schema change:

```sh
npm run generate:types
```

## Boundaries

- The memory executors are contract fixtures, not UE semantic models.
- The SDK does not mutate Unreal assets.
- UE-backed target resolution, Reflection, Palette execution, graph legality,
  transactions, compile, and save remain Bridge work.
- SAL is not a Blueprint replacement language or a public Graph IR.
