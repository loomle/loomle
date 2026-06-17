# LGL Query Semantics

Query documents read one existing graph. They do not mutate graph state and do
not contain palette bindings.

The current query forms are covered by Blueprint example conformance tests.

## Empty Query

An empty query body requests a compact full graph snapshot:

```txt
query blueprint("/Game/BP_Door"/EventGraph)
```

Large adapters may store full snapshots in cache/workspace files and return an
LGL reference or diagnostics rather than a large inline result.

## Find Nodes

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find nodes where type = PrintString
```

Supported conditions are intentionally small:

```txt
where field = value
where field contains value
```

The normalized object model also supports `and` conditions. Adapter-specific
field names are validated by adapters, not by the text parser.

Details may be requested:

```txt
find nodes where type = PrintString with pins, defaults, layout
```

## Find One Node

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find node branch with pins, defaults, layout
```

The result is a compact `graph` snippet for the requested node, with requested
pin/default/layout details when the adapter can provide them.

## Find Path

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find path from begin.Then
```

Output pins walk downstream. Input pins walk upstream. Directional path behavior
is adapter-owned because native graph semantics determine valid links.

## Find Surrounding

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find surrounding around branch depth 2
```

The result is a compact graph snippet containing nearby nodes and links.

## Find Palette Entry

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find palette entry "Print String"
```

Structured constraints may refine the query:

```txt
find palette entry "Print" where function = "/Script/Engine.KismetSystemLibrary.PrintString"
find palette entry where function = "/Script/Engine.KismetSystemLibrary.PrintString"
```

Palette query results are `palette` documents:

```txt
palette blueprint("/Game/BP_Door"/EventGraph)

PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", title: "Print String", category: "Utilities/String"})
```

Patch documents bind palette ids from these results. Patch documents do not
perform fuzzy palette search.
