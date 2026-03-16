use serde_json::{json, Value};

pub fn tool_descriptors() -> Vec<Value> {
    vec![
        json!({
            "name": "loomle",
            "title": "Loomle Status",
            "description": "Bridge health and runtime status.",
            "inputSchema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "properties": {},
                "additionalProperties": false
            },
            "outputSchema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object"
            }
        }),
        json!({
            "name": "context",
            "title": "Editor Context",
            "description": "Read active editor context and selection.",
            "inputSchema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "properties": {
                    "resolveIds": { "type": "array", "items": { "type": "string" } },
                    "resolveFields": { "type": "array", "items": { "type": "string" } }
                },
                "additionalProperties": false
            },
            "outputSchema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object"
            }
        }),
        json!({
            "name": "execute",
            "title": "Execute Script",
            "description": "Execute Unreal-side script code.",
            "inputSchema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["code"],
                "properties": {
                    "language": { "type": "string", "default": "ue-script" },
                    "mode": { "type": "string", "enum": ["exec", "eval"], "default": "exec" },
                    "code": { "type": "string", "minLength": 1 }
                },
                "additionalProperties": false
            },
            "outputSchema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object"
            }
        }),
        runtime_tool_descriptor(
            "editor.open",
            "Open Asset Editor",
            "Open or focus the editor for a specific Unreal asset path.",
            json!({
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["assetPath"],
                "properties": {
                    "assetPath": {
                        "type": "string",
                        "minLength": 1,
                        "description": "Unreal asset path, for example /Game/MyFolder/MyAsset."
                    }
                },
                "additionalProperties": false
            }),
            json!({
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object"
            }),
        ),
        runtime_tool_descriptor(
            "editor.focus",
            "Focus Editor Panel",
            "Focus a semantic panel inside an asset editor, such as graph, viewport, details, palette, or find.",
            json!({
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["assetPath", "panel"],
                "properties": {
                    "assetPath": {
                        "type": "string",
                        "minLength": 1,
                        "description": "Unreal asset path whose editor should be focused."
                    },
                    "panel": {
                        "type": "string",
                        "enum": ["graph", "viewport", "details", "palette", "find", "preview", "log", "profiling", "constructionScript", "myBlueprint"],
                        "description": "Semantic editor panel name. Supported values vary by editor type."
                    }
                },
                "additionalProperties": false
            }),
            json!({
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object"
            }),
        ),
        runtime_tool_descriptor(
            "editor.screenshot",
            "Editor Screenshot",
            "Capture a PNG of the active editor window and return the written file path.",
            json!({
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "properties": {
                    "target": {
                        "type": "string",
                        "enum": ["activeWindow"],
                        "default": "activeWindow",
                        "description": "Screenshot target. The first release supports only the active top-level editor window."
                    },
                    "path": {
                        "type": "string",
                        "description": "Optional output path. Relative paths resolve from the Unreal project root; .png is appended when omitted."
                    }
                },
                "additionalProperties": false
            }),
            json!({
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object"
            }),
        ),
        runtime_tool_descriptor(
            "graph.runtime",
            "Graph Runtime",
            "Inspect runtime graph state. The first release supports graphType=\"pcg\" only.",
            graph_runtime_input_schema(),
            graph_runtime_output_schema(),
        ),
        json!({
            "name": "graph",
            "title": "Graph Descriptor",
            "description": "Read graph capability descriptor and runtime status.",
            "inputSchema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "properties": {
                    "graphType": graph_type_schema()
                },
                "additionalProperties": false
            },
            "outputSchema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object"
            }
        }),
        runtime_tool_descriptor(
            "graph.list",
            "Graph List",
            "List readable graphs in an asset.",
            graph_list_input_schema(),
            graph_list_output_schema(),
        ),
        runtime_tool_descriptor(
            "graph.resolve",
            "Graph Resolve",
            "Resolve an Unreal object or asset reference into queryable graph refs.",
            graph_resolve_input_schema(),
            graph_resolve_output_schema(),
        ),
        runtime_tool_descriptor(
            "graph.query",
            "Graph Query",
            "Query semantic graph snapshot.",
            graph_query_input_schema(),
            graph_query_output_schema(),
        ),
        runtime_tool_descriptor(
            "graph.ops",
            "Graph Ops",
            "List stable semantic graph operations known for a graph domain.",
            graph_ops_input_schema(),
            graph_ops_output_schema(),
        ),
        runtime_tool_descriptor(
            "graph.ops.resolve",
            "Graph Ops Resolve",
            "Resolve semantic graph operations into mutate-ready plans for a concrete graph context.",
            graph_ops_resolve_input_schema(),
            graph_ops_resolve_output_schema(),
        ),
        runtime_tool_descriptor(
            "graph.mutate",
            "Graph Mutate",
            "Apply graph write operations in order.",
            graph_mutate_input_schema(),
            graph_mutate_output_schema(),
        ),
        runtime_tool_descriptor(
            "diag.tail",
            "Diagnostics Tail",
            "Read persisted diagnostics incrementally by sequence cursor.",
            diag_tail_input_schema(),
            diag_tail_output_schema(),
        ),
    ]
}

fn runtime_tool_descriptor(
    name: &str,
    title: &str,
    description: &str,
    input_schema: Value,
    output_schema: Value,
) -> Value {
    json!({
        "name": name,
        "title": title,
        "description": description,
        "inputSchema": input_schema,
        "outputSchema": output_schema
    })
}

fn graph_type_schema() -> Value {
    json!({
        "type": "string",
        "enum": ["blueprint", "material", "pcg"],
        "default": "blueprint",
        "description": "Graph domain."
    })
}

fn graph_ref_schema() -> Value {
    json!({
        "type": "object",
        "description": "Self-contained subgraph locator emitted by graph.list and graph.query. Pass back verbatim — do not construct manually.",
        "required": ["kind"],
        "oneOf": [
            {
                "properties": {
                    "kind": { "type": "string", "enum": ["inline"] },
                    "nodeGuid": { "type": "string", "minLength": 1, "description": "FGuid of the composite/subgraph node that owns this subgraph." },
                    "assetPath": { "type": "string", "minLength": 1, "description": "Asset that contains the node (embedded for self-containment)." }
                },
                "required": ["kind", "nodeGuid", "assetPath"],
                "additionalProperties": false
            },
            {
                "properties": {
                    "kind": { "type": "string", "enum": ["asset"] },
                    "assetPath": { "type": "string", "minLength": 1, "description": "Unreal asset path of the graph asset." },
                    "graphName": { "type": "string", "minLength": 1, "description": "Graph name within the asset. Required for multi-graph assets such as Blueprint; omit for single-graph assets (Material, PCG)." }
                },
                "required": ["kind", "assetPath"],
                "additionalProperties": false
            }
        ]
    })
}

fn graph_list_input_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["assetPath"],
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "Unreal asset path, for example /Game/MyFolder/MyAsset."
            },
            "graphType": graph_type_schema(),
            "includeSubgraphs": {
                "type": "boolean",
                "default": false,
                "description": "When true, recursively enumerate subgraphs owned by composite/subgraph nodes."
            },
            "maxDepth": {
                "type": "integer",
                "minimum": 0,
                "maximum": 8,
                "default": 1,
                "description": "Maximum recursion depth when includeSubgraphs is true. 0 disables recursion; 1 returns direct children only."
            }
        },
        "additionalProperties": false
    })
}

fn graph_query_input_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "Unreal asset path (Mode A). Required when graphName is used; omit when graphRef is provided."
            },
            "graphName": {
                "type": "string",
                "minLength": 1,
                "description": "Graph name within the asset (Mode A), for example EventGraph. Mutually exclusive with graphRef."
            },
            "graphRef": graph_ref_schema(),
            "graphType": graph_type_schema(),
            "layoutDetail": {
                "type": "string",
                "enum": ["basic", "measured"],
                "default": "basic",
                "description": "Requested layout detail level. `basic` returns lightweight geometry; `measured` asks the runtime to provide richer layout data when supported."
            },
            "filter": {
                "type": "object",
                "description": "Optional filters to narrow returned nodes.",
                "properties": {
                    "nodeClasses": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Restrict to nodes whose class path matches any entry."
                    },
                    "nodeIds": {
                        "type": "array",
                        "items": { "type": "string" },
                        "description": "Restrict to nodes with these IDs."
                    },
                    "text": {
                        "type": "string",
                        "description": "Fuzzy text search across node titles and comments."
                    }
                },
                "additionalProperties": false
            },
            "limit": {
                "type": "integer",
                "minimum": 1,
                "maximum": 1000,
                "description": "Maximum number of nodes/edges to return when truncation is supported."
            },
            "cursor": {
                "type": "string",
                "description": "Opaque pagination cursor returned by a prior graph.query response. Supply it together with the same graph address and filters to continue a truncated read."
            },
            "path": {
                "type": "array",
                "items": { "type": "string", "minLength": 1 },
                "minItems": 1,
                "maxItems": 8,
                "description": "Blueprint only. Ordered list of composite node GUIDs to traverse into before querying. Each entry must be a K2Node_Composite nodeId. The server resolves the subgraph of the final GUID in a single round-trip, avoiding multiple graph.query calls for deeply nested composites. Mutually exclusive with graphRef.kind=inline at the same level — supply path instead."
            }
        },
        "additionalProperties": false
    })
}

fn graph_resolve_input_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "properties": {
            "path": {
                "type": "string",
                "minLength": 1,
                "description": "Generic Unreal object path, including values emitted by context.selection.items[*].path."
            },
            "objectPath": {
                "type": "string",
                "minLength": 1,
                "description": "Explicit Unreal object path."
            },
            "actorPath": {
                "type": "string",
                "minLength": 1,
                "description": "Actor object path."
            },
            "componentPath": {
                "type": "string",
                "minLength": 1,
                "description": "Actor component object path."
            },
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "Unreal asset path."
            },
            "graphType": graph_type_schema()
        },
        "anyOf": [
            { "required": ["path"] },
            { "required": ["objectPath"] },
            { "required": ["actorPath"] },
            { "required": ["componentPath"] },
            { "required": ["assetPath"] }
        ],
        "additionalProperties": false
    })
}

fn graph_resolve_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["resolvedGraphRefs", "diagnostics"],
        "properties": {
            "inputEcho": {
                "type": "object",
                "additionalProperties": true
            },
            "resolvedGraphRefs": {
                "type": "array",
                "items": {
                    "type": "object",
                    "required": ["graphType", "graphRef", "relation", "loadStatus"],
                    "properties": {
                        "graphType": graph_type_schema(),
                        "graphRef": graph_ref_schema(),
                        "relation": { "type": "string" },
                        "loadStatus": { "type": "string" }
                    },
                    "additionalProperties": true
                }
            },
            "diagnostics": {
                "type": "array",
                "items": { "type": "object", "additionalProperties": true }
            }
        },
        "additionalProperties": false
    })
}

fn graph_ops_input_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["graphType"],
        "properties": {
            "graphType": graph_type_schema(),
            "query": {
                "type": "string",
                "description": "Optional fuzzy text filter over known opIds, labels, tags, and summaries."
            },
            "stability": {
                "type": "string",
                "enum": ["stable", "experimental"],
                "description": "Optional filter by semantic op stability."
            },
            "limit": {
                "type": "integer",
                "minimum": 1,
                "maximum": 1000,
                "description": "Maximum number of semantic ops to return."
            }
        },
        "additionalProperties": false
    })
}

fn graph_ops_resolve_input_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["graphType", "graphRef", "items"],
        "properties": {
            "graphType": graph_type_schema(),
            "graphRef": graph_ref_schema(),
            "context": {
                "type": "object",
                "description": "Optional graph context used to resolve semantic ops, for example source pin, sink pin, or insertion edge context.",
                "properties": {
                    "fromPin": {
                        "type": "object",
                        "required": ["nodeId", "pinName"],
                        "properties": {
                            "nodeId": {
                                "type": "string",
                                "minLength": 1
                            },
                            "pinName": {
                                "type": "string",
                                "minLength": 1
                            }
                        },
                        "additionalProperties": false
                    },
                    "toPin": {
                        "type": "object",
                        "required": ["nodeId", "pinName"],
                        "properties": {
                            "nodeId": {
                                "type": "string",
                                "minLength": 1
                            },
                            "pinName": {
                                "type": "string",
                                "minLength": 1
                            }
                        },
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
                                    "nodeId": {
                                        "type": "string",
                                        "minLength": 1
                                    },
                                    "pinName": {
                                        "type": "string",
                                        "minLength": 1
                                    }
                                },
                                "additionalProperties": false
                            },
                            "toPin": {
                                "type": "object",
                                "required": ["nodeId", "pinName"],
                                "properties": {
                                    "nodeId": {
                                        "type": "string",
                                        "minLength": 1
                                    },
                                    "pinName": {
                                        "type": "string",
                                        "minLength": 1
                                    }
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
                        "opId": {
                            "type": "string",
                            "minLength": 1
                        },
                        "clientRef": {
                            "type": "string",
                            "description": "Optional client-side reference echoed in the result."
                        },
                        "hints": {
                            "type": "object",
                            "description": "Optional semantic resolution hints interpreted by the resolver.",
                            "additionalProperties": true
                        }
                    },
                    "additionalProperties": false
                }
            }
        },
        "additionalProperties": false
    })
}

fn graph_mutate_input_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["ops"],
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "Unreal asset path (Mode A). Required when graphName is used; omit when graphRef is provided."
            },
            "graphName": {
                "type": "string",
                "minLength": 1,
                "default": "EventGraph",
                "description": "Target graph name (Mode A). Defaults to EventGraph when omitted. Mutually exclusive with graphRef."
            },
            "graphRef": graph_ref_schema(),
            "graphType": graph_type_schema(),
            "expectedRevision": {
                "type": "string",
                "description": "Optional optimistic concurrency token from a prior graph read."
            },
            "idempotencyKey": {
                "type": "string",
                "description": "Optional client-supplied idempotency token."
            },
            "dryRun": {
                "type": "boolean",
                "default": false
            },
            "continueOnError": {
                "type": "boolean",
                "default": false,
                "description": "Continue applying later ops after an op failure."
            },
            "executionPolicy": {
                "type": "object",
                "properties": {
                    "stopOnError": {
                        "type": "boolean",
                        "default": true
                    },
                    "maxOps": {
                        "type": "integer",
                        "minimum": 1,
                        "maximum": 200
                    }
                },
                "additionalProperties": false
            },
            "ops": {
                "type": "array",
                "minItems": 1,
                "maxItems": 200,
                "items": {
                    "type": "object",
                    "required": ["op", "args"],
                    "properties": {
                        "op": {
                            "type": "string",
                            "enum": [
                                "addNode.byClass",
                                "connectPins",
                                "disconnectPins",
                                "breakPinLinks",
                                "setPinDefault",
                                "removeNode",
                                "moveNode",
                                "moveNodeBy",
                                "moveNodes",
                                "layoutGraph",
                                "compile",
                                "runScript"
                            ]
                        },
                        "clientRef": {
                            "type": "string",
                            "description": "Optional client-side reference name for later ops in the same request."
                        },
                        "targetGraphName": {
                            "type": "string",
                            "minLength": 1,
                            "description": "Optional per-op graph override by graph name. Mutually exclusive with targetGraphRef (or args.graphRef)."
                        },
                        "targetGraphRef": graph_ref_schema(),
                        "args": {
                            "type": "object",
                            "description": "Operation-specific arguments. Required keys depend on op. For mutate graph-addressing, args.graphRef is also accepted as an alias of targetGraphRef.",
                            "additionalProperties": true
                        }
                    },
                    "additionalProperties": false
                }
            }
        },
        "additionalProperties": false
    })
}

fn graph_list_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["graphType", "assetPath", "graphs", "diagnostics"],
        "properties": {
            "graphType": graph_type_schema(),
            "assetPath": { "type": "string" },
            "graphs": { "type": "array", "items": { "type": "object", "additionalProperties": true } },
            "diagnostics": { "type": "array", "items": { "type": "object", "additionalProperties": true } }
        },
        "additionalProperties": true
    })
}

fn graph_query_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["graphType", "assetPath", "graphName", "graphRef", "semanticSnapshot", "meta", "diagnostics"],
        "properties": {
            "graphType": graph_type_schema(),
            "assetPath": { "type": "string" },
            "graphName": { "type": "string" },
            "graphRef": graph_ref_schema(),
            "revision": { "type": "string" },
            "semanticSnapshot": { "type": "object", "additionalProperties": true },
            "nextCursor": { "type": "string" },
            "meta": { "type": "object", "additionalProperties": true },
            "diagnostics": { "type": "array", "items": { "type": "object", "additionalProperties": true } }
        },
        "additionalProperties": true
    })
}

fn graph_ops_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["graphType", "ops", "meta", "diagnostics"],
        "properties": {
            "graphType": graph_type_schema(),
            "ops": {
                "type": "array",
                "items": {
                    "type": "object",
                    "required": ["opId", "stability", "scope", "summary"],
                    "properties": {
                        "opId": {
                            "type": "string",
                            "minLength": 1
                        },
                        "stability": {
                            "type": "string",
                            "enum": ["stable", "experimental"]
                        },
                        "scope": {
                            "type": "string",
                            "enum": ["cross-graph", "blueprint", "material", "pcg"]
                        },
                        "summary": {
                            "type": "string"
                        },
                        "tags": {
                            "type": "array",
                            "items": { "type": "string" }
                        }
                    },
                    "additionalProperties": false
                }
            },
            "meta": {
                "type": "object",
                "required": ["source", "coverage"],
                "properties": {
                    "source": {
                        "type": "string",
                        "enum": ["loomle_catalog", "mixed"]
                    },
                    "coverage": {
                        "type": "string",
                        "enum": ["curated", "partial"]
                    }
                },
                "additionalProperties": false
            },
            "diagnostics": {
                "type": "array",
                "items": { "type": "object", "additionalProperties": true }
            }
        },
        "additionalProperties": false
    })
}

fn graph_ops_resolve_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["graphType", "graphRef", "results", "diagnostics"],
        "properties": {
            "graphType": graph_type_schema(),
            "graphRef": graph_ref_schema(),
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
    })
}

fn graph_mutate_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["applied", "partialApplied", "graphType", "assetPath", "graphName", "graphRef", "opResults", "diagnostics"],
        "properties": {
            "applied": { "type": "boolean" },
            "partialApplied": { "type": "boolean" },
            "graphType": graph_type_schema(),
            "assetPath": { "type": "string" },
            "graphName": { "type": "string" },
            "graphRef": graph_ref_schema(),
            "previousRevision": { "type": "string" },
            "newRevision": { "type": "string" },
            "code": { "type": "string" },
            "message": { "type": "string" },
            "opResults": {
                "type": "array",
                "items": {
                    "type": "object",
                    "required": ["index", "op", "ok", "changed", "errorCode", "errorMessage"],
                    "properties": {
                        "index": { "type": "integer" },
                        "op": { "type": "string" },
                        "ok": { "type": "boolean" },
                        "skipped": { "type": "boolean" },
                        "changed": { "type": "boolean" },
                        "nodeId": { "type": "string" },
                        "error": { "type": "string" },
                        "errorCode": { "type": "string" },
                        "errorMessage": { "type": "string" },
                        "skipReason": { "type": "string" },
                        "details": { "type": "object", "additionalProperties": true },
                        "movedNodeIds": { "type": "array", "items": { "type": "string" } },
                        "scriptResult": { "type": "object", "additionalProperties": true }
                    },
                    "additionalProperties": true
                }
            },
            "diagnostics": { "type": "array", "items": { "type": "object", "additionalProperties": true } }
        },
        "additionalProperties": true
    })
}

fn diag_tail_input_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "properties": {
            "fromSeq": {
                "type": "integer",
                "minimum": 0,
                "description": "Return events with seq > fromSeq. Defaults to 0."
            },
            "limit": {
                "type": "integer",
                "minimum": 1,
                "maximum": 1000,
                "default": 200,
                "description": "Maximum number of events to return."
            },
            "filters": {
                "type": "object",
                "properties": {
                    "severity": { "type": "string", "enum": ["error", "warning", "info"] },
                    "category": { "type": "string", "minLength": 1 },
                    "source": { "type": "string", "minLength": 1 },
                    "assetPathPrefix": { "type": "string", "minLength": 1 }
                },
                "additionalProperties": false
            }
        },
        "additionalProperties": false
    })
}

fn diag_tail_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["items", "nextSeq", "hasMore", "highWatermark"],
        "properties": {
            "items": { "type": "array", "items": { "type": "object", "additionalProperties": true } },
            "nextSeq": { "type": "integer", "minimum": 0 },
            "hasMore": { "type": "boolean" },
            "highWatermark": { "type": "integer", "minimum": 0 }
        },
        "additionalProperties": false
    })
}

fn graph_runtime_input_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["graphType"],
        "properties": {
            "graphType": {
                "type": "string",
                "enum": ["pcg"],
                "description": "Runtime graph domain. The first release supports only PCG."
            },
            "componentPath": {
                "type": "string",
                "minLength": 1,
                "description": "Runtime object path to a UPCGComponent."
            },
            "actorPath": {
                "type": "string",
                "minLength": 1,
                "description": "Runtime object path to an actor that owns a UPCGComponent."
            },
            "objectPath": {
                "type": "string",
                "minLength": 1,
                "description": "Generic runtime object path. May resolve to a UPCGComponent or an actor that owns one."
            },
            "path": {
                "type": "string",
                "minLength": 1,
                "description": "Alias for objectPath."
            }
        },
        "additionalProperties": false
    })
}

fn graph_runtime_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["componentPath", "generated", "generating", "generatedGraphOutput", "managedResources", "inspection", "diagnostics"],
        "properties": {
            "componentPath": { "type": "string" },
            "actorPath": { "type": "string" },
            "graphAssetPath": { "type": "string" },
            "generated": { "type": "boolean" },
            "generating": { "type": "boolean" },
            "managedResourcesAccessible": { "type": "boolean" },
            "generatedGraphOutput": { "type": "object", "additionalProperties": true },
            "managedResources": { "type": "object", "additionalProperties": true },
            "inspection": { "type": "object", "additionalProperties": true },
            "diagnostics": { "type": "array", "items": { "type": "object", "additionalProperties": true } }
        },
        "additionalProperties": true
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tools_list_contains_graph_resolve_and_diag_tail() {
        let tools = tool_descriptors();
        assert_eq!(tools.len(), 15);
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("graph.ops")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("graph.ops.resolve")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("graph.resolve")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("diag.tail")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("editor.open")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("editor.focus")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("editor.screenshot")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("graph.runtime")))));
    }

    #[test]
    fn graph_runtime_tool_schemas_expose_required_fields() {
        let tools = tool_descriptors();

        // graph.query: flexible addressing — no required fields at the schema level.
        let graph_query = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.query"))))
            .expect("graph.query descriptor");
        assert!(
            graph_query["inputSchema"]["properties"]["graphRef"].is_object(),
            "graph.query should expose graphRef property"
        );
        assert!(
            graph_query["inputSchema"]["properties"]["graphName"].is_object(),
            "graph.query should expose graphName property"
        );
        assert!(
            graph_query["inputSchema"]["properties"]["layoutDetail"].is_object(),
            "graph.query should expose layoutDetail property"
        );

        let graph_resolve = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.resolve"))))
            .expect("graph.resolve descriptor");
        assert!(
            graph_resolve["inputSchema"]["properties"]["path"].is_object(),
            "graph.resolve should expose path property"
        );
        assert!(
            graph_resolve["outputSchema"]["properties"]["resolvedGraphRefs"].is_object(),
            "graph.resolve output should expose resolvedGraphRefs property"
        );

        let graph_ops = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.ops"))))
            .expect("graph.ops descriptor");
        let graph_ops_required = graph_ops["inputSchema"]["required"]
            .as_array()
            .expect("required array");
        assert!(graph_ops_required.contains(&Value::String(String::from("graphType"))));
        assert!(
            graph_ops["outputSchema"]["properties"]["ops"].is_object(),
            "graph.ops output should expose ops property"
        );

        let graph_ops_resolve = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.ops.resolve"))))
            .expect("graph.ops.resolve descriptor");
        assert!(
            graph_ops_resolve["inputSchema"]["properties"]["graphRef"].is_object(),
            "graph.ops.resolve should expose graphRef property"
        );
        assert!(
            graph_ops_resolve["inputSchema"]["properties"]["items"].is_object(),
            "graph.ops.resolve should expose items property"
        );
        assert!(
            graph_ops_resolve["inputSchema"]["properties"]["context"]["properties"]["toPin"]
                .is_object(),
            "graph.ops.resolve should expose context.toPin"
        );
        assert!(
            graph_ops_resolve["inputSchema"]["properties"]["context"]["properties"]["edge"]
                .is_object(),
            "graph.ops.resolve should expose context.edge"
        );
        assert!(
            graph_ops_resolve["outputSchema"]["properties"]["results"].is_object(),
            "graph.ops.resolve output should expose results property"
        );

        // graph.mutate: only ops is required; assetPath is optional when graphRef is supplied.
        let graph_mutate = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.mutate"))))
            .expect("graph.mutate descriptor");
        let mutate_required = graph_mutate["inputSchema"]["required"]
            .as_array()
            .expect("required array");
        assert!(mutate_required.contains(&Value::String(String::from("ops"))));
        assert!(
            graph_mutate["inputSchema"]["properties"]["graphRef"].is_object(),
            "graph.mutate should expose graphRef property"
        );

        let diag_tail = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("diag.tail"))))
            .expect("diag.tail descriptor");
        assert!(
            diag_tail["inputSchema"]["properties"]["fromSeq"].is_object(),
            "diag.tail should expose fromSeq property"
        );
        assert!(
            diag_tail["outputSchema"]["properties"]["nextSeq"].is_object(),
            "diag.tail output should expose nextSeq property"
        );

        let graph_runtime = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.runtime"))))
            .expect("graph.runtime descriptor");
        assert!(
            graph_runtime["inputSchema"]["properties"]["graphType"].is_object(),
            "graph.runtime should expose graphType property"
        );
        assert!(
            graph_runtime["inputSchema"]["properties"]["componentPath"].is_object(),
            "graph.runtime should expose componentPath property"
        );
        assert!(
            graph_runtime["outputSchema"]["properties"]["managedResources"].is_object(),
            "graph.runtime should expose managedResources property"
        );
    }

    #[test]
    fn graph_runtime_tool_schemas_include_structured_op_and_graph_type_metadata() {
        let tools = tool_descriptors();
        let graph_list = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.list"))))
            .expect("graph.list descriptor");
        assert_eq!(
            graph_list["inputSchema"]["properties"]["graphType"]["default"],
            Value::String(String::from("blueprint"))
        );

        let graph_ops = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.ops"))))
            .expect("graph.ops descriptor");
        assert_eq!(
            graph_ops["outputSchema"]["properties"]["meta"]["properties"]["source"]["enum"][0],
            Value::String(String::from("loomle_catalog"))
        );

        let graph_ops_resolve = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.ops.resolve"))))
            .expect("graph.ops.resolve descriptor");
        assert!(
            graph_ops_resolve["outputSchema"]["properties"]["results"]["items"]["properties"]
                ["preferredPlan"]["properties"]["steps"]
                .is_object(),
            "graph.ops.resolve output schema should expose preferredPlan.steps"
        );
        assert!(
            graph_ops_resolve["outputSchema"]["properties"]["results"]["items"]["properties"]
                ["preferredPlan"]["properties"]["settingsTemplate"]
                .is_object(),
            "graph.ops.resolve output schema should expose preferredPlan.settingsTemplate"
        );
        assert!(
            graph_ops_resolve["outputSchema"]["properties"]["results"]["items"]["properties"]
                ["remediation"]
                .is_object(),
            "graph.ops.resolve output schema should expose remediation"
        );
        assert!(
            graph_ops_resolve["outputSchema"]["properties"]["results"]["items"]["properties"]
                ["preferredPlan"]["properties"]["executionHints"]
                .is_object(),
            "graph.ops.resolve output schema should expose preferredPlan.executionHints"
        );

        let graph_mutate = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.mutate"))))
            .expect("graph.mutate descriptor");
        let op_enum = graph_mutate["inputSchema"]["properties"]["ops"]["items"]["properties"]["op"]
            ["enum"]
            .as_array()
            .expect("op enum array");
        assert!(op_enum.contains(&Value::String(String::from("runScript"))));
        assert!(op_enum.contains(&Value::String(String::from("removeNode"))));
        assert!(op_enum.contains(&Value::String(String::from("moveNodeBy"))));
        assert!(op_enum.contains(&Value::String(String::from("moveNodes"))));
        assert!(op_enum.contains(&Value::String(String::from("layoutGraph"))));
        assert!(
            graph_mutate["inputSchema"]["properties"]["ops"]["items"]["properties"]
                ["targetGraphRef"]
                .is_object(),
            "graph.mutate op schema should expose targetGraphRef"
        );
        assert!(
            graph_mutate["inputSchema"]["properties"]["ops"]["items"]["properties"]
                ["targetGraphName"]
                .is_object(),
            "graph.mutate op schema should expose targetGraphName"
        );
        assert!(
            graph_mutate["outputSchema"]["properties"]["opResults"]["items"]["properties"]
                ["errorCode"]
                .is_object(),
            "graph.mutate output schema should expose opResults[].errorCode"
        );
        assert!(
            graph_mutate["outputSchema"]["properties"]["opResults"]["items"]["properties"]
                ["errorMessage"]
                .is_object(),
            "graph.mutate output schema should expose opResults[].errorMessage"
        );
        assert!(
            graph_mutate["outputSchema"]["properties"]["opResults"]["items"]["properties"]
                ["movedNodeIds"]
                .is_object(),
            "graph.mutate output schema should expose opResults[].movedNodeIds"
        );
    }
}
