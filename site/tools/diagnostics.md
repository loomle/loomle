---
layout: default
title: Diagnostics and Logs
parent: Tools
nav_order: 8
---

# Diagnostics and Log Tools

Diagnostics and log tools read persisted runtime evidence incrementally. They
use sequence cursors so an agent can continue from the last seen event.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `diagnostic.tail` | Read structured LOOMLE diagnostic events. |
| `log.tail` | Read Unreal output log events. |

## `diagnostic.tail`

Reads structured LOOMLE diagnostic events.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `fromSeq` | no | Start sequence; defaults to 0. |
| `limit` | no | 1 to 1000; defaults to 200. |
| `filters.severity` | no | Filter by severity. |
| `filters.category` | no | Filter by diagnostic category. |
| `filters.source` | no | Filter by source. |
| `filters.assetPathPrefix` | no | Filter diagnostics under an asset path prefix. |

### Use When

Use diagnostics when the issue is likely in LOOMLE's structured runtime events:
tool errors, bridge state, validation failures, or asset-specific diagnostics.

## `log.tail`

Reads Unreal output log events.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `fromSeq` | no | Start sequence; defaults to 0. |
| `limit` | no | 1 to 1000; defaults to 200. |
| `filters.minVerbosity` | no | Minimum Unreal log verbosity. |
| `filters.category` | no | Single Unreal log category. |
| `filters.categories` | no | Multiple Unreal log categories. |
| `filters.source` | no | Log source. |

### Use When

Use logs when the next clue is likely in Unreal's output log, such as compile
messages, plugin warnings, runtime errors, or editor subsystem output.
