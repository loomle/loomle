---
layout: default
title: Blueprint Members
parent: Blueprint
nav_order: 4
---

# Blueprint Members

Blueprint members are asset-owned definitions, not graph wiring.

Use `blueprint.member.inspect` and `blueprint.member.edit` for:

- variables
- functions
- macros
- dispatchers
- custom events
- components

Call `schema.inspect` before `blueprint.member.edit` operations. Member editing
uses operation-specific schemas.

Use graph tools only for graph placement, pins, links, and node-level changes.
