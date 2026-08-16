# pcg_component

Inspect persistent PCG configuration on one exact authored `UPCGComponent`
owned by a saved source Level. This interface is read-only and does not expose
live execution or generated resources.

## Target

Every Query uses the complete exact Target:

```sal
forestComponent = target {
  domain: pcg_component,
  asset: "/Game/Maps/Forest.Forest",
  actorId: "11111111-1111-1111-1111-111111111111",
  source: "native",
  id: "PCGComponent",
  type: "/Script/PCG.PCGComponent"
}
```

`asset` is the owning saved source map, `actorId` is its persistent ActorGuid,
and `source + id` is the exact Level Component slot. `source` is `native`,
`scs`, or `instance`. Resolution reuses the Level Domain's source proof and
requires one loaded, original authored Component. UCS, transient, generated,
debug, cleanup, local-partition, preview, ambiguous, and unloaded-owner
Components have no Target.

## Identity

A Graph Parameter uses the non-zero canonical lowercase descriptor Guid from
the terminal top Graph's native Property Bag:

```sal
@22222222-2222-2222-2222-222222222222
```

Parameter name, authored order, type, value, and override source are not
identity. Rename preserves the reference; removal invalidates it. The adapter
audits the complete bounded Graph-interface and Property Bag chain before
publishing any Parameter StableRef.

## Query

```sal
target [with schema]
summary
parameters ["text"]
@parameter-guid [with schema]
```

`target` returns the exact persistent Component facts. `summary` additionally
returns the direct `graphInterface`, terminal top `graph`,
`graphBindingKind`, and `graphBindingComplete`. A complete unbound Component
has null Graph fields and an empty Parameter collection. An incomplete,
cyclic, unsupported, or over-budget chain fails Parameter Query closed rather
than returning partial identity or values.

`parameters` accepts optional case-insensitive text search and cursor
pagination only:

```sal
query forestComponent
parameters "density"
page limit 25
page after "<cursor>"
```

Search covers descriptor Guid, name, value-type token, container tokens,
type-object path, and effective-source token. Canonical descriptor-Guid order
is fixed. The default page limit is 50 and the maximum is 200. Cursors are
bound to the exact Component Target, complete Graph-interface and Parameter
snapshot, search, page limit, override bits, values, and effective sources.
`where`, `with`, and `order by` are unavailable on the collection.

Exact Parameter Query accepts only optional `with schema`; `summary` accepts
no clauses. Target schema and Parameter schema explicitly advertise no Patch
operations.

## Parameter Values

Each Parameter returns its descriptor identity and type, local override state,
effective source, and lossless value status:

```sal
forestComponent.Parameters.Density = parameter {
  id: "22222222-2222-2222-2222-222222222222",
  name: "Density",
  type: {
    valueType: double,
    containerTypes: [],
    valueTypeObject: null
  },
  valueStatus: available,
  overridden: true,
  localValue: 0.35,
  effectiveValue: 0.35,
  effectiveSource: component_override,
  stableRefAvailable: true,
  ref: @22222222-2222-2222-2222-222222222222
}
```

`localValue` is present only for an available Component override. Effective
source is `component_override`, `parent_instance`, or `graph_default`.
Certified scalar kinds are `bool`, `byte`, `int32`, `uint32`, `int64`,
`uint64`, `float`, `double`, `name`, `string`, and `enum`. Int64 and UInt64
use exact decimal strings; non-finite floating values use `nan`, `+infinity`,
or `-infinity`; enum value is an exact decimal string. Recognized descriptor
shapes whose values are not certified remain descriptor-only with
`valueStatus: unsupported` and `effectiveValue: null`. A native shape that the
closed three-field type record cannot represent, including a UE 5.8 Map key
shape, fails the complete Parameter snapshot closed instead of dropping key
facts.

String-like values are limited to 8 Ki UTF-16 code units each and 64 Ki in
aggregate. The snapshot accepts at most 4,096 Parameters and 32 loaded
Graph-interface links. Invalid, ambiguous, misaligned, stale, unsafe, or
over-budget evidence fails atomically; it is never truncated or guessed.

## Handoffs And Read-only Boundary

Every successful result retains the owning `level` Target through
`handoff inspect_level`.
`summary` also emits `handoff inspect_graph` when the terminal Graph is an
independently canonical asset-backed `pcg` Target. These are navigation for
later independent Queries, not shared authority.

`pcg_component` accepts no Patch Target. Graph assignment, Parameter override
mutation, schema mutation, save, generation, cleanup, cancellation, task
state, managed resources, messages, and inspection are unavailable. Level
owns Component containment and persistence; `pcg` owns Graph-authored state;
live PCG execution remains a separate typed frontend.
