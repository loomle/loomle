---
layout: default
title: Diagnostics and Logs
parent: Tools
nav_order: 8
---

# Diagnostics and Log Tools

Diagnostics and log tools read persisted runtime evidence incrementally. They
are cursor-based so agents can continue from the last seen sequence.

## Tool List

- `diagnostic.tail`: read structured LOOMLE diagnostic events.
- `log.tail`: read Unreal output log events.

## Schemas

| Tool | Required | Key Fields |
| --- | --- | --- |
| `diagnostic.tail` | none | `fromSeq`, `limit`, `filters.severity`, `filters.category`, `filters.source`, `filters.assetPathPrefix` |
| `log.tail` | none | `fromSeq`, `limit`, `filters.minVerbosity`, `filters.category`, `filters.categories`, `filters.source` |

Use diagnostics for structured LOOMLE runtime events. Use log tail when the
next clue is likely in Unreal's output log.
