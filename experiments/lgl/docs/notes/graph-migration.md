# Graph Migration Notes

## Intent

This note tracks differences between the current graph-first implementation and
the target graph module design in `docs/modules/graph.md`.

The module document should stay focused on the final shape. This note exists so
implementation work can still audit the migration from parser, formatter,
schema, fixtures, and examples.

## Current Headers

The current parser accepts graph-first document headers:

```lgl
graph blueprint("/Game/BP_LGLExample"/EventGraph)
query blueprint("/Game/BP_LGLExample"/EventGraph)
patch blueprint("/Game/BP_LGLExample"/EventGraph)
palette blueprint("/Game/BP_LGLExample"/EventGraph)
```

Target canonical text uses statement-list bindings:

```lgl
bp = asset(path: "/Game/BP_LGLExample.BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
```

## Current Node Readback

Current:

```lgl
delay@A002: Delay({Duration: 1.0})
```

Target:

```lgl
delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0)
```

The old `alias@id: Type(...)` form should not remain as permanent sugar.

## Current Pin Readback

Current:

```lgl
delay.Duration: float in {1.0, anchor: [320, 72]}
```

Target:

```lgl
delay.Duration = pin(type: float, direction: in, value: 1.0, anchor: [320, 72])
```

## Current Palette Binding

Current:

```lgl
Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})
```

Target:

```lgl
DelaySource = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")
delay = node(graph: g, source: DelaySource, Duration: 1.0)
```

## Current Query

Current:

```lgl
query blueprint("/Game/BP_LGLExample"/EventGraph)

find nodes where type = PrintString with pins, defaults
```

Target:

```lgl
query g
find nodes
where type = PrintString
with pins, defaults
```

Next query syntax keeps `where` as a single condition expression:

```lgl
query g
find nodes
where type = PrintString and name ~= "Print"
with pins, defaults
order by name asc
limit 10
```

Old single-line query sugar should migrate from repeated or implicit clauses
to the shared query form:

```lgl
query g find nodes where type = PrintString and name ~= "Print" with pins, defaults
```

## Current Patch Chains

Current:

```lgl
connect begin.Then -> print.Exec
insert begin.Then -> delay.Exec/Completed -> print.Exec
```

Target canonical text:

```lgl
connect(begin.Then, print.Exec)
insert(delay, from: begin.Then, to: print.Exec, input: delay.Exec, output: delay.Completed)
```

The `->` forms remain accepted sugar.

## Schema Migrations

Planned graph schema changes:

```txt
Node.layout.at        -> Node.at
Node.layout.size      -> Node.size
Pin.layout.anchor     -> Pin.anchor
Connect.chain         -> Connect.edge
Insert.chain          -> Insert.from / Insert.to / Insert.input / Insert.output
node creation binding -> node(..., source: PaletteBindingName, ...)
```

## Implementation Checklist

- Update schema fixtures for direct node and pin readback metadata.
- Update parser to parse statement-list graph bindings.
- Add normalizer pass for graph sugar to canonical text.
- Update formatter to emit canonical graph text.
- Update patch binding parsing to support `node(..., source: ...)`.
- Update `connect` JSON from chain to edge.
- Update `insert` JSON from chain to explicit `from`, `to`, `input`, and
  `output`.
- Update examples from graph-first headers to canonical statement-list form.
