---
layout: default
title: Quickstart
nav_order: 3
description: Check status, bind a project, inspect a Blueprint, dry-run a SAL patch, apply it, and finalize the asset.
---

# Quickstart

This guide completes one read-and-edit workflow. It assumes that the matching
Loomle 0.8 plugin is installed, `LoomleBridge` is enabled, the MCP host launches
the bundled Client, and the target Unreal project is open.

{: .note }
> Paths and identities below are examples. Always copy flat Targets,
> Target-relative StableRefs, Palette entries, and invocation templates
> returned by the current project. Target Guid fields are canonical lowercase,
> hyphenated, and non-zero.

## 1. Check Status

Call once before the first Loomle operation:

```text
status({})
```

This identifies the running Client, checks for an update, and reports the
current session and Bridge health. An unavailable update check does not block
local Loomle work.

## 2. Bind One Project

If Status reports an unbound session, inspect the current session and known
projects:

```text
project({})
```

If Loomle reports no bound project or more than one candidate, bind the desired
one:

```text
project({ projectId: "<returned-project-id>" })
```

The binding is session-local and sticky. If the Editor restarts or the project
goes offline, Loomle preserves the same project intent.

## 3. Load a Matching Workflow Skill When Needed

The `agent_skill` tool description exposes the resident Skill names and trigger
descriptions automatically. A general read-and-edit request does not require a
Skill call. When the task matches specialized guidance, load it before planning
that work:

```text
agent_skill({ name: "format-unreal-blueprints" })
```

The returned `SKILL.md` and references guide use of the existing Loomle tools.
Users do not install a separate Skill in the MCP host.

## 4. Start From the Editor

Open or select the asset you want to discuss, then call:

```text
editor({})
```

The first result block is canonical SAL Result Text. It declares
`exact_target`, `domain_root`, or `unresolved_target`, includes an explicit
Target table when resolved, and then carries `objects` or terminates with
`no_objects`. Use the returned Target instead of guessing from the visible UI.

## 5. Discover the Interface

List the active interface modules:

```text
sal_schema({})
```

Load the Blueprint card when exact domain syntax is unfamiliar:

```text
sal_schema({ module: "blueprint" })
```

The `sal_schema` tool description already carries the compact resident SAL
guide. Static module cards are for exact domain boundaries; they are not a
prerequisite before every request.

## 6. Read a Blueprint Summary

Send one self-contained Query Text to `sal_query`:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door"
}

query door
summary
```

The first path-based query returns the Blueprint id and compact counts for
Variables, Dispatchers, Graphs, and Components. It does not download the
complete Blueprint.

Use the returned id in later exact requests:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

query door
graphs "Event"
```

Every Query and Patch supplies exactly one flat Target binding. The Target Guid
verifies the object opened by the Asset Path; it is separate from contained
StableRefs.

## 7. Follow the Graph Locally

Bind the exact Graph returned by the Blueprint query:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
exec flow from @returned-node-guid depth 2
```

Flow queries return compact Nodes and only the Pins needed to express the
Edges. Query an exact Node reference when an operation needs every current Pin
or dynamic schema.

## 8. Discover Before Creating

Never guess Node creation fields. Search the target Graph Palette:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
palette entries "Print String"
```

Inspect the selected entry with exact schema:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
palette @returned-palette-entry-id
with schema
```

Then copy its returned brace object fields into a Patch. A displayed tag such
as `node` is erasable and does not select creation behavior.

## 9. Dry Run

Send the complete Patch Text to `sal_patch` with dry-run state on the Patch
header:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

patch eventGraph dry run
print = { palette: "returned-palette-entry-id" }
add print
```

Dry run parses, resolves, validates, and plans through the real edit path
without changing live authored state. Review the returned diagnostics,
resolved references, operations, effects, and diff.

## 10. Apply and Read Back

If the dry run is valid, send the same authored Patch again with dry-run state
removed from the header. Afterward, query the affected Graph or exact Node
again rather than assuming that the intended result was applied.

## 11. Finalize Through the Owner

Graph edits do not compile or save their owning Blueprint automatically.
The Graph result returns an independent related Blueprint Target and names it
with an explicit compile handoff. Copy that returned Target into a separate
terminal Patch:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

patch door
compile
save
```

The first response block remains canonical Result Text. Native UE compiler
messages remain factual comments in that Result Text. Mutation metadata and
structured validation or execution diagnostics use later independent
SAL-comment blocks.

You have now completed the standard Loomle loop:

```text
status → [load matching Skill] → bind → locate → inspect → discover → dry run → apply → read back → finalize
```

Continue with [Core Concepts](concepts/) or browse the
[Interfaces](tools/).
