# pcg

Inspect one authored, asset-backed `UPCGGraph`, including its Nodes, Pins,
persisted layout, Settings evidence, and incident Edges. This interface is
read-only.

## Target

A discovery Query may omit `type`:

```sal
forest = target {
  domain: pcg,
  asset: "/Game/PCG/PCG_Forest.PCG_Forest"
}
```

The canonical exact Target includes the actual native Graph Class:

```sal
forest = target {
  domain: pcg,
  asset: "/Game/PCG/PCG_Forest.PCG_Forest",
  type: "/Script/PCG.PCGGraph"
}
```

The Target must be an independently saved top-level `UPCGGraph` asset. A
`UPCGGraphInstance`, Component-owned Graph, embedded Graph, or non-PCG asset is
not a `pcg` Target. A valid subclass retains its actual Class Path.

## Identity

PCG uses native string identity rather than fabricated Guids:

```sal
@SurfaceSampler_0
@SurfaceSampler_0/in/Surface
@SurfaceSampler_0/out/Points
@SurfaceSampler_0/in/"Bounding Shape"
```

A Node uses its serialized UObject `FName`. A Pin uses its owner Node name,
native `in` or `out` direction, and exact native Label. Pin Labels containing
punctuation remain one quoted identity segment. Node title, Settings Class,
Pin type, local result key, and semantic tag are not identity. Edges are exact
relationships between Pins and have no object or StableRef identity.

## Query

```sal
target [with schema]
summary
nodes ["text"] [with layout]
@identity [with schema, layout]
```

`summary` returns compact Graph counts, default Input/Output Pin schemas, and
structural diagnostics. `nodes` searches native Node name, computed title,
authored title and comment, Node Class, Settings-interface Class, and effective
Settings Class. It accepts only optional case-insensitive text search,
`with layout`, and cursor pagination:

```sal
query forest
nodes "surface"
with layout
page limit 25
page after "<cursor>"
```

The default limit is 50 and the maximum is 200. Native Graph order is
preserved. A cursor is bound to the exact Target, search, detail, page limit,
Node search evidence, and Pin structure. `where` and `order by` are
unavailable.

`with layout` returns only the persisted integer Node position as `at: [x, y]`.
It does not promise live Slate bounds, Pin geometry, placement anchors, focus,
or an open PCG Editor. Exact Node reads include all current Pins and incident
Edges. Exact Pin reads include their compact owner and incident Edges, including
the opposite endpoints required to express each relationship exactly.

Exact Target, Node, and Pin reads may use `with schema`. The schema is
explicitly read-only. `summary` accepts no clauses; Node and Pin exact reads
accept only `schema` and `layout` details.

## Objects And Settings

A Node returns its actual Node Class independently from its Settings interface:

```sal
surface = node {
  id: "SurfaceSampler_0",
  type: "/Script/PCG.PCGNode",
  title: "Surface Sampler",
  titleOverride: null,
  NodeComment: "",
  SettingsInterface: {
    type: "/Script/PCG.PCGSurfaceSamplerSettings",
    ownership: owned,
    interfaceOwnership: owned,
    effectiveOwnership: owned
  },
  at: [320, 0]
}
```

Pin results preserve native `allowedTypes` and `currentTypes` identifiers
separately; `typeDisplay` is presentation only. Settings projection is bounded
and classifies the interface object and effective Settings independently by
their real Outer chains. An external Settings object or Settings-instance
wrapper remains read-only even when reachable from the Graph.

## Read-only Boundary

`pcg` accepts no Patch Target in this release. Parameters, context and data
flow, Palette, Node creation or removal, Settings mutation, movement,
connection edits, save, execution, generation, cancellation, and inspection
are unavailable. Runtime Component configuration belongs to
`pcg_component`; live execution does not become a Graph Query operation.
