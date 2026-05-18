---
layout: default
title: Assets
parent: Tools
nav_order: 0
---

# Asset Tools

Asset tools operate at the Unreal asset boundary.

Use them before choosing a domain-specific tool when the task is about creating,
inspecting, or editing asset-level metadata.

## `asset.create`

Creates supported Unreal assets.

Supported public categories include:

- Blueprint
- enum
- Material
- Material Function
- PCG graph
- WidgetBlueprint

After creating an asset, switch to the domain-specific tools for graph, member,
parameter, or tree edits.

## `asset.inspect`

Inspects an Unreal asset through the matching public domain surface.

Use it when the asset kind is known and the next step depends on asset-level
metadata.

## `asset.edit`

Edits asset-level metadata.

Enum entry editing remains supported as a special compatibility case, but the
general intent of `asset.edit` is metadata editing rather than graph or member
mutation.
