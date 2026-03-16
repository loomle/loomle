# Graph Ops Protocol Draft

## 1. Scope

This document turns the graph-ops design into a protocol draft aligned with the
existing MCP tool contract style.

It covers:

- `graph.ops`
- `graph.ops.resolve`

This is still a draft. It does not mean the tools are implemented yet.

## 2. Public Direction

Target public graph workflow:

1. `graph.list` or `graph.resolve`
2. `graph.query`
3. `graph.ops`
4. `graph.ops.resolve`
5. `graph.mutate`

Public graph-semantic direction:

- agents discover stable semantics through `graph.ops`
- agents turn semantics into execution plans through `graph.ops.resolve`
- `graph.mutate` executes stable realization ops only

## 3. Tool Catalog Impact

Planned tool catalog:

- `loomle`
- `context`
- `execute`
- `graph`
- `graph.list`
- `graph.resolve`
- `graph.query`
- `graph.ops`
- `graph.ops.resolve`
- `graph.mutate`
- `diag.tail`

## 4. Shared Concepts

### 4.1 `opId`

Stable semantic identifier for a graph operation.

Examples:

- `core.comment`
- `core.reroute`
- `bp.flow.branch`
- `mat.texture.sample`
- `pcg.filter.by_tag`

### 4.2 Resolution Source

- `typed_discovery`
- `loomle_catalog`
- `generic_fallback`
- `mixed`

### 4.3 Coverage

- `contextual`
- `curated`
- `partial`

### 4.4 Determinism

- `stable`
- `context_sensitive`
- `ephemeral`

## 5. `graph.ops`

### 5.1 Purpose

List stable semantic operations LOOMLE knows how to reason about for a graph
domain.

### 5.2 Input schema draft

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["graphType"],
  "properties": {
    "graphType": {
      "type": "string",
      "enum": ["blueprint", "material", "pcg"]
    },
    "query": {
      "type": "string",
      "description": "Optional fuzzy text filter over known opIds, labels, tags, and summaries."
    },
    "stability": {
      "type": "string",
      "enum": ["stable", "experimental"],
      "description": "Optional filter by op stability."
    },
    "limit": {
      "type": "integer",
      "minimum": 1,
      "maximum": 1000,
      "description": "Maximum number of ops to return."
    }
  },
  "additionalProperties": false
}
```

### 5.3 Output schema draft

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["graphType", "ops", "meta", "diagnostics"],
  "properties": {
    "graphType": {
      "type": "string",
      "enum": ["blueprint", "material", "pcg"]
    },
    "ops": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["opId", "stability", "scope", "summary"],
        "properties": {
          "opId": { "type": "string", "minLength": 1 },
          "stability": { "type": "string", "enum": ["stable", "experimental"] },
          "scope": {
            "type": "string",
            "enum": ["cross-graph", "blueprint", "material", "pcg"]
          },
          "summary": { "type": "string" },
          "tags": { "type": "array", "items": { "type": "string" } }
        },
        "additionalProperties": false
      }
    },
    "meta": {
      "type": "object",
      "required": ["source", "coverage"],
      "properties": {
        "source": { "type": "string", "enum": ["loomle_catalog", "mixed"] },
        "coverage": { "type": "string", "enum": ["curated", "partial"] }
      },
      "additionalProperties": false
    },
    "diagnostics": {
      "type": "array",
      "items": { "type": "object", "additionalProperties": true }
    }
  },
  "additionalProperties": false
}
```

## 6. `graph.ops.resolve`

### 6.1 Purpose

Resolve one or more `opId` values in a concrete graph context into mutate-ready
plans.

### 6.2 Input schema draft

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["graphType", "graphRef", "items"],
  "properties": {
    "graphType": {
      "type": "string",
      "enum": ["blueprint", "material", "pcg"]
    },
    "graphRef": {
      "type": "object",
      "description": "GraphRef emitted by graph.list or graph.query."
    },
    "context": {
      "type": "object",
      "properties": {
        "fromPin": {
          "type": "object",
          "properties": {
            "nodeId": { "type": "string", "minLength": 1 },
            "pinName": { "type": "string", "minLength": 1 }
          },
          "required": ["nodeId", "pinName"],
          "additionalProperties": false
        },
        "toPin": {
          "type": "object",
          "properties": {
            "nodeId": { "type": "string", "minLength": 1 },
            "pinName": { "type": "string", "minLength": 1 }
          },
          "required": ["nodeId", "pinName"],
          "additionalProperties": false
        },
        "edge": {
          "type": "object",
          "required": ["fromPin", "toPin"],
          "properties": {
            "fromPin": {
              "type": "object",
              "required": ["nodeId", "pinName"],
              "properties": {
                "nodeId": { "type": "string", "minLength": 1 },
                "pinName": { "type": "string", "minLength": 1 }
              },
              "additionalProperties": false
            },
            "toPin": {
              "type": "object",
              "required": ["nodeId", "pinName"],
              "properties": {
                "nodeId": { "type": "string", "minLength": 1 },
                "pinName": { "type": "string", "minLength": 1 }
              },
              "additionalProperties": false
            }
          },
          "additionalProperties": false
        }
      },
      "additionalProperties": false
    },
    "items": {
      "type": "array",
      "minItems": 1,
      "maxItems": 200,
      "items": {
        "type": "object",
        "required": ["opId"],
        "properties": {
          "opId": { "type": "string", "minLength": 1 },
          "clientRef": {
            "type": "string",
            "description": "Optional client-side reference echoed in the result."
          },
          "hints": {
            "type": "object",
            "description": "Optional semantic resolution hints.",
            "additionalProperties": true
          }
        },
        "additionalProperties": false
      }
    }
  },
  "additionalProperties": false
}
```

### 6.3 Output schema draft

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["graphType", "graphRef", "results", "diagnostics"],
  "properties": {
    "graphType": {
      "type": "string",
      "enum": ["blueprint", "material", "pcg"]
    },
    "graphRef": {
      "type": "object",
      "description": "Effective graph locator used for resolution."
    },
    "results": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["opId", "resolved", "compatibility"],
        "properties": {
          "opId": { "type": "string" },
          "clientRef": { "type": "string" },
          "resolved": { "type": "boolean" },
          "compatibility": {
            "type": "object",
            "required": ["isCompatible", "reasons"],
            "properties": {
              "isCompatible": { "type": "boolean" },
              "reasons": { "type": "array", "items": { "type": "string" } }
            },
            "additionalProperties": false
          },
          "remediation": {
            "type": "object",
            "properties": {
              "requiredContext": {
                "type": "array",
                "items": { "type": "string" }
              },
              "missingFields": {
                "type": "array",
                "items": { "type": "string" }
              },
              "nextAction": { "type": "string" },
              "fallbackKind": {
                "type": "string",
                "enum": ["none", "direct_mutate", "manual_readback"]
              }
            },
            "additionalProperties": false
          },
          "preferredPlan": {
            "type": "object",
            "required": ["realizationKind", "source", "coverage", "determinism"],
            "properties": {
              "realizationKind": { "type": "string" },
              "preferredMutateOp": { "type": "string" },
              "args": {
                "type": "object",
                "additionalProperties": true
              },
              "steps": {
                "type": "array",
                "items": {
                  "type": "object",
                  "required": ["op", "args"],
                  "properties": {
                    "op": { "type": "string" },
                    "clientRef": { "type": "string" },
                    "args": {
                      "type": "object",
                      "additionalProperties": true
                    }
                  },
                  "additionalProperties": false
                }
              },
              "settingsTemplate": {
                "type": "object",
                "additionalProperties": true
              },
              "pinHints": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "kind": { "type": "string" },
                    "pinName": { "type": "string" },
                    "semanticRole": { "type": "string" },
                    "isDefaultPath": { "type": "boolean" }
                  },
                  "additionalProperties": true
                }
              },
              "verificationHints": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "kind": { "type": "string" },
                    "targetClientRef": { "type": "string" },
                    "requiredBeforeNextStep": { "type": "boolean" }
                  },
                  "additionalProperties": true
                }
              },
              "executionHints": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "kind": { "type": "string" },
                    "preserveUpstream": { "type": "boolean" },
                    "preserveDownstream": { "type": "boolean" },
                    "composeMode": {
                      "type": "string",
                      "enum": ["independent", "pipeline_segment"]
                    }
                  },
                  "additionalProperties": true
                }
              },
              "source": {
                "type": "string",
                "enum": ["typed_discovery", "loomle_catalog", "generic_fallback", "mixed"]
              },
              "coverage": {
                "type": "string",
                "enum": ["contextual", "curated", "partial"]
              },
              "determinism": {
                "type": "string",
                "enum": ["stable", "context_sensitive", "ephemeral"]
              }
            },
            "additionalProperties": false
          },
          "alternatives": {
            "type": "array",
            "items": { "type": "object", "additionalProperties": true }
          },
          "reason": { "type": "string" }
        },
        "additionalProperties": false
      }
    },
    "diagnostics": {
      "type": "array",
      "items": { "type": "object", "additionalProperties": true }
    }
  },
  "additionalProperties": false
}
```

## 7. Plan Semantics

### 7.1 Preferred plan

`preferredPlan` is the best currently known realization for the requested
semantic op in the supplied graph context.

It should prefer deterministic realizations over editor-ephemeral ones.

It should not re-expose public action-token-style flows.

### 7.2 Multi-step plans

If one semantic op requires several mutate operations, the result may use
`preferredPlan.steps[]`.

This is especially important for PCG, where one semantic operation may require:

1. adding one or more nodes
2. applying nested settings
3. connecting specific semantic pins
4. adding layout or compile steps

### 7.3 PCG enrichments

For PCG, a plan may include:

- `settingsTemplate`
- `pinHints`
- `verificationHints`

These fields exist because PCG behavior often depends more on nested settings
and pin semantics than on topology alone.

## 8. Error and Trust Model

- unresolved items remain structured inside `results[]`
- `compatibility.reasons[]` should explain why a semantic op does not fit the
  current context
- `remediation` should tell the caller what narrower context or follow-up is
  required when the item did not resolve
- if LOOMLE lacks sufficient graph observability to resolve with confidence, the
  result should degrade coverage or determinism rather than overstate certainty

## 9. Public Boundary

The public graph-semantic surface is:

- `graph.ops`
- `graph.ops.resolve`
- `graph.mutate`

Internal editor discovery may still survive as a resolver implementation
detail.

## 10. Draft Follow-Up Direction

Recent feedback suggests the next protocol iteration should settle:

1. richer context shapes such as `toPin` and `edge`
2. structured unresolved remediation
3. richer `steps[]`, `pinHints`, `verificationHints`, and `executionHints`
4. explicit PCG insertion and composition semantics
5. workflow-driven catalog expansion

## 11. Proposed v1 Catalog Baseline

This section defines the minimum semantic catalog expected for the first usable
release.

### 11.1 Blueprint

Required:

- `core.comment`
- `core.reroute`
- `bp.flow.branch`

Stretch:

- `bp.flow.sequence`
- `bp.exec.delay`
- `bp.debug.print_string`

### 11.2 Material

Required:

- `mat.constant.scalar`
- `mat.constant.vector3`
- `mat.math.multiply`
- `mat.param.scalar`
- `mat.param.vector`
- `mat.texture.sample`

Stretch:

- `mat.math.add`
- `mat.math.lerp`
- `mat.utility.clamp`

### 11.3 PCG

Required:

- `pcg.create.points`
- `pcg.meta.add_tag`
- `pcg.filter.by_tag`
- `pcg.sample.surface`
- `pcg.transform.points`
- `pcg.sample.spline`
- `pcg.source.actor_data`
- `pcg.spawn.static_mesh`

Stretch:

- `pcg.filter.by_attribute`
- `pcg.project.surface`
- `pcg.spawn.actor`

Known pressure from first-release feedback:

- `pipeline_insert` should preserve or explicitly rewrite downstream edges
- pin hints should match actual runtime outputs
- multi-item resolution should distinguish independent plans from composed
  pipeline segments

### 11.4 Realization guidance

Expected first-pass realization mapping:

- Blueprint: mix of stable class-based realization and context-sensitive
  resolver logic
- Material: primarily stable class-based realization
- PCG: class-based realization plus frequent multi-step plans with settings and
  pin guidance
