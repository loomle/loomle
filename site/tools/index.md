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
- `project_list`: list known Unreal projects, online by default.
- `project_attach`: attach this MCP session to one online LOOMLE-enabled project.
- `project_install`: install or update `Plugins/LoomleBridge` for a UE project.
- `schema_inspect`: read second-level operation schemas for compact edit tools.
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

- `editor_open`: open or focus an Unreal asset editor.
- `editor_focus`: focus a semantic panel in an asset editor.
- `editor_screenshot`: capture the active editor window to a PNG file.

[Editor tools](editor.html)

## Assets

- `asset_create`: create supported Unreal assets.
- `asset_inspect`: inspect an asset through the matching public domain surface.
- `asset_edit`: edit asset-level metadata, with enum entries as a compatibility case.

[Asset tools](asset.html)

## Blueprint

- `blueprint_inspect`: inspect a Blueprint asset and class-level contract.
- `blueprint_class_inspect`: inspect parent class and implemented interfaces.
- `blueprint_class_edit`: edit parent class and implemented interfaces.
- `blueprint_member_inspect`: inspect variables, functions, macros, events, dispatchers, and components.
- `blueprint_member_edit`: edit Blueprint-owned members.
- `blueprint_graph_list`: list graphs in a Blueprint asset.
- `blueprint_graph_inspect`: inspect graph nodes, pins, links, and compact views.
- `blueprint_graph_edit`: apply explicit local graph edit commands.
- `blueprint_graph_layout`: format selected graph regions.
- `blueprint_node_inspect`: inspect one node's pins, defaults, and node-local edit capabilities.
- `blueprint_node_edit`: edit node-local structure such as switch cases or format-text arguments.
- `blueprint_graph_palette`: search UE Blueprint Action Menu entries for node creation.
- `blueprint_compile`: compile a Blueprint asset.

[Blueprint tools](blueprint/)

## Material

- `material_list`: list material expressions in a Material asset.
- `material_graph_inspect`: inspect expression nodes, pins, and links.
- `material_graph_edit`: apply explicit Material graph edit commands.
- `material_graph_layout`: format selected Material graph nodes.
- `material_compile`: compile a Material asset and return diagnostics.
- `material_node_inspect`: inspect one expression instance or expression class.
- `material_node_edit`: set one editable expression property.
- `material_palette`: search UE Material palette actions for expression creation.

[Material tools](material/)

## PCG

- `pcg_graph_inspect`: inspect PCG graph nodes, pins, links, and defaults.
- `pcg_palette`: search UE PCG palette actions for node creation.
- `pcg_node_inspect`: inspect one PCG node instance or settings class.
- `pcg_parameter_inspect`: inspect PCG graph user parameters.
- `pcg_parameter_edit`: edit PCG graph user parameters.
- `pcg_graph_edit`: apply explicit PCG graph edit commands.
- `pcg_graph_layout`: format selected PCG graph nodes.
- `pcg_compile`: validate and compile-confirm a PCG graph.

[PCG tools](pcg/)

## Diagnostics and Logs

- `diagnostic_tail`: read structured LOOMLE diagnostics by sequence cursor.
- `log_tail`: read Unreal output log events by sequence cursor.

[Diagnostics and log tools](diagnostics.html)

## Widget

- `widget_palette`: search UE Widget Palette entries for UMG widget creation.
- `widget_tree_inspect`: inspect a WidgetBlueprint WidgetTree.
- `widget_tree_edit`: apply explicit WidgetTree edit commands.
- `widget_inspect`: inspect one UMG widget class or WidgetTree instance.
- `widget_compile`: compile a WidgetBlueprint and return diagnostics.

[Widget tools](widget/)
