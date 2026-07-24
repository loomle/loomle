---
layout: default
title: Interfaces
nav_order: 6
has_children: true
description: The six active Loomle 0.7 SAL interface modules and their UE ownership boundaries.
permalink: /tools/
---

# Interfaces

Interfaces describe the UE objects and operations carried through `sal_query`
and `sal_patch`. They are separate from the Client's
[six public MCP calls](../calls/).

Loomle 0.7 has six active interface modules:

| Module | Owns | Complete target |
| --- | --- | --- |
| [Asset](asset.html) | Asset Registry discovery and exact package save | exact Asset Object Path |
| [Blueprint](blueprint/) | Class Settings, declarations, Graph lifecycle, SCS Components, compile, and save | Asset Path plus Blueprint Guid |
| [Class](blueprint/class.html) | Reflection and effective Class Defaults | native Class Path |
| [Graph](blueprint/graph.html) | Nodes, Pins, Edges, flow, Palette-backed creation, and layout | exact asset-backed Graph owner chain |
| [StateTree](state-tree.html) | Authored hierarchy, Nodes, Transitions, Parameters, Bindings, compile, and save | exact StateTree Asset Path and Class Path |
| [Widget](widget/) | Authored Widget tree, Slot state, and structural edits | exact WidgetBlueprint locator |

These names organize static documentation. They are not target-routing fields
inside SAL.

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

```text
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

```text
references to <typed-ref>[.<native-member-path>] [in project]
```

Scope and project-wide support depend on the selected interface. Results remain
ordinary Object Text and include the owner bindings needed to interpret each
page independently.

## Common Patch Shape

```text
patch <bound-target> [dry run]
<ordered binding or operation>
<ordered binding or operation>
```

Core operations and module-specific extensions include:

```text
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

One authored Patch belongs to one interface planner. Composed targets may
support queries from several interfaces, but mixed mutation families use
following requests.

Graph and Widget authored changes finalize through their owning Blueprint.
StateTree compiles and saves through its asset target. Each interface page
states its own handoff and finalization boundary.

See [Diagnostics](diagnostics.html) for result health and compiler information.
