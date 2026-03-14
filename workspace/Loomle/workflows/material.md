# Material Workflow

Recommended Material editing rhythm for LOOMLE:

1. query the material graph
2. create and connect expression chains
3. connect terminal expressions into `__material_root__`
4. call `layoutGraph(scope=\"touched\")`

Layout expectation:
- material root is the right-side sink
- sink expressions sit immediately to the left of the root
- upstream expressions expand leftward by dependency depth
