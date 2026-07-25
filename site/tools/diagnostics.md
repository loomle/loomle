---
layout: default
title: Diagnostics
parent: Interfaces
nav_order: 7
---

# Diagnostics

Loomle does not require a separate diagnostics query for ordinary SAL work.
Relevant health and execution information stays beside the objects and
operations that produced it.

SAL supports single-line and block comments:

```text
# short guidance

###
multi-line compiler or validation detail
###
```

Graph queries attach current Node and Pin health, including compiler messages,
upgrade messages, visual warnings, orphaned Pins, and Pin deprecation state.
Graph summary keeps representatives compact and returns a comment index of
Nodes carrying health state so an agent can follow exact Node references.

Blueprint compilation returns native Status and ordered compiler messages as
factual comments inside the first canonical Result Text block, next to any
source identities they describe. Mutation plans, effects, and apply state use a
later independent metadata block; structured validation or execution
diagnostics use another independent SAL-comment block. Neither later block is
appended after `no_objects` or fabricated as Object Text.

Stored compiler annotations can be stale when the owning Blueprint is dirty or
unknown. Loomle reports that condition; run a separate Blueprint terminal
compile Patch to obtain fresh complete diagnostics.
