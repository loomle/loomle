---
layout: default
title: Concepts
nav_order: 4
has_children: true
description: The core model behind LOOMLE.
---

# Concepts

LOOMLE is organized around UE concepts, not around generic automation concepts.

## Main Ideas

- The global CLI starts an MCP server.
- A project-local bridge runs inside Unreal Editor.
- The MCP session attaches to one active Unreal project.
- Asset domains follow UE semantics: Blueprint, Material, PCG, Widget.
- Creation uses UE palettes.
- Edits are explicit, local, and inspect-driven.
- Some tools use `schema.inspect` for second-level operation schemas.

Use these concepts to decide which tool to call before editing.
