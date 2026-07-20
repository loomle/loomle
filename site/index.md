---
layout: home
title: Loomle
nav_order: 1
description: Agent-native Unreal Engine tools for Blueprint, Widget, Class, Graph, and Asset workflows through SAL.
permalink: /
---

# Loomle
{: .fs-9 }

Readable, precise Unreal Engine objects for people and agents.
{: .fs-6 .fw-300 }

[0.7 installation status](install.html){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Start with the Quickstart](quickstart.html){: .btn .fs-5 .mb-4 .mb-md-0 }

Loomle lets Codex, Claude, and other MCP-compatible agents read and change
complex Unreal Editor state through SAL, the Structured Agent Language. SAL is
compact line-oriented text: easy for an agent to reason over, and readable
enough for a person to copy, review, and discuss.

## Why SAL

Blueprint graphs and Widget trees are rich UE objects, not source files. Raw
serialization is noisy, while simplified wrappers easily lose UE semantics.
SAL takes a narrower approach:

- UE paths, types, field names, values, Palettes, and diagnostics stay native.
- Summaries and local traversal avoid pulling entire graphs into context.
- Typed stable references support precise follow-up queries and patches.
- Query and mutation results use the same ordered Object Text.
- Dry runs use the real validation and planning path before applying edits.

This gives agents the text-like workflow they already have for code without
inventing a replacement object model for Unreal.

## Current 0.7 Surface

The Client exposes four MCP calls:

- `sal_query` reads one self-contained SAL Query Text.
- `sal_patch` validates or applies one ordered SAL Patch Text.
- `sal_schema` discovers SAL and the active UE interface cards.
- `editor_context` returns the user's current UE interaction target as SAL.

The current public modules are Asset, Blueprint, Class, Graph, and Widget.
They include factual references, graph execution and data flow, dynamic schema
discovery, native object health, Blueprint compilation, and asset save.

## How It Ships

Loomle 0.7 will ship one Fab plugin containing both the C++ Bridge and the
matching self-contained Client:

```text
LoomleBridge/Resources/Loomle/<platform-arch>/loomle(.exe)
```

No Python, `uv`, Node.js, global Loomle installation, or project-local plugin
copy is required. The public Fab listing remains on 0.6 while this package is
being prepared; see [Install](install.html) for the exact current status.

Start with [Install](install.html), then follow the
[Quickstart](quickstart.html).
