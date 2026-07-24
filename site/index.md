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

[Download 0.7.0-rc.1](https://github.com/loomle/loomle/releases/tag/v0.7.0-rc.1){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Install](install.html){: .btn .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Quickstart](quickstart.html){: .btn .fs-5 .mb-4 .mb-md-0 }

Loomle connects Codex, Claude, and other MCP-compatible agents to a running
Unreal Editor. It expresses Blueprint graphs, Widget trees, StateTrees, Class
Defaults, Assets, and other structured editor state as SAL: compact,
line-oriented text that both an agent and a person can inspect, copy, discuss,
and patch.

Loomle does not replace Unreal's object model. Asset Paths, Class Paths, native
types, field names, Palette capabilities, compiler diagnostics, and editor
semantics remain UE-native.

{: .note }
> Loomle 0.7.0-rc.1 is available for Unreal Engine 5.7 on Apple Silicon macOS
> and x64 Windows through GitHub Releases. The public Fab listing still
> contains Loomle 0.6.

## The Working Model

A reliable Loomle workflow has eight steps:

1. Check Client, update, session, and Bridge health with `status`.
2. Bind the MCP session if it is not already bound.
3. Start from `editor_context` or an exact Asset Path.
4. Query a compact summary, collection, tree, context, or flow.
5. Discover exact schema only when the operation requires it.
6. Use a UE Palette result before creating an object.
7. Dry-run the complete Patch, then apply the same authored change.
8. Compile and save through the owning asset when its interface requires it.

The [Quickstart](quickstart.html) walks through that complete path.

## Six Calls, Six Interfaces

The Client exposes six public MCP tools:

| Call | Responsibility |
| --- | --- |
| `status` | Inspect Client/update status and bound session/Bridge health. |
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

Loomle 0.7 ships as one platform-specific Unreal plugin package containing
both the C++ Unreal Bridge and the matching self-contained Client:

```text
LoomleBridge/Resources/Loomle/<platform-arch>/loomle(.exe)
```

There is no separate Python server, Node.js runtime, global Loomle
installation, website installer, or project-local Client. GitHub Releases is
the current 0.7 release-candidate channel; the same plugin package is intended
to move to Fab later. The MCP host launches the Client bundled inside the
installed plugin.
