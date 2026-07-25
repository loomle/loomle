---
layout: default
title: Targets and Stable References
parent: Core Concepts
nav_order: 2
description: How flat Targets, native identity paths, member paths, and aliases identify UE objects.
---

# Targets and Stable References

Loomle separates complete target identity from selectors inside that target.
This mirrors how Unreal owns editor objects.

## Asset-backed Targets

An exact StateTree Target uses its Asset Path and verified native Class:

```sal
omle = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Omle.ST_Omle",
  type: "/Script/StateTreeModule.StateTree"
}
```

A Blueprint has both an Asset Path and a native Blueprint Guid. Its first
discovery query may use the path alone:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door"
}
```

Later exact queries and every Patch use the returned id:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
```

The path loads the asset; the Guid verifies that the resolved object is still
the intended Blueprint. Every Target Guid field is canonical lowercase text
with hyphens and must be non-zero, matching UE `FGuid::IsValid()`.

## Owner Chains

Contained UE objects are scoped by their native owner. A Graph Target therefore
carries the identity of its owning Blueprint, but remains flat:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
```

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
```

A Target never contains another Target or an alias. The two bindings above are
independent Targets for two different Domains, shown for comparison rather
than as one request. Every Query or Patch declares exactly one of them.

## Target-relative Stable References

Existing contained objects use native identity paths relative to one exact
Target:

```sal
@node-guid
@node-guid/pin-guid
@variable-guid
@component-guid
@widget-guid
@state-guid
@transition-guid
```

When an id is local to a native owner, that owner identity precedes it:

```sal
@parameter-container-guid/property-guid
@function-graph-guid/local-variable-guid
```

The active Target's Domain card defines the accepted identity paths.

An optional semantic tag may improve readability:

```sal
node @node-guid
pin @node-guid/pin-guid
```

The tag is erasable and cannot select an object kind or disambiguate a
collision. `@node-guid` and `node @node-guid` identify the same object. If two
native categories expose the same id in one Target identity environment,
resolution reports an identity conflict.

## Target Self and Result Scope

The active Target is addressed structurally, not through a fabricated
StableRef:

```sal
query eventGraph
target

query functionGraph
references to target

query interfaceFunctionGraph
references to target.InterfaceGuid
```

The exact `target` read works across Domains. Target-self as a reference
subject is narrower: bare `target` currently requires a callable Function or
Macro Graph, while direct `target.InterfaceGuid` requires a valid Graph
Interface declaration. Other Domain or Graph roles return
`capability.reference_unavailable`.

When Result Text declares related Targets, an unqualified StableRef remains
relative to the main Target. A foreign reference uses the related Target alias:

```sal
bp::@variable-guid
```

That foreign qualifier is result-side structure only. Request text may
redundantly qualify a reference with its own active Target alias; the parser
lowers that spelling to an ordinary unqualified StableRef and rejects foreign
or unknown qualifiers. To follow a related Target, copy it into a new Target
binding and use an unqualified StableRef relative to it.

## Member Paths

Member paths select native fields or nested value surfaces:

```sal
set @widget-guid.Slot.Padding = "<native FMargin text>"
references to @variable-guid.SomeNativeMember in project
```

Only paths advertised by the exact schema are writable. Do not infer a member
path from a display label.

## Local Aliases

Aliases are readable handles inside one SAL Text:

```sal
print = { palette: "palette-entry-id" }
add print
```

They are not persisted session state. A later request binds its own target and
uses returned stable identities.

## Names Are for Discovery

Names and labels help search and navigation, but stable ids or exact paths
carry identity. Query a current name to discover the current native identity,
then use that Target-relative path for exact follow-up work.
