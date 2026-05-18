---
layout: default
title: Blueprint Class
parent: Blueprint
nav_order: 5
---

# Blueprint Class

Blueprint class tools handle the class contract of a Blueprint asset: parent
class and implemented interfaces. They do not edit graph nodes, member
signatures, or pin defaults.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `blueprint.inspect` | Inspect a Blueprint asset and class-level contract. |
| `blueprint.class.inspect` | Inspect parent class and implemented interfaces. |
| `blueprint.class.edit` | Edit parent class and implemented interfaces. |

## `blueprint.inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |

Use this for an asset-level Blueprint summary before choosing class, member, or
graph tools.

## `blueprint.class.inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |

Use this when the task is about parent class or interfaces.

## `blueprint.class.edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `operation` | yes | `setParent`, `listInterfaces`, `addInterface`, or `removeInterface`. |
| `args` | no | Operation-specific argument object. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Compile after changing class parent or interfaces.
