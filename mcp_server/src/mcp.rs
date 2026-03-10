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
                    "graphType": { "type": "string", "enum": ["k2", "material", "pcg"], "default": "k2" }
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
        ),
        runtime_tool_descriptor(
            "graph.query",
            "Graph Query",
            "Query semantic graph snapshot.",
        ),
        runtime_tool_descriptor(
            "graph.actions",
            "Graph Actions",
            "List addable actions for graph context.",
        ),
        runtime_tool_descriptor(
            "graph.mutate",
            "Graph Mutate",
            "Apply graph write operations in order.",
        ),
    ]
}

fn runtime_tool_descriptor(name: &str, title: &str, description: &str) -> Value {
    json!({
        "name": name,
        "title": title,
        "description": description,
        "inputSchema": {
            "$schema": "https://json-schema.org/draft/2020-12/schema",
            "type": "object"
        },
        "outputSchema": {
            "$schema": "https://json-schema.org/draft/2020-12/schema",
            "type": "object"
        }
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
    fn tools_list_contains_graph_actions() {
        let svc = McpService::new(StubConnector);
        let response = handle_request(
            &svc,
            json!({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}),
        )
        .expect("response");

        let tools = response["result"]["tools"].as_array().expect("array");
        assert_eq!(tools.len(), 8);
        assert!(tools
            .iter()
            .any(|v| v.get("name") == Some(&Value::String(String::from("graph.actions")))));
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
