# pcg

Inspect one authored, asset-backed `UPCGGraph`, including its Nodes, Pins,
persisted layout, Settings evidence, and incident Edges, and edit authored
Graph structure, Settings values, layout, and connectivity. Query reads are
read-only; Patch mutations apply inside one top-level transaction.

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

## Patch

`pcg` is a Patch Target for authored PCG Graph edits:

```sal
patch forest [dry run]
sample = { palette: "P_PrintString" }
add sample
set @SurfaceSampler_0.Enabled = true
reset @SurfaceSampler_0.PointRadius
move @SurfaceSampler_0 to (320, 0)
connect @SurfaceSampler_0/out/Points -> @Noise_0/in/Points
remove @Obsolete_0
```

Node creation is Palette-backed: `add` references the exact opaque Palette id
discovered through the read-only `palette entries` Query for the same Target,
and the created Node returns its live serialized identity. Every authored
change is a planned effect with dry-run/live parity. A dry run shares the
ordered plan without touching the live Graph; live apply runs inside one
top-level transaction with `Modify` and native post-edit notification, and a
later failure rolls back the complete authored Graph snapshot.

`set`/`reset` target exact Node member fields on the graph-owned Settings and
notify through the Node's public change path so the owning Node rebuilds Pins.
External Settings assets and Settings-instance wrappers stay read-only.
`connect`/`disconnect` resolve exact output/input Pin refs; incompatible,
occupied, cyclic, or missing-edge edits fail closed before mutation. The Graph
default input and output Nodes are protected from removal.

Persistence is an independent terminal statement:

```sal
patch forest
save
```

`save` must be the only statement in its Patch. It persists only the
outermost package that owns the exact Graph Target through a
source-control-aware package save and never saves a related subgraph or
external Settings asset. A save dry run is advisory: it reports the dirty or
clean closure plan without executing native `PreSave` or writing disk, and a
clean closure is a valid no-op.

## Unavailable

`break`, Parameter migration, external Settings mutation, execution,
generation, cancellation, and live component configuration remain
unavailable. Runtime Component configuration belongs to
`pcg_component`; live execution does not become a Graph Query or Patch
operation.
