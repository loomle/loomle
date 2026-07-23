---
layout: home
title: Loomle
nav_order: 1
description: Read and change Unreal Engine editor objects through a compact, UE-faithful language for agents.
permalink: /
---

# Loomle
{: .fs-9 }

Unreal Engine objects, made readable and editable for agents.
{: .fs-6 .fw-300 }

[Install Loomle 0.7](install.html){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Run the Quickstart](quickstart.html){: .btn .fs-5 .mb-4 .mb-md-0 }

Loomle connects Codex, Claude, and other MCP-compatible agents to a running
Unreal Editor. It expresses Blueprint graphs, Widget trees, StateTrees, Class
Defaults, Assets, and other structured editor state as SAL: compact,
line-oriented text that both an agent and a person can inspect, copy, discuss,
and patch.

Loomle does not replace Unreal's object model. Asset Paths, Class Paths, native
types, field names, Palette capabilities, compiler diagnostics, and editor
semantics remain UE-native.

{: .note }
> Loomle 0.7 is currently an accepted QA build and is not yet published on
> Fab. The public Fab listing still contains Loomle 0.6. Follow
> [Install](install.html) for the exact availability and compatibility status.

## The Working Model

A reliable Loomle workflow has seven steps:

1. Bind the MCP session to one Unreal project.
2. Start from `editor_context` or an exact Asset Path.
3. Query a compact summary, collection, tree, context, or flow.
4. Discover exact schema only when the operation requires it.
5. Use a UE Palette result before creating an object.
6. Dry-run the complete Patch, then apply the same authored change.
7. Compile and save through the owning asset when its interface requires it.

The [Quickstart](quickstart.html) walks through that complete path.

## Five Calls, Six Interfaces

The Client exposes five public MCP tools:

| Call | Responsibility |
| --- | --- |
| `project` | Inspect projects and bind this MCP session to one project. |
| `sal_query` | Execute one self-contained SAL Query Text. |
| `sal_patch` | Dry-run or apply one ordered SAL Patch Text. |
| `sal_schema` | Discover the resident SAL guide and active interface cards. |
| `editor_context` | Read the user's current UE interaction target as SAL. |

The six active SAL interface modules are Asset, Blueprint, Class, Graph,
StateTree, and Widget. The small MCP surface is intentional: rich UE behavior
is expressed in SAL and discovered through interface cards instead of being
registered as hundreds of unrelated tools.

[Understand the MCP calls](calls/){: .btn .fs-4 .mr-2 }
[Browse the interfaces](tools/){: .btn .fs-4 }

## Why SAL

Unreal assets are rich editor objects rather than source files. Raw
serialization is noisy, while generic wrappers tend to erase the details that
make UE behavior reliable. SAL keeps the useful text workflow without creating
a parallel Unreal model:

- summaries and local traversal avoid downloading an entire asset;
- typed stable references support precise follow-up requests;
- query and mutation results share the same ordered Object Text;
- Palette and dynamic schema discovery expose capabilities UE actually offers;
- dry runs use the real parse, resolve, validate, and planning path; and
- native health, validation, and compiler diagnostics remain beside the
  objects and operations that produced them.

Read [Core Concepts](concepts/) for the language and identity model, or go
straight to [Workflows](workflows/) for task-oriented examples.

## Installation Model

Loomle 0.7 ships as one Fab plugin containing both the C++ Unreal Bridge and
the matching self-contained Client:

```text
LoomleBridge/Resources/Loomle/<platform-arch>/loomle(.exe)
```

There is no separate Python server, Node.js runtime, global Loomle
installation, website installer, or project-local Client. Fab owns plugin
installation and updates; the MCP host launches the Client bundled inside that
plugin.
