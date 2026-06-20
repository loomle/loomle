# LGL Overview

## Intent

LGL is an agent-facing, line-oriented object language for Unreal Engine work.
It should let agents describe, query, and patch UE objects with compact text
while keeping the bridge implementation on a strict normalized JSON contract.

LGL text is for agents. Normalized JSON is for the bridge, schema validation,
RPC, generated types, and C++ codecs. The two forms should stay mechanically
convertible, but agents should not have to write JSON by hand.

The implemented experiment is graph-first. Its parser, formatter, schema,
examples, and in-memory adapter use these top-level object forms:

```ts
type LglObject = Graph | Query | Patch | Palette;
```

The next LGL design keeps that implementation as the factual baseline and
organizes future work by domain modules. Graph, asset, widget, and future
modules each own their object forms, sugar, canonical text, normalized object
model, queries, patches, diagnostics, and examples.

## Why Text

JSON is precise, but it is not the best primary interface for an agent that is
reading, composing, and patching UE objects. LGL keeps the agent-facing surface
closer to normal code and command output:

```lgl
bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
begin.Then -> delay.Exec/Completed -> print.Exec
```

The bridge still receives normalized JSON after parsing and validation. This
keeps the runtime contract strict without making agent output verbose.

## Representation Layers

LGL has three representation layers:

```txt
sugar text
  -> canonical text
  -> normalized JSON
```

Sugar text is optional agent convenience. It must normalize without target
state or UE schema knowledge. Existing examples already use graph edge sugar
such as `begin.Then -> delay.Exec/Completed -> print.Exec`.

Canonical text is the explicit text form that can be translated mechanically
into normalized JSON. A module may still choose to display sugar in query
results when sugar is the clearest agent-facing form.

Normalized JSON is the bridge and schema contract. It is used for validation,
RPC, generated types, and C++ codecs. Agents should not have to write it.

Example:

```lgl
begin.Then -> delay.Exec/Completed -> print.Exec
```

normalizes to canonical graph readback text:

```lgl
edge(begin.Then, delay.Exec)
edge(delay.Completed, print.Exec)
```

and then to normalized JSON:

```json
[
  {
    "kind": "edge",
    "from": { "node": "begin", "pin": "Then" },
    "to": { "node": "delay", "pin": "Exec" }
  },
  {
    "kind": "edge",
    "from": { "node": "delay", "pin": "Completed" },
    "to": { "node": "print", "pin": "Exec" }
  }
]
```

In patch context, the same chain-shaped sugar normalizes to operation calls:

```lgl
connect begin.Then -> print.Exec
```

normalizes to:

```lgl
connect(begin.Then, print.Exec)
```

The sugar layer may stay highly readable; the canonical layer must be explicit
enough to translate mechanically into normalized JSON.

## Document Organization

The docs are split by the same boundaries the implementation should use:

- [`LANGUAGE_CORE.md`](LANGUAGE_CORE.md): shared statement, expression, value,
  binding, constructor, reference, and sugar rules.
- [`MODULES.md`](MODULES.md): how domain modules define syntax and object
  contracts.
- [`modules/graph.md`](modules/graph.md): graph syntax from sugar to canonical
  text to normalized JSON.
- [`modules/asset.md`](modules/asset.md): asset discovery, identity, registry
  metadata, and asset query results.
- [`modules/blueprint.md`](modules/blueprint.md): Blueprint class contract,
  member declarations, custom events, and component tree structure.
- [`modules/widget.md`](modules/widget.md): widget tree object text, widget
  hierarchy, slots, and widget patching.
- [`notes/graph-migration.md`](notes/graph-migration.md): implementation
  migration notes for the current graph-first parser, formatter, schema, and
  examples.

Some remaining documents still describe the current graph-first experiment.
They should be treated as implementation context until the next language pass
updates code and examples.

## Replacement Map

The new module-oriented docs should replace older graph-first docs in stages:

| Current document | Replacement target | Status |
| --- | --- | --- |
| `OBJECT_MODEL.md` graph sections | `modules/graph.md` normalized JSON section | current schema reference |
| `SDK_DESIGN.md` | future runtime docs | current SDK reference |

The replacement target must preserve implemented names and semantics unless a
future schema migration explicitly changes them.
