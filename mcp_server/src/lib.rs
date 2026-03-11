use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

pub mod mcp;
pub mod transport;

pub const TOOL_NAMES: [&str; 8] = [
    "loomle",
    "context",
    "execute",
    "graph",
    "graph.list",
    "graph.query",
    "graph.actions",
    "graph.mutate",
];

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct RpcHealth {
    pub status: String,
    #[serde(rename = "rpcVersion")]
    pub rpc_version: String,
    pub timestamp: String,
    #[serde(default, rename = "isPIE")]
    pub is_pie: bool,
    #[serde(default, rename = "editorBusyReason")]
    pub editor_busy_reason: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RpcMeta {
    pub request_id: String,
    pub trace_id: String,
    pub deadline_ms: u64,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RpcError {
    pub code: u16,
    pub message: String,
    pub retryable: bool,
    pub detail: String,
}

pub trait RpcConnector: Send + Sync {
    fn health(&self) -> Result<RpcHealth, RpcError>;
    fn invoke(&self, tool: &str, args: Value, meta: RpcMeta) -> Result<Value, RpcError>;
}

#[derive(Clone, Debug, PartialEq)]
pub struct McpToolResult {
    pub structured_content: Value,
    pub is_error: bool,
}

pub struct McpService<C: RpcConnector> {
    connector: C,
}

impl<C: RpcConnector> McpService<C> {
    pub fn new(connector: C) -> Self {
        Self { connector }
    }

    pub fn tools_list() -> Vec<&'static str> {
        TOOL_NAMES.to_vec()
    }

    pub fn call_tool(&self, name: &str, args: Value, meta: RpcMeta) -> McpToolResult {
        match name {
            "loomle" => self.call_loomle(),
            "graph" => self.call_graph(args),
            "context" | "execute" | "graph.list" | "graph.query" | "graph.actions"
            | "graph.mutate" => self.call_runtime(name, args, meta),
            _ => McpToolResult {
                structured_content: error_payload(
                    1002,
                    format!("unknown tool: {name}"),
                    false,
                    String::new(),
                ),
                is_error: true,
            },
        }
    }

    fn call_loomle(&self) -> McpToolResult {
        match self.connector.health() {
            Ok(h) => {
                let status = if h.is_pie {
                    "blocked"
                } else {
                    h.status.as_str()
                };
                McpToolResult {
                    structured_content: json!({
                        "status": status,
                        "domainCode": if h.is_pie { "EDITOR_BUSY" } else { "" },
                        "message": if h.is_pie { "Unreal Editor is currently in Play In Editor (PIE)." } else { "" },
                        "runtime": {
                            "rpcConnected": true,
                            "listenerReady": true,
                            "isPIE": h.is_pie,
                            "editorBusyReason": h.editor_busy_reason,
                            "acceptsRuntimeTools": !h.is_pie,
                            "rpcHealth": {
                                "status": h.status,
                                "rpcVersion": h.rpc_version,
                                "timestamp": h.timestamp,
                                "isPIE": h.is_pie,
                                "editorBusyReason": h.editor_busy_reason
                            }
                        }
                    }),
                    is_error: false,
                }
            }
            Err(e) => McpToolResult {
                structured_content: json!({
                    "status": "error",
                    "runtime": {
                        "rpcConnected": false,
                        "listenerReady": false,
                        "rpcHealth": {
                            "status": "error",
                            "rpcVersion": "1.0",
                            "timestamp": "",
                            "probeError": e.detail
                        }
                    }
                }),
                is_error: false,
            },
        }
    }

    fn call_graph(&self, args: Value) -> McpToolResult {
        let graph_type = args
            .get("graphType")
            .and_then(Value::as_str)
            .unwrap_or("blueprint");

        match self.connector.health() {
            Ok(h) => {
                let status = if h.is_pie {
                    "blocked"
                } else {
                    h.status.as_str()
                };
                McpToolResult {
                    structured_content: json!({
                        "status": status,
                        "graphType": graph_type,
                        "version": "1.0",
                        "domainCode": if h.is_pie { "EDITOR_BUSY" } else { "" },
                        "message": if h.is_pie { "Unreal Editor is currently in Play In Editor (PIE)." } else { "" },
                        "ops": [
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
                        ],
                        "limits": {
                            "defaultLimit": 200,
                            "maxLimit": 1000,
                            "maxOpsPerMutate": 200
                        },
                        "runtime": {
                            "isPIE": h.is_pie,
                            "editorBusyReason": h.editor_busy_reason,
                            "acceptsRuntimeTools": !h.is_pie,
                            "rpcHealth": {
                                "status": h.status,
                                "rpcVersion": h.rpc_version,
                                "timestamp": h.timestamp,
                                "isPIE": h.is_pie,
                                "editorBusyReason": h.editor_busy_reason
                            }
                        }
                    }),
                    is_error: false,
                }
            }
            Err(e) => McpToolResult {
                structured_content: json!({
                    "status": "error",
                    "graphType": graph_type,
                    "version": "1.0",
                    "ops": [],
                    "limits": {
                        "defaultLimit": 200,
                        "maxLimit": 1000,
                        "maxOpsPerMutate": 200
                    },
                    "runtime": {
                        "rpcHealth": {
                            "status": "error",
                            "rpcVersion": "1.0",
                            "timestamp": "",
                            "probeError": e.detail
                        }
                    }
                }),
                is_error: false,
            },
        }
    }

    fn call_runtime(&self, tool: &str, args: Value, meta: RpcMeta) -> McpToolResult {
        if let Ok(h) = self.connector.health() {
            if h.is_pie {
                return McpToolResult {
                    structured_content: editor_busy_payload(tool, &h),
                    is_error: true,
                };
            }
        }

        match self.connector.invoke(tool, args, meta) {
            Ok(payload) => McpToolResult {
                structured_content: payload,
                is_error: false,
            },
            Err(err) => McpToolResult {
                structured_content: error_payload(err.code, err.message, err.retryable, err.detail),
                is_error: true,
            },
        }
    }
}

fn editor_busy_payload(tool: &str, health: &RpcHealth) -> Value {
    let busy_reason = if health.editor_busy_reason.is_empty() {
        "PIE_ACTIVE"
    } else {
        health.editor_busy_reason.as_str()
    };

    json!({
        "domainCode": "EDITOR_BUSY",
        "message": "Unreal Editor is currently in Play In Editor (PIE).",
        "retryable": true,
        "detail": format!("Tool `{tool}` was blocked because the editor reported {busy_reason}. Retry after PIE stops."),
        "busyReason": busy_reason,
        "runtime": {
            "isPIE": health.is_pie,
            "editorBusyReason": busy_reason,
            "rpcHealth": {
                "status": health.status,
                "rpcVersion": health.rpc_version,
                "timestamp": health.timestamp,
                "isPIE": health.is_pie,
                "editorBusyReason": busy_reason
            }
        }
    })
}

fn error_payload(code: u16, message: String, retryable: bool, detail: String) -> Value {
    json!({
        "domainCode": map_error_code(code),
        "message": message,
        "retryable": retryable,
        "detail": detail
    })
}

fn map_error_code(code: u16) -> &'static str {
    match code {
        1000 => "INVALID_ARGUMENT",
        1001 => "METHOD_NOT_FOUND",
        1002 => "TOOL_NOT_FOUND",
        1003 => "UNSUPPORTED_GRAPH_TYPE",
        1004 => "ASSET_NOT_FOUND",
        1005 => "GRAPH_NOT_FOUND",
        1006 => "NODE_NOT_FOUND",
        1007 => "PIN_NOT_FOUND",
        1008 => "REVISION_CONFLICT",
        1009 => "LIMIT_EXCEEDED",
        1010 => "EXECUTION_TIMEOUT",
        1011 => "INTERNAL_ERROR",
        1012 => "GRAPH_REF_INVALID",
        1013 => "GRAPH_REF_ASSET_NOT_LOADED",
        1014 => "GRAPH_REF_NOT_COMPOSITE",
        _ => "INTERNAL_ERROR",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::sync::{Arc, Mutex};
    use std::thread;
    use std::time::Duration;

    #[derive(Clone)]
    struct FakeConnector {
        state: Arc<Mutex<State>>,
    }

    struct State {
        health_calls: usize,
        health_result: RpcHealth,
        invoke_calls: Vec<(String, Value, RpcMeta)>,
        by_tool_result: HashMap<String, Result<Value, RpcError>>,
        out_of_order_ids: bool,
    }

    impl FakeConnector {
        fn new() -> Self {
            Self {
                state: Arc::new(Mutex::new(State {
                    health_calls: 0,
                    health_result: RpcHealth {
                        status: "ok".to_string(),
                        rpc_version: "1.0".to_string(),
                        timestamp: "2026-03-05T12:00:00Z".to_string(),
                        is_pie: false,
                        editor_busy_reason: String::new(),
                    },
                    invoke_calls: Vec::new(),
                    by_tool_result: HashMap::new(),
                    out_of_order_ids: false,
                })),
            }
        }

        fn with_out_of_order_ids(self) -> Self {
            self.state.lock().unwrap().out_of_order_ids = true;
            self
        }

        fn set_tool_result(&self, tool: &str, result: Result<Value, RpcError>) {
            self.state
                .lock()
                .unwrap()
                .by_tool_result
                .insert(tool.to_string(), result);
        }

        fn health_calls(&self) -> usize {
            self.state.lock().unwrap().health_calls
        }

        fn set_health_result(&self, health_result: RpcHealth) {
            self.state.lock().unwrap().health_result = health_result;
        }

        fn last_invoke(&self) -> Option<(String, Value, RpcMeta)> {
            self.state.lock().unwrap().invoke_calls.last().cloned()
        }
    }

    impl RpcConnector for FakeConnector {
        fn health(&self) -> Result<RpcHealth, RpcError> {
            let mut st = self.state.lock().unwrap();
            st.health_calls += 1;
            Ok(st.health_result.clone())
        }

        fn invoke(&self, tool: &str, args: Value, meta: RpcMeta) -> Result<Value, RpcError> {
            {
                let mut st = self.state.lock().unwrap();
                st.invoke_calls
                    .push((tool.to_string(), args.clone(), meta.clone()));
            }

            if self.state.lock().unwrap().out_of_order_ids {
                let delay = (meta.request_id.parse::<u64>().unwrap_or(0) % 5) * 3;
                thread::sleep(Duration::from_millis(30 - delay));
                return Ok(json!({ "requestId": meta.request_id }));
            }

            if let Some(result) = self.state.lock().unwrap().by_tool_result.get(tool).cloned() {
                return result;
            }

            Ok(json!({ "ok": true }))
        }
    }

    fn meta(id: &str) -> RpcMeta {
        RpcMeta {
            request_id: id.to_string(),
            trace_id: format!("trace-{id}"),
            deadline_ms: 10_000,
        }
    }

    #[test]
    fn tools_list_includes_eight_tools_and_graph_actions() {
        let tools = McpService::<FakeConnector>::tools_list();
        assert_eq!(tools.len(), 8);
        assert!(tools.contains(&"graph.actions"));
    }

    #[test]
    fn loomle_must_probe_health_each_call() {
        let connector = FakeConnector::new();
        let service = McpService::new(connector.clone());

        let _ = service.call_tool("loomle", json!({}), meta("1"));
        let _ = service.call_tool("loomle", json!({}), meta("2"));

        assert_eq!(connector.health_calls(), 2);
    }

    #[test]
    fn graph_must_probe_health_each_call() {
        let connector = FakeConnector::new();
        let service = McpService::new(connector.clone());

        let _ = service.call_tool("graph", json!({ "graphType": "blueprint" }), meta("1"));
        let _ = service.call_tool("graph", json!({ "graphType": "material" }), meta("2"));

        assert_eq!(connector.health_calls(), 2);
    }

    #[test]
    fn loomle_reports_blocked_during_pie() {
        let connector = FakeConnector::new();
        connector.set_health_result(RpcHealth {
            status: "ok".to_string(),
            rpc_version: "1.0".to_string(),
            timestamp: "2026-03-10T12:00:00Z".to_string(),
            is_pie: true,
            editor_busy_reason: "PIE_ACTIVE".to_string(),
        });

        let service = McpService::new(connector);
        let result = service.call_tool("loomle", json!({}), meta("3"));

        assert!(!result.is_error);
        assert_eq!(result.structured_content["status"], "blocked");
        assert_eq!(result.structured_content["runtime"]["isPIE"], true);
        assert_eq!(
            result.structured_content["runtime"]["acceptsRuntimeTools"],
            false
        );
    }

    #[test]
    fn context_maps_to_rpc_invoke_with_context_tool() {
        let connector = FakeConnector::new();
        let service = McpService::new(connector.clone());

        let _ = service.call_tool("context", json!({ "resolveIds": ["A"] }), meta("9"));
        let (tool, args, call_meta) = connector.last_invoke().expect("invoke call recorded");

        assert_eq!(tool, "context");
        assert_eq!(args["resolveIds"][0], "A");
        assert_eq!(call_meta.request_id, "9");
    }

    #[test]
    fn runtime_tools_fail_fast_during_pie_without_invoke() {
        let connector = FakeConnector::new();
        connector.set_health_result(RpcHealth {
            status: "ok".to_string(),
            rpc_version: "1.0".to_string(),
            timestamp: "2026-03-10T12:00:00Z".to_string(),
            is_pie: true,
            editor_busy_reason: "PIE_ACTIVE".to_string(),
        });

        let service = McpService::new(connector.clone());
        let result = service.call_tool("context", json!({}), meta("10"));

        assert!(result.is_error);
        assert_eq!(result.structured_content["domainCode"], "EDITOR_BUSY");
        assert!(connector.last_invoke().is_none());
    }

    #[test]
    fn graph_mutate_preserves_ops_order() {
        let connector = FakeConnector::new();
        let service = McpService::new(connector.clone());

        let ops = json!([
            {"op": "addNode.byClass", "args": {"nodeClassPath": "A"}},
            {"op": "connectPins", "args": {"from": 1, "to": 2}},
            {"op": "compile", "args": {}}
        ]);

        let _ = service.call_tool(
            "graph.mutate",
            json!({
                "graphType": "blueprint",
                "assetPath": "/Game/X",
                "graphName": "EventGraph",
                "expectedRevision": "r1",
                "idempotencyKey": "id-1",
                "ops": ops
            }),
            meta("12"),
        );

        let (_, args, _) = connector.last_invoke().expect("invoke call recorded");
        assert_eq!(args["ops"][0]["op"], "addNode.byClass");
        assert_eq!(args["ops"][1]["op"], "connectPins");
        assert_eq!(args["ops"][2]["op"], "compile");
    }

    #[test]
    fn maps_revision_conflict_error_to_domain_code() {
        let connector = FakeConnector::new();
        connector.set_tool_result(
            "graph.mutate",
            Err(RpcError {
                code: 1008,
                message: "revision conflict".to_string(),
                retryable: false,
                detail: "expected r2 got r3".to_string(),
            }),
        );

        let service = McpService::new(connector);
        let result = service.call_tool(
            "graph.mutate",
            json!({
                "graphType": "blueprint",
                "assetPath": "/Game/X",
                "graphName": "EventGraph",
                "expectedRevision": "r2",
                "idempotencyKey": "id-2",
                "ops": [{"op":"compile","args":{}}]
            }),
            meta("13"),
        );

        assert!(result.is_error);
        assert_eq!(result.structured_content["domainCode"], "REVISION_CONFLICT");
    }

    #[test]
    fn concurrent_requests_are_matched_by_request_id() {
        let connector = FakeConnector::new().with_out_of_order_ids();
        let service = Arc::new(McpService::new(connector));

        let mut handles = Vec::new();
        for i in 0..50_u64 {
            let svc = Arc::clone(&service);
            handles.push(thread::spawn(move || {
                let request_id = i.to_string();
                let res = svc.call_tool("context", json!({}), meta(&request_id));
                let echoed = res.structured_content["requestId"]
                    .as_str()
                    .expect("requestId exists");
                (request_id, echoed.to_string())
            }));
        }

        for h in handles {
            let (expected, actual) = h.join().expect("thread joins");
            assert_eq!(expected, actual);
        }
    }
}
