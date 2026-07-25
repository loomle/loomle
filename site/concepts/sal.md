---
layout: default
title: SAL Working Model
parent: Core Concepts
nav_order: 1
description: How SAL Query Text, Patch Text, and canonical Result Text work together.
---

# SAL Working Model

SAL is Loomle's agent-facing, line-oriented language for reading and changing
Unreal Engine objects.

It is intentionally small. UE Asset Paths, Class Paths, types, field names,
values, Palette capabilities, and diagnostics remain native rather than being
translated into a generic replacement model.

## Objects, Tags, and Calls

Ordinary object data always uses braces:

```sal
{
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_Event"
}
```

An optional semantic tag can make the same data easier to read:

```sal
node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_Event"
}
```

Removing `node` cannot change type, identity, Domain, validation, or creation.
`target { domain: ... }` is separate structural syntax, not an object or tag.
JSON literals, the retired generic label `object`, `target`, `domain`, the six
Domain names, and the structurally ambiguous `tree`, `context`, and `palette`
words are reserved; they cannot be semantic tags or local aliases.
Parentheses remain for true calls or explicitly defined non-object syntax:

```sal
invoke @node-guid Rename(displayName: "New Name")
```

## Three Text Forms

### Query Text

A Query binds a target and selects one primary read:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door"
}

query door
summary
```

Optional clauses depend on the selected interface and primary operation:

```sal
where <condition>
with <detail>
order by <field>
page limit <count>
```

Depth is an inline argument of operations that define it, for example
`tree depth 20`, `context @identity depth 2`, or a Graph flow query. It is not
a standalone Query clause.

### Patch Text

A Patch binds its complete target and contains ordered bindings or operations:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

patch door dry run
set door.BlueprintDescription = "Interactive door"
```

Core operations and interface-specific extensions include:

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

### Result Text

Queries and Patches return canonical Result Text. It declares the result
context and Target table before its ordered Object Text:

```sal
result exact_target
target eventGraph = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
objects

beginPlay = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_Event"
}

beginPlay.then = pin {
  id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  direction: out,
  type: "<native FEdGraphPinType text>"
}
```

`exact_target`, `domain_root`, and `unresolved_target` are the three result
contexts. When no Object Text exists, the Result Text ends with the strict
terminator `no_objects`.

## Comments and Transport Annotations

Comments may appear as ordered Object Text statements for factual context such
as counts, titles, health, schema, and pagination guidance:

```sal
# variables: 3

###
complete factual detail
###
```

The first MCP text block contains only canonical Result Text. Mutation metadata
and diagnostics use later independent text blocks formatted as SAL comments;
they are never appended to the canonical Result Text block.

## Every Request Is Self-contained

Aliases do not survive between tool calls. Copy the returned facts needed for
the next request and declare its complete Domain Target again.

This boundary makes requests reviewable and prevents hidden conversational
state from retargeting an edit.

## Creation Starts From Palette

Do not guess object fields, classes, Pins, destinations, or operation
parameters. Search a Palette in the real target context:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
palette entries "Print String"
```

Inspect the exact entry when necessary, then copy its returned brace object
fields into Patch Text. An optional semantic tag may make that object easier to
read, but removing the tag cannot change creation behavior.

Use `sal_schema({ module: "<module>" })` for the exact grammar and clauses
supported by one interface.
