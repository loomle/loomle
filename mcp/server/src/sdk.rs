use crate::{schema::tool_descriptors, McpService, RpcConnector, RpcMeta};
use rmcp::{
    model::{
        CallToolRequestParams, CallToolResult, Implementation, ListToolsResult,
        PaginatedRequestParams, ServerCapabilities, ServerInfo, Tool,
    },
    service::{RequestContext, RoleServer},
    ErrorData as McpError, ServerHandler,
};
use serde_json::{json, Value};
use std::future::Future;
use std::sync::Arc;

pub struct LoomleSdkServer<C: RpcConnector> {
    service: Arc<McpService<C>>,
    info: ServerInfo,
    tools: Arc<Vec<Tool>>,
}

impl<C: RpcConnector> LoomleSdkServer<C> {
    pub fn new(service: Arc<McpService<C>>) -> Self {
        Self {
            service,
            info: build_server_info(),
            tools: Arc::new(build_tools()),
        }
    }
}

impl<C: RpcConnector + 'static> ServerHandler for LoomleSdkServer<C> {
    fn get_info(&self) -> ServerInfo {
        self.info.clone()
    }

    fn list_tools(
        &self,
        request: Option<PaginatedRequestParams>,
        _context: RequestContext<RoleServer>,
    ) -> impl Future<Output = Result<ListToolsResult, McpError>> + Send + '_ {
        std::future::ready(match request.and_then(|params| params.cursor) {
            Some(cursor) => Err(McpError::invalid_params(
                "LOOMLE does not support paginated tool cursors",
                Some(json!({ "cursor": cursor })),
            )),
            None => Ok(ListToolsResult {
                meta: None,
                next_cursor: None,
                tools: self.tools.as_ref().clone(),
            }),
        })
    }

    fn get_tool(&self, name: &str) -> Option<Tool> {
        self.tools.iter().find(|tool| tool.name == name).cloned()
    }

    fn call_tool(
        &self,
        request: CallToolRequestParams,
        context: RequestContext<RoleServer>,
    ) -> impl Future<Output = Result<CallToolResult, McpError>> + Send + '_ {
        let service = Arc::clone(&self.service);
        async move {
            let tool_name = request.name.to_string();
            let meta = rpc_meta_for_request(&request, &context);
            let arguments = Value::Object(request.arguments.unwrap_or_default());
            let result =
                tokio::task::spawn_blocking(move || service.call_tool(&tool_name, arguments, meta))
                    .await
                    .map_err(|error| {
                        McpError::internal_error(
                            format!("tool worker join failed for {}: {}", request.name, error),
                            None,
                        )
                    })?;

            if result.is_error {
                Ok(CallToolResult::structured_error(result.structured_content))
            } else {
                Ok(CallToolResult::structured(result.structured_content))
            }
        }
    }
}

fn build_server_info() -> ServerInfo {
    ServerInfo::new(ServerCapabilities::builder().enable_tools().build())
        .with_server_info(Implementation::new(
            "loomle-mcp-server",
            env!("CARGO_PKG_VERSION"),
        ))
        .with_instructions("LOOMLE MCP server for Unreal Engine bridge tools.")
}

fn build_tools() -> Vec<Tool> {
    tool_descriptors()
        .into_iter()
        .map(|descriptor| {
            serde_json::from_value::<Tool>(descriptor)
                .expect("LOOMLE tool descriptor should be valid rmcp::model::Tool")
        })
        .collect()
}

fn rpc_meta_for_request(
    request: &CallToolRequestParams,
    context: &RequestContext<RoleServer>,
) -> RpcMeta {
    let trace_id = request
        .meta
        .as_ref()
        .and_then(|meta| meta.get("traceId"))
        .and_then(Value::as_str)
        .map(str::to_owned)
        .unwrap_or_else(|| format!("trace-{}", context.id));

    let deadline_ms = request
        .meta
        .as_ref()
        .and_then(|meta| meta.get("deadlineMs"))
        .and_then(Value::as_u64)
        .unwrap_or(10_000);

    RpcMeta {
        request_id: context.id.to_string(),
        trace_id,
        deadline_ms,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{RpcError, RpcHealth};
    use rmcp::ServiceExt;
    use serde_json::json;

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

        fn invoke(&self, tool: &str, _args: Value, meta: RpcMeta) -> Result<Value, RpcError> {
            Ok(json!({
                "tool": tool,
                "requestId": meta.request_id,
                "traceId": meta.trace_id,
                "deadlineMs": meta.deadline_ms
            }))
        }
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn sdk_server_lists_tools_over_rmcp() {
        let (client_stream, server_stream) = tokio::io::duplex(1024 * 1024);
        let server = LoomleSdkServer::new(Arc::new(McpService::new(StubConnector)));
        let server_task = tokio::spawn(async move { server.serve(server_stream).await });

        let mut client = ().serve(client_stream).await.expect("client");
        let mut server_running = server_task
            .await
            .expect("join server init")
            .expect("server");
        let tools = client.peer().list_all_tools().await.expect("list tools");

        assert_eq!(tools.len(), 13);
        assert!(tools.iter().any(|tool| tool.name == "graph.query"));
        assert!(tools.iter().any(|tool| tool.name == "diag.tail"));
        assert!(tools.iter().any(|tool| tool.name == "editor.open"));
        assert!(tools.iter().any(|tool| tool.name == "editor.focus"));
        assert!(tools.iter().any(|tool| tool.name == "editor.screenshot"));

        client.close().await.expect("close client");
        server_running.close().await.expect("close server");
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn sdk_server_routes_tool_calls_over_rmcp() {
        let (client_stream, server_stream) = tokio::io::duplex(1024 * 1024);
        let server = LoomleSdkServer::new(Arc::new(McpService::new(StubConnector)));
        let server_task = tokio::spawn(async move { server.serve(server_stream).await });

        let mut client = ().serve(client_stream).await.expect("client");
        let mut server_running = server_task
            .await
            .expect("join server init")
            .expect("server");
        let result = client
            .peer()
            .call_tool(CallToolRequestParams::new("context").with_arguments(serde_json::Map::new()))
            .await
            .expect("call tool");

        assert_eq!(result.is_error, Some(false));
        assert_eq!(
            result.structured_content.as_ref().unwrap()["tool"],
            "context"
        );
        assert_eq!(
            result.structured_content.as_ref().unwrap()["deadlineMs"],
            json!(10_000)
        );

        client.close().await.expect("close client");
        server_running.close().await.expect("close server");
    }
}
