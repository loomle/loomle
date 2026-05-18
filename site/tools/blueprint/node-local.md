---
layout: default
title: Node-Local Edits
parent: Blueprint
nav_order: 3
---

# Blueprint Node-Local Edits

Some Blueprint nodes own local structure that is not a graph-level edit and not
a Blueprint member signature.

Examples:

- Switch cases.
- Sequence pins.
- Select option pins.
- Format Text arguments.
- SetFieldsInStruct field visibility.

Use `blueprint.node.inspect` first. It returns current pins, node-local state,
and `editCapabilities`.

Then call `blueprint.node.edit`. Use `schema.inspect` for the selected
operation schema.
