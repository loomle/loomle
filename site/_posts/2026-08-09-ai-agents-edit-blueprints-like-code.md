---
layout: loomle-post
title: How we designed a language that lets AI agents edit Blueprints like code
subtitle: "Inside Structured Agent Language (SAL): compact context, stable identity, grounded discovery, composable patches, and precise feedback for Unreal Engine."
description: How Structured Agent Language gives AI agents a compact, stable, discoverable, and verifiable way to edit Unreal Engine Blueprints.
summary: "SAL gives agents the missing working surface for Blueprint editing: compact object text, stable references, grounded discovery, ordered patches, and compiler-like feedback."
date: 2026-08-09 00:00:00 +0800
author: Loomle Lab
reading_time: 10 min read
kicker: SAL / BLUEPRINTS
cover: /assets/images/blog/sal-blueprints-like-code-cover.webp
cover_alt: A Blueprint-like node graph passing through a loom and becoming a compact structured language
image: /assets/images/blog/sal-blueprints-like-code-cover.webp
permalink: /blog/ai-agents-edit-blueprints-like-code/
nav_exclude: true
---

Code agents work because code gives them a compact working surface. They can search a symbol, inspect a small region, make a patch, run the compiler, and use the error to make the next decision.

A Blueprint graph offers none of that by default. Its structure is spread across Unreal objects, visual layout, Pins, links, generated classes, and editor state. A screenshot shows appearance but loses identity and type information. A raw object dump preserves detail but spends the context window before the agent reaches the actual problem.

We built Structured Agent Language (SAL) to give agents a better surface: a small language for querying, representing, and patching Unreal objects without replacing Unreal's own semantics.

## Blueprints aren’t hard for agents. Guessing is.

When a coding agent changes a function call, it does not infer the parameter types from the pixels on your screen. It reads source, resolves symbols, asks the language server, and lets the compiler reject invalid assumptions.

Blueprint editing often forces an agent in the opposite direction. A visible label such as `Delay` is not enough to identify a Node. A Pin caption is not a stable Pin identity. A position in a screenshot cannot tell the agent which connection owns which endpoint. Even if the intended change is obvious to a person, the executable details are not.

Consider the request: “Insert a Delay after `BeginPlay`, then reconnect the existing execution flow.” To do that safely, the agent needs to know:

- which Blueprint and Graph are active;
- the exact identity of the `BeginPlay` Node and its output Pin;
- the exact creation capability for the correct Delay Node;
- the Pins created by that capability;
- the existing downstream connection; and
- whether the complete change is valid before it touches the asset.

If any of those facts is guessed, the edit is probabilistic. The first design goal for SAL was therefore not “support more Blueprint actions.” It was “remove every reason the agent has to guess.”

That is the same foundation that makes code agents useful. Source text, symbol identity, type information, available APIs, patches, and compiler feedback all constrain the next action. Blueprint agents need equivalent constraints.

## Blueprint agents need a language, not more tools.

Coding agents do not need a tool for every syntax construct. A programming language lets them find a symbol, inspect its type, modify it, and interpret compiler feedback without changing representations at every step.

Blueprint edits need the same continuity. One change may involve Graph discovery, Pin identity, Node creation, connections, validation, and compilation. If each step uses an unrelated tool schema, the agent must preserve the plan and translate state between calls.

SAL puts that plan in one model. Query Text reads Unreal state. Result Text returns canonical Targets and Object Text. Patch Text expresses ordered changes. They share the same fields, identities, references, and operations.

`Structured` means a closed grammar and explicit scope. `Agent` means selective queries, copyable results, and diagnostics that support correction. `Language` means Results can feed later Queries and Patches, while Patch statements can use objects created earlier in the same change.

MCP transports the request. SAL expresses the edit.

## SAL lets agents read Blueprints with the same focus as code

A code agent rarely loads an entire repository into context. It searches first, then reads the function, type, or call chain needed for the current decision.

SAL applies that pattern to Unreal objects. The agent binds one Target and asks one focused question:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door"
}

query door
graphs
```

The result gives the exact Graph Target. A second query can follow only the execution flow around the relevant Node:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "87d103e6-7c54-4c39-928f-b9bc2a7846d1",
  id: "3f27697b-574d-4381-a3ea-ef118e97bb44"
}

query eventGraph
exec flow from pin @c54137a2-e4b8-49e5-a61f-c1a0c94146ef/7d22b594-714f-4560-9c3e-370bd557e634 depth 4
```

Result Text is compact Object Text, not a generic JSON mirror of every Unreal property:

```sal
result exact_target
target eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "87d103e6-7c54-4c39-928f-b9bc2a7846d1",
  id: "3f27697b-574d-4381-a3ea-ef118e97bb44"
}
objects

beginPlay = node {
  id: "c54137a2-e4b8-49e5-a61f-c1a0c94146ef",
  type: "/Script/BlueprintGraph.K2Node_Event",
  title: "Event BeginPlay",
  at: (0, 0)
}

beginPlay.then = pin {
  id: "7d22b594-714f-4560-9c3e-370bd557e634",
  direction: out,
  category: "exec"
}

beginPlay.then -> initialize.execute
```

The representation is small enough to reason over, but the query surface remains flexible. The agent can ask for a summary, a collection, an execution flow, a local context, exact layout geometry, references, or schema when the task requires it.

That separation matters. Compactness should not mean flattening the object until useful information disappears. It should mean retrieving the smallest faithful view that supports the next decision, just as a code agent reads a function instead of serializing the entire compiler AST.

## StableRefs give Unreal objects the stability of code symbols

Code agents can carry a symbol from search results into later reads and edits. Blueprint labels cannot provide the same guarantee. Users rename Nodes, duplicate them, move them, and reuse the same display names throughout a Graph.

SAL uses the native owner chain and Unreal identity to create Target-relative StableRefs:

```sal
@c54137a2-e4b8-49e5-a61f-c1a0c94146ef
@c54137a2-e4b8-49e5-a61f-c1a0c94146ef/7d22b594-714f-4560-9c3e-370bd557e634
```

The first reference identifies a Node inside the exact Graph Target. The second identifies one of that Node's Pins. They do not depend on a label, screen position, result order, or a hidden session handle.

Every SAL request is self-contained. A later call repeats the complete Target and copies the StableRefs it needs:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "87d103e6-7c54-4c39-928f-b9bc2a7846d1",
  id: "3f27697b-574d-4381-a3ea-ef118e97bb44"
}

query eventGraph
@c54137a2-e4b8-49e5-a61f-c1a0c94146ef
with schema
```

This is deliberately closer to applying a patch against named code than clicking whatever happens to be at a coordinate. The request can be reviewed on its own, and stale identity produces an explicit failure instead of silently retargeting the change.

## Schema, References, and Palette make Blueprints as discoverable as code

A code agent does not invent a method name and hope the library implements it. It searches definitions, reads types, finds references, and completes against APIs that exist in the current project.

Blueprint agents need three equivalent discovery surfaces.

**Schema** answers what this exact object supports now. Static interface cards describe a Domain, while dynamic schema resolves fields, constraints, and operations against a concrete Unreal object:

```sal
query eventGraph
@c54137a2-e4b8-49e5-a61f-c1a0c94146ef
with schema
```

**References** answer where an object is used. Stable identity remains intact across the query, so the result can guide a later read or edit without matching display strings:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "87d103e6-7c54-4c39-928f-b9bc2a7846d1"
}

query door
references to @63f4d584-6f1d-45ef-a4ab-a1cf21c5bd76 in project
```

**Palette** answers what can be created in the current target context. The agent searches by intent:

```sal
query eventGraph
palette entries "Delay"
```

Then it asks for the exact entry and its creation schema:

```sal
query eventGraph
palette @1d31bda7-e19f-4ab8-a28a-b03e7cb24a3c
with schema
```

The returned Palette object contains the copyable creation fields and the capability's current constraints. The agent does not need to guess a C++ class, fabricate Pins, or assume that a Node available in one Graph is valid in another.

Together, Schema, References, and Palette play the role that type information, find-references, and code completion play in a coding environment. Without them, a Blueprint agent is forced to hallucinate the editable surface before it can even begin the edit.

## Ordered Patch lets agents edit Blueprints the way they edit code

A useful code patch expresses a coherent change, not a bag of unrelated clicks. Later lines can depend on symbols introduced earlier, and the reviewer can understand the intended sequence before applying it.

SAL Patch Text does the same for Unreal objects. Local aliases bind objects created inside the Patch, and statements execute in written order:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "87d103e6-7c54-4c39-928f-b9bc2a7846d1",
  id: "3f27697b-574d-4381-a3ea-ef118e97bb44"
}

patch eventGraph dry run
delay = { palette: "1d31bda7-e19f-4ab8-a28a-b03e7cb24a3c" }
insert @c54137a2-e4b8-49e5-a61f-c1a0c94146ef/7d22b594-714f-4560-9c3e-370bd557e634 ->
  delay.execute / delay.then ->
  @d85a0c52-6ea0-4632-bda7-b93efbf5a5e3/824daea6-108d-4b42-ab4d-04e281475c59
move delay to (480, 32)
```

The exact Graph interface determines which connection or insertion operations are available. The important property is composability: `delay` names the object created earlier in the same Patch, so later statements can place it and connect it without waiting for another round trip to discover its generated identity.

The complete Patch is parsed, resolved, validated, and planned before live mutation begins. That gives the agent one reviewable unit of intent, similar to a source diff, while Unreal remains responsible for the native operation and transaction semantics.

This also prevents a common failure mode in tool-heavy protocols. When every small action is a separate remote call, the asset can be left half-edited if call four fails after calls one through three succeeded. An ordered Patch lets the interface judge the change as a whole before applying it.

## Dry run gives Blueprint edits a compiler-like feedback loop

Code agents are effective partly because failure is useful. A compiler does not merely say “the edit failed.” It identifies the file, symbol, type, and constraint that must change next.

`patch eventGraph dry run` follows the real edit path through parsing, identity resolution, validation, and planning, then stops before changing authored state. If a Palette entry is stale, a StableRef no longer resolves, or an operation is unavailable in this Graph, the diagnostic should point the agent back to the exact discovery step it needs.

For example, a stale creation capability should produce guidance equivalent to:

```sal
# invalid: palette entry is not available in the current graph context
# next: query eventGraph
#       palette entries "Delay"
```

The agent can refresh the Palette result, replace the stale value, and run the same Patch again. When the plan is valid, it removes `dry run` and applies the authored text:

```sal
patch eventGraph
delay = { palette: "6cb32e5f-b129-46ca-a70d-dd65ce8ad14f" }
insert @c54137a2-e4b8-49e5-a61f-c1a0c94146ef/7d22b594-714f-4560-9c3e-370bd557e634 ->
  delay.execute / delay.then ->
  @d85a0c52-6ea0-4632-bda7-b93efbf5a5e3/824daea6-108d-4b42-ab4d-04e281475c59
move delay to (480, 32)
```

Apply is not the end of the loop. The agent reads the changed Graph again, then uses the independently returned Blueprint Target to compile and save when the workflow requires it:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "87d103e6-7c54-4c39-928f-b9bc2a7846d1"
}

patch door
compile
save
```

Dry run is therefore more than a safety switch. It gives Blueprint editing the iterative error-and-correction loop that coding agents already rely on.

## SAL unifies the complete coding-agent workflow for Blueprints

A coding agent works across source text, symbols, types, patches, and compiler feedback without rebuilding its model of the program after every step. SAL provides that continuity for Blueprints.

Query returns Object Text and StableRefs. Schema, References, and Palette ground the next decision in the open Unreal project. Patch reuses the same identities, dry run validates the proposed change, and apply returns new Object Text for the next round.

`Query → Object Text → Discovery → Patch → Dry run → Apply → Object Text`

This shared model is what makes SAL a language rather than syntax attached to a collection of MCP tools. A human can review it, an agent can revise it, and Unreal can validate and execute it.

SAL does not convert Blueprints into source code. It gives agents a persistent, queryable, and patchable surface over Unreal objects, leaving more context for the Blueprint logic itself.

You can try SAL with [Loomle MCP](/install.html), read the [SAL working model](/concepts/sal.html), or inspect the source on [GitHub](https://github.com/loomle/loomle).
