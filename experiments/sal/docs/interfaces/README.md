# SAL Interface Schemas

`sal.schema()` returns this compact index with only the modules active in the
current server:

```text
asset
  Find UE assets and exact Asset Paths.

blueprint
  Inspect and edit Blueprint-owned structure and finalize changes.

class
  Inspect UE Reflection and edit durable Blueprint Class Defaults.

graph
  Inspect and edit Graph Nodes, Pins, Edges, flow, and Node creation.

widget
  Inspect and edit WidgetBlueprint trees, Widgets, placement, and events.

Use sal.schema("<module>") for one static interface.
Use exact with schema for current UE Query operations, fields, Patch statements,
and Operations.
```

Unavailable modules are omitted rather than listed with status metadata. The
index does not repeat the resident Core guide or load UE objects.
