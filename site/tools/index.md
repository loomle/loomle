---
layout: default
title: Tools
nav_order: 5
has_children: true
description: Complete public LOOMLE tool list.
---

# Tools

This page lists every public LOOMLE tool registered by the MCP server. Project
and session tools come first because they decide whether the UE-facing tools can
run at all.

## Project and Session

- `loomle.status`: report whether the MCP session is attached to a UE project.
- `project.list`: list known Unreal projects, online by default.
- `project.attach`: attach this MCP session to one online LOOMLE-enabled project.
- `project.install`: install or update `Plugins/LoomleBridge` for a UE project.
- `schema.inspect`: read second-level operation schemas for compact edit tools.
- `loomle`: report bridge health and runtime status after attach.
- `context`: read active editor asset, active window, and current selection.

[Project and session tools](project.html)

## Runtime

- `execute`: run Unreal-side Python inside the editor process.
- `jobs`: inspect long-running job state, results, and logs.
- `profiling`: read official Unreal profiling data and capture workflows.
- `play`: inspect and control PIE play sessions.

[Runtime tools](runtime.html)

## Editor

- `editor.open`: open or focus an Unreal asset editor.
- `editor.focus`: focus a semantic panel in an asset editor.
- `editor.screenshot`: capture the active editor window to a PNG file.

[Editor tools](editor.html)

## Assets

- `asset.create`: create supported Unreal assets.
- `asset.inspect`: inspect an asset through the matching public domain surface.
- `asset.edit`: edit asset-level metadata, with enum entries as a compatibility case.

[Asset tools](asset.html)

## Blueprint

- `blueprint.inspect`: inspect a Blueprint asset and class-level contract.
- `blueprint.class.inspect`: inspect parent class and implemented interfaces.
- `blueprint.class.edit`: edit parent class and implemented interfaces.
- `blueprint.member.inspect`: inspect variables, functions, macros, events, dispatchers, and components.
- `blueprint.member.edit`: edit Blueprint-owned members.
- `blueprint.graph.list`: list graphs in a Blueprint asset.
- `blueprint.graph.inspect`: inspect graph nodes, pins, links, and compact views.
- `blueprint.graph.edit`: apply explicit local graph edit commands.
- `blueprint.graph.layout`: format selected graph regions.
- `blueprint.node.inspect`: inspect one node's pins, defaults, and node-local edit capabilities.
- `blueprint.node.edit`: edit node-local structure such as switch cases or format-text arguments.
- `blueprint.palette`: search UE Blueprint Action Menu entries for node creation.
- `blueprint.compile`: compile a Blueprint asset.

[Blueprint tools](blueprint/)

## Material

- `material.list`: list material expressions in a Material asset.
- `material.graph.inspect`: inspect expression nodes, pins, and links.
- `material.graph.edit`: apply explicit Material graph edit commands.
- `material.graph.layout`: format selected Material graph nodes.
- `material.compile`: compile a Material asset and return diagnostics.
- `material.node.inspect`: inspect one expression instance or expression class.
- `material.node.edit`: set one editable expression property.
- `material.palette`: search UE Material palette actions for expression creation.

[Material tools](material/)

## PCG

- `pcg.graph.inspect`: inspect PCG graph nodes, pins, links, and defaults.
- `pcg.palette`: search UE PCG palette actions for node creation.
- `pcg.node.inspect`: inspect one PCG node instance or settings class.
- `pcg.parameter.inspect`: inspect PCG graph user parameters.
- `pcg.parameter.edit`: edit PCG graph user parameters.
- `pcg.graph.edit`: apply explicit PCG graph edit commands.
- `pcg.graph.layout`: format selected PCG graph nodes.
- `pcg.compile`: validate and compile-confirm a PCG graph.

[PCG tools](pcg/)

## Diagnostics and Logs

- `diagnostic.tail`: read structured LOOMLE diagnostics by sequence cursor.
- `log.tail`: read Unreal output log events by sequence cursor.

[Diagnostics and log tools](diagnostics.html)

## Widget

- `widget.palette`: search UE Widget Palette entries for UMG widget creation.
- `widget.tree.inspect`: inspect a WidgetBlueprint WidgetTree.
- `widget.tree.edit`: apply explicit WidgetTree edit commands.
- `widget.inspect`: inspect one UMG widget class or WidgetTree instance.
- `widget.compile`: compile a WidgetBlueprint and return diagnostics.

[Widget tools](widget/)
