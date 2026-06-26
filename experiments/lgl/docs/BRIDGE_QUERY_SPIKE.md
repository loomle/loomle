# LGL Bridge Query Spike

This document defines the first UE-backed LGL-native bridge spike.

> Status: first implementation spike. This document follows the current
> graph-first object schema where needed. The target agent-facing text syntax
> is documented in `OVERVIEW.md`, `LANGUAGE_CORE.md`, and `domains/graph.md`.

The spike exists to prove the object RPC boundary and Blueprint readback path
before adding mutation, palette lookup, or public LGL text tools.

Implementation notes from the existing UE bridge are collected in
[`BRIDGE_QUERY_IMPLEMENTATION_NOTES.md`](BRIDGE_QUERY_IMPLEMENTATION_NOTES.md).
The bridge architecture is defined in
[`BRIDGE_ARCHITECTURE.md`](BRIDGE_ARCHITECTURE.md).

## Goal

Implement a minimal `lgl.query` path that accepts normalized LGL object
JSON and returns an LGL object result built from live UE Blueprint graph state.

This is not an MCP text tool. The TypeScript SDK remains responsible for LGL
text parsing, formatting, and schema validation on the client side. The UE
bridge receives object JSON only.

## Scope

Supported:

- RPC: `lgl.query`
- target: `GraphTarget` with `target.domain = "blueprint"`
- graph reference: asset path plus graph name or graph id
- query forms:
  - empty query
  - `find nodes where name = <name> with pins, defaults`
- response object: compact `graph` snippet
- diagnostics: schema, target, graph, node, and unsupported-query failures

Not supported:

- `lgl.patch`
- raw LGL text parsing inside UE
- public MCP `lgl.query`
- palette queries
- path queries
- surrounding-context queries
- full graph caching
- Material or PCG targets
- mutation, transactions, reconstruction, or asset dirtying

## Request Boundary

The bridge-facing request envelope is:

```ts
interface LglObjectRequest {
  object: Query;
}
```

The object must conform to `schema/lgl-object.schema.json`.

The first spike may use hand-written C++ structs and structural validation, but
the accepted JSON shape must stay aligned with the schema. Do not add C++-only
request fields at the RPC boundary.

Example empty query object:

```json
{
  "object": {
    "kind": "query",
    "target": {
      "domain": "blueprint",
      "asset": "/Game/BP_Door",
      "graph": {
        "kind": "name",
        "name": "EventGraph"
      }
    }
  }
}
```

Example find-node query object:

```json
{
  "object": {
    "kind": "query",
    "target": {
      "domain": "blueprint",
      "asset": "/Game/BP_Door",
      "graph": {
        "kind": "name",
        "name": "EventGraph"
      }
    },
    "find": {
      "kind": "nodes"
    },
    "where": {
      "kind": "eq",
      "field": {
        "path": ["name"]
      },
      "value": {
        "kind": "name",
        "name": "branch"
      }
    },
    "with": ["pins", "defaults"]
  }
}
```

## Response Boundary

The bridge returns:

```ts
interface ObjectResult {
  object?: LglObject;
  diagnostics: Diagnostic[];
  page?: Page;
}
```

Successful query responses should return a compact `graph` object. The graph
object should contain only the information requested by the query.

For `with pins`, include node pins and enough pin identity for follow-up patch
work. For `with defaults`, include node fields, pin values, or palette defaults
where the returned object type supports them. For `with layout`, include stable
UE editor layout fields such as node `at` and non-zero node `size`. Do not
estimate pin anchors in asset readback.

The response must be schema-valid before it leaves the LGL bridge boundary.

## Empty Query Behavior

An empty query requests a compact graph snapshot:

```txt
query blueprint("/Game/BP_Door"/EventGraph)
```

For the first spike, the snapshot should be small and inline. It should include:

- target
- graph identity
- node aliases or ids
- node titles/types sufficient for follow-up `find nodes where name = <name>`
- links between included nodes when cheap to read

It does not need to include every pin, default, layout field, or large metadata
field. Large graph cache references are out of scope for the first spike.

## Find Node Behavior

The first spike supports the constrained form `find nodes where name = <name>`.
It must return exactly one Blueprint graph node or a diagnostic.

The node name may match a stable LGL alias, a UE node title, or another
adapter-defined readable identity. The matching rules must be documented in the
implementation notes once UE behavior is confirmed.

If there are no matches, return `unknown_node`.

If there is more than one match, return `ambiguous_node` with enough candidate
information for the caller to issue a more precise query.

The successful response is a one-node graph snippet with requested details.

## Diagnostics

Diagnostics should be actionable and stable enough for tests.

Minimum diagnostic codes:

```txt
invalid_request
invalid_object
unsupported_domain
unsupported_query
asset_not_found
graph_not_found
unknown_node
ambiguous_node
readback_failed
```

Suggested recovery text examples:

- `Run an empty query for this graph and use one of the returned node names.`
- `Use Target.domain = "blueprint" for this spike.`
- `This query form is not implemented yet; use empty query or find nodes.`

Diagnostics should identify the request path when the problem is structural,
for example `object.target.domain` or `object.with`.

## Implementation Boundary

The spike should follow the core, adapter, and shared service boundaries in
[`BRIDGE_ARCHITECTURE.md`](BRIDGE_ARCHITECTURE.md).

Production LGL code must not call existing public graph inspect handlers. Old
bridge tools may be used only as reference material or comparison-test oracles.

The expected runtime path is:

```txt
RPC endpoint
  -> request envelope decode
  -> object validation
  -> adapter lookup by target.domain
  -> Blueprint asset and graph resolution
  -> UE graph readback
  -> LGL graph snippet assembly
  -> response validation
  -> ObjectResult encode
```

## Acceptance

The first spike is complete when:

- `lgl.query` is registered and reachable in the UE bridge.
- malformed request envelopes fail before adapter dispatch.
- non-Blueprint domains fail with `unsupported_domain`.
- missing assets and graphs return actionable diagnostics.
- empty Blueprint graph queries return schema-valid graph snippets.
- `find nodes where name = <name> with pins, defaults` returns one-node snippets from live UE
  graph state.
- ambiguous node matches return candidates instead of guessing.
- tests or fixtures cover accepted and rejected object JSON.
- no LGL implementation file calls old public graph inspect tool handlers.

## Follow-Up Work

After this spike:

- add palette query readback
- add path and surrounding-context query forms
- introduce `lgl.patch` dry-run planning
- add public MCP `lgl.query` after object RPC and TypeScript formatting work
  are connected
- reassess generated or semi-generated C++ codecs once the object boundary is
  proven against live UE data
