use crate::{McpService, RpcConnector, RpcMeta};
use serde_json::{json, Value};

const MCP_PROTOCOL_VERSION: &str = "2025-11-05";

pub fn handle_request<C: RpcConnector>(service: &McpService<C>, request: Value) -> Option<Value> {
    let id = request.get("id").cloned().unwrap_or(Value::Null);

    let method = match request.get("method").and_then(Value::as_str) {
        Some(m) => m,
        None => {
            return Some(error_response(
                id,
                -32600,
                "Invalid Request",
                "missing method",
            ));
        }
    };

    match method {
        "initialize" => Some(ok_response(
            id,
            json!({
                "protocolVersion": MCP_PROTOCOL_VERSION,
                "capabilities": {
                    "tools": { "listChanged": false }
                },
                "serverInfo": {
                    "name": "loomle-mcp-server",
                    "version": env!("CARGO_PKG_VERSION")
                }
            }),
        )),
        "notifications/initialized" => None,
        "tools/list" => Some(ok_response(id, json!({ "tools": tool_descriptors() }))),
        "tools/call" => Some(handle_tools_call(service, id, request)),
        _ => Some(error_response(
            id,
            -32601,
            "Method not found",
            format!("unknown method: {method}"),
        )),
    }
}

fn handle_tools_call<C: RpcConnector>(service: &McpService<C>, id: Value, request: Value) -> Value {
    let params = request
        .get("params")
        .and_then(Value::as_object)
        .cloned()
        .unwrap_or_default();

    let tool_name = match params.get("name").and_then(Value::as_str) {
        Some(v) => v,
        None => {
            return error_response(
                id,
                -32602,
                "Invalid params",
                "tools/call requires params.name",
            );
        }
    };

    let args = params
        .get("arguments")
        .cloned()
        .unwrap_or_else(|| json!({}));

    let trace_id = params
        .get("_meta")
        .and_then(|m| m.get("traceId"))
        .and_then(Value::as_str)
        .map(str::to_owned)
        .unwrap_or_else(|| format!("trace-{}", id_to_string(&id)));

    let deadline_ms = params
        .get("_meta")
        .and_then(|m| m.get("deadlineMs"))
        .and_then(Value::as_u64)
        .unwrap_or(10_000);

    let meta = RpcMeta {
        request_id: id_to_string(&id),
        trace_id,
        deadline_ms,
    };

    let tool_result = service.call_tool(tool_name, args, meta);

    ok_response(
        id,
        json!({
            "structuredContent": tool_result.structured_content,
            "isError": tool_result.is_error,
            "content": [
                {
                    "type": "text",
                    "text": summary_text(tool_name, tool_result.is_error)
                }
            ]
        }),
    )
}

fn ok_response(id: Value, result: Value) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "result": result
    })
}

fn error_response(id: Value, code: i64, message: &str, detail: impl Into<String>) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "error": {
            "code": code,
            "message": message,
            "data": {
                "detail": detail.into()
            }
        }
    })
}

fn id_to_string(id: &Value) -> String {
    if let Some(s) = id.as_str() {
        return s.to_string();
    }

    if let Some(n) = id.as_i64() {
        return n.to_string();
    }

    if let Some(n) = id.as_u64() {
        return n.to_string();
    }

    if let Some(f) = id.as_f64() {
        return f.to_string();
    }

    String::from("unknown")
}

fn summary_text(tool: &str, is_error: bool) -> String {
    if is_error {
        return format!("{tool}: error");
    }

    format!("{tool}: completed")
}

fn tool_descriptors() -> Vec<Value> {
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
            "graph.actions",
            "Graph Actions",
            "List addable actions for graph context.",
            graph_actions_input_schema(),
            graph_actions_output_schema(),
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

fn graph_actions_input_schema() -> Value {
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
            "query": {
                "type": "string",
                "description": "Optional fuzzy search text used to filter available actions."
            },
            "limit": {
                "type": "integer",
                "minimum": 1,
                "maximum": 1000,
                "description": "Maximum number of actions to return."
            },
            "context": {
                "type": "object",
                "description": "Optional graph context used to scope returned actions.",
                "properties": {
                    "fromPin": {
                        "type": "object",
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
                                "addNode.byAction",
                                "connectPins",
                                "disconnectPins",
                                "breakPinLinks",
                                "setPinDefault",
                                "removeNode",
                                "moveNode",
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

fn graph_actions_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["graphType", "assetPath", "graphName", "graphRef", "actions", "meta", "diagnostics"],
        "properties": {
            "graphType": graph_type_schema(),
            "assetPath": { "type": "string" },
            "graphName": { "type": "string" },
            "graphRef": graph_ref_schema(),
            "contextEcho": { "type": "object", "additionalProperties": true },
            "actions": { "type": "array", "items": { "type": "object", "additionalProperties": true } },
            "nextCursor": { "type": "string" },
            "meta": { "type": "object", "additionalProperties": true },
            "diagnostics": { "type": "array", "items": { "type": "object", "additionalProperties": true } }
        },
        "additionalProperties": true
    })
}

fn graph_mutate_output_schema() -> Value {
    json!({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["applied", "graphType", "assetPath", "graphName", "graphRef", "opResults", "diagnostics"],
        "properties": {
            "applied": { "type": "boolean" },
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
                        "changed": { "type": "boolean" },
                        "nodeId": { "type": "string" },
                        "error": { "type": "string" },
                        "errorCode": { "type": "string" },
                        "errorMessage": { "type": "string" },
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{RpcError, RpcHealth};

    #[derive(Clone)]
    struct StubConnector;

    impl RpcConnector for StubConnector {
        fn health(&self) -> Result<RpcHealth, RpcError> {
            Ok(RpcHealth {
                status: String::from("ok"),
                rpc_version: String::from("1.0"),
                timestamp: String::from("2026-03-05T12:00:00Z"),
                is_pie: false,
                editor_busy_reason: String::new(),
            })
        }

        fn invoke(&self, tool: &str, _args: Value, _meta: RpcMeta) -> Result<Value, RpcError> {
            Ok(json!({"tool": tool, "ok": true}))
        }
    }

    #[test]
    fn initialize_returns_protocol_version() {
        let svc = McpService::new(StubConnector);
        let response = handle_request(
            &svc,
            json!({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}),
        )
        .expect("response");

        assert_eq!(response["result"]["protocolVersion"], MCP_PROTOCOL_VERSION);
    }

    #[test]
    fn tools_list_contains_graph_resolve_and_diag_tail() {
        let svc = McpService::new(StubConnector);
        let response = handle_request(
            &svc,
            json!({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}),
        )
        .expect("response");

        let tools = response["result"]["tools"].as_array().expect("array");
        assert_eq!(tools.len(), 10);
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("graph.actions")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("graph.resolve")))));
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("diag.tail")))));
    }

    #[test]
    fn graph_runtime_tool_schemas_expose_required_fields() {
        let svc = McpService::new(StubConnector);
        let response = handle_request(
            &svc,
            json!({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}),
        )
        .expect("response");

        let tools = response["result"]["tools"].as_array().expect("array");

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

        let graph_actions = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.actions"))))
            .expect("graph.actions descriptor");
        assert!(
            graph_actions["inputSchema"]["properties"]["graphRef"].is_object(),
            "graph.actions should expose graphRef property"
        );
        assert!(
            graph_actions["outputSchema"]["properties"]["graphRef"].is_object(),
            "graph.actions output should expose graphRef property"
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
    }

    #[test]
    fn graph_runtime_tool_schemas_include_structured_op_and_graph_type_metadata() {
        let svc = McpService::new(StubConnector);
        let response = handle_request(
            &svc,
            json!({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}),
        )
        .expect("response");

        let tools = response["result"]["tools"].as_array().expect("array");
        let graph_list = tools
            .iter()
            .find(|v| v.get("name") == Some(&Value::String(String::from("graph.list"))))
            .expect("graph.list descriptor");
        assert_eq!(
            graph_list["inputSchema"]["properties"]["graphType"]["default"],
            Value::String(String::from("blueprint"))
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
        assert!(
            graph_mutate["inputSchema"]["properties"]["ops"]["items"]["properties"]["targetGraphRef"]
                .is_object(),
            "graph.mutate op schema should expose targetGraphRef"
        );
        assert!(
            graph_mutate["inputSchema"]["properties"]["ops"]["items"]["properties"]["targetGraphName"]
                .is_object(),
            "graph.mutate op schema should expose targetGraphName"
        );
        assert!(
            graph_mutate["outputSchema"]["properties"]["opResults"]["items"]["properties"]["errorCode"]
                .is_object(),
            "graph.mutate output schema should expose opResults[].errorCode"
        );
        assert!(
            graph_mutate["outputSchema"]["properties"]["opResults"]["items"]["properties"]["errorMessage"]
                .is_object(),
            "graph.mutate output schema should expose opResults[].errorMessage"
        );
    }

    #[test]
    fn tools_call_routes_to_service() {
        let svc = McpService::new(StubConnector);
        let response = handle_request(
            &svc,
            json!({
                "jsonrpc":"2.0",
                "id":"req-9",
                "method":"tools/call",
                "params":{"name":"context","arguments":{}}
            }),
        )
        .expect("response");

        assert_eq!(response["result"]["isError"], false);
        assert_eq!(response["result"]["structuredContent"]["tool"], "context");
    }
}
