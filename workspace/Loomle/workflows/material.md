# Material Workflow

Recommended Material editing rhythm for LOOMLE:

1. query the material graph
2. if you see `childGraphRef` on `MaterialFunctionCall` nodes, follow those refs or run `graph.list(includeSubgraphs=true)` to inspect referenced `UMaterialFunction` graphs
3. create and connect expression chains
4. connect terminal expressions into `__material_root__`
5. call `layoutGraph(scope=\"touched\")`

Addressing rule:
- if you already have a Material or MaterialFunction asset path, prefer resolving or querying that asset directly
- if `graph.query` returns `childGraphRef`, treat it as the preferred handle for the referenced function subgraph instead of rebuilding the address manually

Layout expectation:
- material root is the right-side sink
- sink expressions sit immediately to the left of the root
- upstream expressions expand leftward by dependency depth
