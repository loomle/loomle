---
layout: default
title: Interfaces
nav_order: 6
has_children: true
description: The six active Loomle 0.8 SAL interface modules and their UE ownership boundaries.
permalink: /tools/
---

# Interfaces

Interfaces describe the UE objects and operations carried through `sal_query`
and `sal_patch`. They are separate from the Client's
[seven public MCP calls](../calls/).

Loomle 0.8 has six active interface modules:

| Domain | Owns | Canonical exact Target |
| --- | --- | --- |
| [Asset](asset.html) | Asset Registry discovery and exact package save | Asset Object Path plus verified native Class |
| [Blueprint](blueprint/) | Class Settings, declarations, Graph lifecycle, SCS Components, compile, and save | Asset Path plus Blueprint Guid |
| [Class](blueprint/class.html) | Reflection and effective Class Defaults | native Class Path |
| [Graph](blueprint/graph.html) | Nodes, Pins, Edges, flow, Palette-backed creation, and layout | Asset Path + Blueprint Guid + Graph Guid |
| [StateTree](state-tree.html) | Authored hierarchy, Nodes, Transitions, Parameters, Bindings, compile, and save | exact StateTree Asset Path and Class Path |
| [Widget](widget/) | Authored Widget tree, Slot state, and structural edits | Asset Path + WidgetBlueprint Guid |

These six names are the closed values of structural `Target.domain`. They are
not semantic tags, object kinds, or StableRef prefixes.

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
StateTree compiles and saves through its asset target. Each interface page
states its own handoff and finalization boundary.

See [Diagnostics](diagnostics.html) for result health and compiler information.
