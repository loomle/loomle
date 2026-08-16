---
layout: default
title: Interfaces
nav_order: 6
has_children: true
description: The nine active Loomle 0.7 SAL interface modules, their UE ownership boundaries, and their Query or Patch availability.
permalink: /tools/
---

# Interfaces

Interfaces describe the UE objects and operations carried through `sal_query`
and `sal_patch`. They are separate from the Client's
[eight public MCP calls](../calls/).

Loomle 0.7 has nine active interface modules:

| Domain | Owns | Canonical exact Target | Access |
| --- | --- | --- | --- |
| [Asset](asset.html) | Asset Registry discovery and exact package save | Asset Object Path plus verified native Class | Query + Patch |
| [Blueprint](blueprint/) | Class Settings, declarations, Graph lifecycle, SCS Components, compile, and save | Asset Path plus Blueprint Guid | Query + Patch |
| [Class](blueprint/class.html) | Reflection and effective Class Defaults | native Class Path | Query + Patch |
| [Graph](blueprint/graph.html) | Nodes, Pins, Edges, flow, Palette-backed creation, and layout | Asset Path + Blueprint Guid + Graph Guid | Query + Patch |
| [StateTree](state-tree.html) | Authored hierarchy, Nodes, Transitions, Parameters, Bindings, compile, and save | exact StateTree Asset Path and Class Path | Query + Patch |
| [Widget](widget/) | Authored Widget tree, Slot state, and structural edits | Asset Path + WidgetBlueprint Guid | Query + Patch |
| [Level](level.html) | Persistent source-map Actors and serialized Components | map Asset Path plus verified native World Class | Query only; no `PatchTarget` |
| [PCG](pcg.html) | Asset-backed PCG Graph Nodes, Pins, Settings evidence, Edges, and persisted layout | PCG Graph Asset Path plus verified native Class | Query only; no `PatchTarget` |
| [PCG Component](pcg-component.html) | Persistent PCG configuration and Graph Parameters on one authored Level Component | map + ActorGuid + Component source and slot + native Class | Query only; no `PatchTarget` |

These nine names are the closed values of structural `Target.domain`. They are
not semantic tags, object kinds, or StableRef prefixes. All nine are valid
Query Targets and canonical Result Targets. `level`, `pcg`, and
`pcg_component` are not admitted to `PatchTarget` in this release; the SAL
parser rejects a Patch for them before Bridge dispatch.

## Use the Installed Contract

The website explains concepts, ownership, and workflows. The Client's embedded
interface card is the precise contract that matches the installed build:

```text
sal_schema({})
sal_schema({ module: "graph" })
```

Use exact dynamic-schema discovery for fields, constraints, and UE operations
that depend on one resolved object or Palette capability.

## Common Query Shape

```sal
<alias> = target { domain: <domain>, ... }

query <bound-target>
[one primary operation]
[where <condition>]
[with <detail>]
[order by <field> asc|desc]
[page limit <count>]
[page after "<cursor>"]
```

Every Query is self-contained. Collections provide orientation and discovery;
exact reads provide complete current state and, where supported, dynamic
schema.

The shared factual-reference operation is:

```sal
references to <exact-subject>[.<native-member-path>] [in project]
```

Scope and project-wide support depend on the selected Domain. The first public
result block is canonical Result Text: it declares its context and Target
table, then carries ordered Object Text under `objects` or ends with
`no_objects`.

## Common Patch Shape

Patch applies only to the six Domains admitted to `PatchTarget`.

```sal
<alias> = target { domain: <domain>, ... }

patch <bound-target> [dry run]
<ordered binding or operation>
<ordered binding or operation>
```

Core operations and module-specific extensions include:

```sal
add
remove
set
reset
move
invoke
save

connect
bind
wrap
compile
```

Every object created directly through an add operation starts from a Palette
capability in the real target context.

## Ownership and Handoffs

One authored Patch belongs to one Domain. A Target is flat and never contains
or composes another Target. Cross-Domain work uses independent related Targets,
explicit handoffs, and following requests.

Graph and Widget authored changes finalize through their owning Blueprint.
StateTree compiles and saves through its asset target. The Query-only Level,
PCG, and PCG Component interfaces expose only the related Targets and handoffs
stated on their pages. Each interface page states its own handoff and
finalization boundary.

See [Diagnostics](diagnostics.html) for result health and compiler information.
