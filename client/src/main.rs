#![recursion_limit = "512"]
#![allow(dead_code)] // Temporary while tools/list reads the manifest and legacy schema builders remain for schema.inspect/tests.

mod schema_inspect;

use loomle::{
    resolve_project_root, rpc_capabilities, rpc_health, rpc_invoke,
    should_handoff_to_active_client, validate_project_root, Environment, RpcClientError,
    StartupAction, StartupError, StartupErrorKind,
};
use rmcp::{
    model::{
        CallToolRequestParams, CallToolResult, Implementation, ListToolsResult,
        PaginatedRequestParams, ServerCapabilities, ServerInfo, Tool,
    },
    service::RequestContext,
    transport::stdio,
    ErrorData as McpError, RoleServer, ServerHandler, ServiceExt,
};
use std::collections::{HashMap, HashSet};
use std::env;
use std::ffi::OsString;
use std::fs;
use std::fs::OpenOptions;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::process::{Command, ExitStatus, Stdio};
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::sync::Mutex;

use schema_inspect::{call_schema_inspect, schema_inspect_schema};

#[tokio::main(flavor = "multi_thread")]
async fn main() -> ExitCode {
    cleanup_old_binaries();

    if let Some(code) = maybe_handoff_to_global_active_client() {
        return code;
    }

    let command = match Cli::parse(env::args_os()) {
        Ok(cli) => cli,
        Err(message) => {
            eprintln!("{message}");
            eprintln!();
            print_usage_stderr();
            return ExitCode::from(2);
        }
    };

    match command {
        Cli::Help => {
            print_usage_stdout();
            ExitCode::SUCCESS
        }
        Cli::Mcp { project_root } => run_mcp(project_root).await,
        Cli::Doctor => run_doctor(),
        Cli::Update(update) => run_update(update),
    }
}

fn maybe_handoff_to_global_active_client() -> Option<ExitCode> {
    let state = match read_global_active_install_state() {
        Ok(Some(state)) => state,
        Ok(None) => return None,
        Err(error) => {
            emit_startup_error(&StartupError::new(
                StartupErrorKind::ActiveInstallStateInvalid,
                StartupAction::ReinstallLoomle,
                15,
                false,
                error,
            ));
            return Some(ExitCode::from(15));
        }
    };
    handoff_to_active_client_if_needed(&state)
}

async fn run_mcp(project_root_arg: Option<PathBuf>) -> ExitCode {
    let explicit_project_root = project_root_arg
        .as_deref()
        .and_then(|path| resolve_project_root(Some(path)).ok());
    let cwd = env::current_dir().ok();
    let online_projects = discover_runtime_projects(ProjectStatusFilter::Online);
    let project_root = infer_attached_project_root(explicit_project_root, cwd, &online_projects);
    let env_info = project_root.map(Environment::for_project_root);
    let server = LoomleProxyServer::new(env_info);
    let running = match server.serve(stdio()).await {
        Ok(running) => running,
        Err(error) => {
            emit_startup_error(
                &StartupError::new(
                    StartupErrorKind::StdioServerStartFailed,
                    StartupAction::VerifyRuntimeHealth,
                    13,
                    false,
                    "failed to start the LOOMLE stdio MCP service.",
                )
                .with_detail(error.to_string()),
            );
            return ExitCode::from(13);
        }
    };

    match running.waiting().await {
        Ok(_) => ExitCode::SUCCESS,
        Err(error) => {
            emit_startup_error(
                &StartupError::new(
                    StartupErrorKind::StdioServerRuntimeFailed,
                    StartupAction::VerifyRuntimeHealth,
                    14,
                    false,
                    "the LOOMLE stdio MCP service terminated unexpectedly.",
                )
                .with_detail(error.to_string()),
            );
            ExitCode::from(14)
        }
    }
}

fn run_doctor() -> ExitCode {
    let root = loomle_root();
    let active_state = root.join("install").join("active.json");
    let runtimes = root.join("state").join("runtimes");

    println!("LOOMLE doctor");
    println!("  installRoot: {}", root.display());
    println!(
        "  activeState: {} ({})",
        active_state.display(),
        exists_label(&active_state)
    );
    println!(
        "  runtimes: {} ({})",
        runtimes.display(),
        exists_label(&runtimes)
    );
    println!();
    println!("MCP host configuration:");
    println!(
        "  Codex:  codex mcp add loomle -- {}/bin/loomle mcp",
        root.display()
    );
    println!(
        "  Claude: claude mcp add --scope user loomle -- {}/bin/loomle mcp",
        root.display()
    );
    ExitCode::SUCCESS
}

fn run_update(options: UpdateOptions) -> ExitCode {
    match update_global_install(options) {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("[loomle][update] {message}");
            ExitCode::from(1)
        }
    }
}

fn exists_label(path: &std::path::Path) -> &'static str {
    if path.exists() {
        "exists"
    } else {
        "missing"
    }
}

fn home_dir() -> Option<PathBuf> {
    #[cfg(windows)]
    {
        env::var_os("USERPROFILE").map(PathBuf::from)
    }

    #[cfg(not(windows))]
    {
        env::var_os("HOME").map(PathBuf::from)
    }
}

fn handoff_to_active_client_if_needed(state: &loomle::ActiveInstallState) -> Option<ExitCode> {
    let current_exe = match env::current_exe() {
        Ok(path) => path,
        Err(error) => {
            emit_startup_error(
                &StartupError::new(
                    StartupErrorKind::HandoffFailed,
                    StartupAction::ReinstallLoomle,
                    15,
                    false,
                    "failed to resolve the current LOOMLE launcher executable.",
                )
                .with_detail(error.to_string()),
            );
            return Some(ExitCode::from(15));
        }
    };
    let handoff_decision = match should_handoff_to_active_client(&current_exe, &state) {
        Ok(value) => value,
        Err(error) => {
            emit_startup_error(&StartupError::new(
                StartupErrorKind::ActiveInstallStateInvalid,
                StartupAction::ReinstallLoomle,
                15,
                false,
                error,
            ));
            return Some(ExitCode::from(15));
        }
    };
    match handoff_decision {
        Some(true) => {}
        Some(false) | None => return None,
    }

    let mut child = match Command::new(&state.active_client_path)
        .args(env::args_os().skip(1))
        .stdin(Stdio::inherit())
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .spawn()
    {
        Ok(child) => child,
        Err(error) => {
            emit_startup_error(
                &StartupError::new(
                    StartupErrorKind::HandoffFailed,
                    StartupAction::ReinstallLoomle,
                    15,
                    false,
                    format!(
                        "failed to start the active LOOMLE client for version {}.",
                        state.active_version
                    ),
                )
                .with_detail(format!(
                    "{}: {}",
                    state.active_client_path.display(),
                    error
                )),
            );
            return Some(ExitCode::from(15));
        }
    };
    let status = match child.wait() {
        Ok(status) => status,
        Err(error) => {
            emit_startup_error(
                &StartupError::new(
                    StartupErrorKind::HandoffFailed,
                    StartupAction::ReinstallLoomle,
                    15,
                    false,
                    "failed while waiting for the active LOOMLE client to exit.",
                )
                .with_detail(format!(
                    "{}: {}",
                    state.active_client_path.display(),
                    error
                )),
            );
            return Some(ExitCode::from(15));
        }
    };
    Some(exit_code_from_status(status))
}

fn emit_startup_error(error: &StartupError) {
    match serde_json::to_string(error) {
        Ok(json) => eprintln!("[loomle][startup_error] {json}"),
        Err(encode_error) => eprintln!(
            "[loomle][startup_error] {{\"kind\":\"connect_failed\",\"action\":\"verify_runtime_health\",\"exitCode\":1,\"retryable\":false,\"message\":\"failed to encode startup error\",\"detail\":\"{}\"}}",
            encode_error
        ),
    }
}

fn exit_code_from_status(status: ExitStatus) -> ExitCode {
    match status.code() {
        Some(code) => ExitCode::from(code as u8),
        None => ExitCode::from(1),
    }
}

struct LoomleProxyServer {
    session_cwd: Option<PathBuf>,
    env_info: Mutex<Option<Arc<Environment>>>,
}

impl LoomleProxyServer {
    fn new(env_info: Option<Environment>) -> Self {
        Self {
            session_cwd: env::current_dir().ok(),
            env_info: Mutex::new(env_info.map(Arc::new)),
        }
    }

    async fn call_public_asset_tool(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<Option<CallToolResult>, McpError> {
        match tool_name {
            "asset.create" => Ok(Some(self.call_asset_create(args).await?)),
            "asset.inspect" => Ok(Some(self.call_asset_inspect(args).await?)),
            "asset.edit" => Ok(Some(self.call_asset_edit(args).await?)),
            _ => Ok(None),
        }
    }

    async fn call_asset_create(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let Some(kind) = args.get("kind").and_then(|value| value.as_str()) else {
            return Ok(invalid_argument_result("asset.create requires kind."));
        };
        let asset_path = match read_required_asset_path(&args, "asset.create") {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };

        match kind {
            "blueprint" => {
                let mut edit_args = rmcp::model::JsonObject::new();
                edit_args.insert("assetPath".into(), serde_json::json!(asset_path));
                edit_args.insert("operation".into(), serde_json::json!("create"));
                let mut operation_args = serde_json::Map::new();
                if let Some(parent_class_path) = args
                    .get("parentClassPath")
                    .or_else(|| args.get("parentClass"))
                    .and_then(|value| value.as_str())
                {
                    operation_args.insert(
                        "parentClassPath".into(),
                        serde_json::json!(parent_class_path),
                    );
                }
                edit_args.insert("args".into(), serde_json::Value::Object(operation_args));
                copy_mutation_controls(&args, &mut edit_args);
                self.runtime_call("blueprint.class.edit", edit_args).await
            }
            "enum" => {
                let mut edit_args = rmcp::model::JsonObject::new();
                edit_args.insert("assetPath".into(), serde_json::json!(asset_path));
                edit_args.insert("operation".into(), serde_json::json!("create"));
                edit_args.insert("args".into(), asset_enum_args_from_top_level(&args));
                copy_mutation_controls(&args, &mut edit_args);
                self.runtime_call("blueprint.enum.edit", edit_args).await
            }
            "userDefinedStruct" => {
                let mut edit_args = rmcp::model::JsonObject::new();
                edit_args.insert("assetPath".into(), serde_json::json!(asset_path));
                edit_args.insert("operation".into(), serde_json::json!("create"));
                edit_args.insert("args".into(), asset_struct_args_from_top_level(&args));
                copy_mutation_controls(&args, &mut edit_args);
                self.runtime_call("blueprint.struct.edit", edit_args).await
            }
            "material" | "materialFunction" | "pcgGraph" | "widgetBlueprint" => {
                self.runtime_call("asset.create", args).await
            }
            other => Ok(invalid_argument_result(format!(
                "Unsupported asset.create kind: {other}."
            ))),
        }
    }

    async fn call_asset_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let Some(kind) = args.get("kind").and_then(|value| value.as_str()) else {
            return Ok(invalid_argument_result("asset.inspect requires kind."));
        };
        match kind {
            "blueprint" => self.runtime_call("blueprint.inspect", args).await,
            "enum" => self.runtime_call("blueprint.enum.inspect", args).await,
            "userDefinedStruct" => self.runtime_call("blueprint.struct.inspect", args).await,
            "material" | "materialFunction" => {
                let mut inspect_args = args;
                inspect_args.remove("kind");
                let query_args = match translate_material_graph_inspect_args(&inspect_args) {
                    Ok(value) => value,
                    Err(error) => return Ok(error),
                };
                self.runtime_call("material.graph.inspect", query_args)
                    .await
            }
            "pcgGraph" => {
                let mut inspect_args = args;
                inspect_args.remove("kind");
                self.call_pcg_graph_inspect(inspect_args).await
            }
            "widgetBlueprint" => {
                let mut inspect_args = args;
                inspect_args.remove("kind");
                self.call_public_widget_tool("widget.tree.inspect", inspect_args)
                    .await?
                    .ok_or_else(|| McpError::internal_error("widget.tree.inspect missing", None))
            }
            other => Ok(invalid_argument_result(format!(
                "Unsupported asset.inspect kind: {other}."
            ))),
        }
    }

    async fn call_asset_edit(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let Some(operation) = args.get("operation").and_then(|value| value.as_str()) else {
            return Ok(invalid_argument_result("asset.edit requires operation."));
        };

        if operation == "updateMetadata" {
            return self.runtime_call("asset.edit", args).await;
        }

        let Some(kind) = args.get("kind").and_then(|value| value.as_str()) else {
            return Ok(invalid_argument_result(format!(
                "asset.edit operation {operation} requires kind."
            )));
        };

        match (kind, operation) {
            ("enum", "updateEntries") => {
                let asset_path = match read_required_asset_path(&args, "asset.edit") {
                    Ok(value) => value,
                    Err(error) => return Ok(error),
                };
                let mut edit_args = rmcp::model::JsonObject::new();
                edit_args.insert("assetPath".into(), serde_json::json!(asset_path));
                edit_args.insert("operation".into(), serde_json::json!("updateEntries"));
                edit_args.insert("args".into(), asset_enum_args_from_top_level(&args));
                copy_mutation_controls(&args, &mut edit_args);
                self.runtime_call("blueprint.enum.edit", edit_args).await
            }
            (
                "userDefinedStruct",
                "setTooltip" | "addField" | "removeField" | "renameField" | "changeFieldType"
                | "setFieldDefault" | "setFieldTooltip" | "setFieldMetadata" | "moveField",
            ) => {
                let asset_path = match read_required_asset_path(&args, "asset.edit") {
                    Ok(value) => value,
                    Err(error) => return Ok(error),
                };
                let mut edit_args = rmcp::model::JsonObject::new();
                edit_args.insert("assetPath".into(), serde_json::json!(asset_path));
                edit_args.insert("operation".into(), serde_json::json!(operation));
                if let Some(operation_args) = args.get("args").cloned() {
                    edit_args.insert("args".into(), operation_args);
                } else {
                    edit_args.insert("args".into(), asset_struct_args_from_top_level(&args));
                }
                copy_mutation_controls(&args, &mut edit_args);
                self.runtime_call("blueprint.struct.edit", edit_args).await
            }
            _ => Ok(invalid_argument_result(format!(
                "Unsupported asset.edit operation for kind {kind}: {operation}."
            ))),
        }
    }

    async fn attached_env_info(&self) -> Result<Arc<Environment>, String> {
        self.try_auto_attach().await;
        let env_info = self.env_info.lock().await.clone();
        env_info.ok_or_else(|| {
            "No Unreal project is attached. Use project.list to find online projects, \
             then project.attach to select one for this MCP session."
                .into()
        })
    }

    async fn ensure_runtime_ready(&self) -> Result<Arc<Environment>, String> {
        let env_info = self.attached_env_info().await?;
        match rpc_health(&env_info).await {
            Ok(_) => Ok(env_info),
            Err(RpcClientError::Startup(err)) => Err(format!(
                "Unreal Engine is not running or LoomleBridge is not loaded. \
                 Start Unreal Editor and wait for the Bridge to initialise. \
                 ({})",
                err.message
            )),
            Err(RpcClientError::Protocol(message)) => Err(format!(
                "LOOMLE runtime RPC protocol error. Verify that Unreal Editor is running and LoomleBridge is healthy. ({message})"
            )),
            Err(RpcClientError::Invoke(error)) => Err(format!(
                "LOOMLE runtime health check failed: {}",
                error.message
            )),
        }
    }

    async fn runtime_call(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let env_info = match self.ensure_runtime_ready().await {
            Ok(env_info) => env_info,
            Err(reason) => return Ok(bridge_unavailable_result(&reason)),
        };

        match rpc_invoke(&env_info, tool_name, args).await {
            Ok(payload) => Ok(CallToolResult::structured(
                self.add_observability_hint(&env_info, payload).await,
            )),
            Err(RpcClientError::Startup(err)) => Ok(bridge_unavailable_result(&format!(
                "Unreal Engine is not running or LoomleBridge is not loaded. \
                 Start Unreal Editor and wait for the Bridge to initialise. \
                 ({})",
                err.message
            ))),
            Err(RpcClientError::Invoke(error)) => {
                let payload = runtime_invoke_failure_payload(error);
                Ok(CallToolResult::structured_error(
                    self.add_observability_hint(&env_info, payload).await,
                ))
            }
            Err(RpcClientError::Protocol(message)) => Err(McpError::internal_error(
                format!("runtime RPC failed: {message}"),
                None,
            )),
        }
    }

    async fn call_play_wait(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let env_info = match self.ensure_runtime_ready().await {
            Ok(env_info) => env_info,
            Err(reason) => return Ok(bridge_unavailable_result(&reason)),
        };

        let timeout_ms = args
            .get("timeoutMs")
            .and_then(|value| value.as_u64())
            .unwrap_or(30_000)
            .clamp(1, 300_000);
        let target_session_state = args
            .get("until")
            .and_then(|value| value.as_object())
            .and_then(|until| until.get("session"))
            .and_then(|value| value.as_str())
            .unwrap_or("ready")
            .to_string();
        let participant_conditions = play_wait_participant_conditions_from_args(&args);
        let deadline = Instant::now() + Duration::from_millis(timeout_ms);

        loop {
            let status_args = serde_json::json!({"action": "status"})
                .as_object()
                .cloned()
                .unwrap_or_default();
            let payload = match rpc_invoke(&env_info, "play", status_args).await {
                Ok(payload) => payload,
                Err(RpcClientError::Startup(err)) => {
                    return Ok(bridge_unavailable_result(&format!(
                        "Unreal Engine is not running or LoomleBridge is not loaded. \
                         Start Unreal Editor and wait for the Bridge to initialise. \
                         ({})",
                        err.message
                    )));
                }
                Err(RpcClientError::Invoke(error)) => {
                    let payload = runtime_invoke_failure_payload(error);
                    return Ok(CallToolResult::structured_error(
                        self.add_observability_hint(&env_info, payload).await,
                    ));
                }
                Err(RpcClientError::Protocol(message)) => {
                    return Err(McpError::internal_error(
                        format!("runtime RPC failed: {message}"),
                        None,
                    ));
                }
            };

            let state = payload
                .get("session")
                .and_then(|session| session.get("state"))
                .and_then(|value| value.as_str())
                .unwrap_or("");
            let participant_conditions_met = args
                .get("until")
                .and_then(|value| value.as_object())
                .and_then(|until| until.get("participants"))
                .and_then(|value| value.as_array())
                .map(|conditions| play_participant_wait_conditions_met(&payload, conditions))
                .unwrap_or_else(|| {
                    participant_conditions.as_ref().is_none_or(|conditions| {
                        play_participant_wait_conditions_met(&payload, conditions)
                    })
                });
            if state == target_session_state && participant_conditions_met {
                return Ok(CallToolResult::structured(
                    self.add_observability_hint(&env_info, payload).await,
                ));
            }

            if Instant::now() >= deadline {
                let timeout_payload = serde_json::json!({
                    "isError": true,
                    "code": "PLAY_WAIT_TIMEOUT",
                    "message": format!("Timed out waiting for play session state '{target_session_state}'."),
                    "retryable": true,
                    "lastStatus": payload,
                });
                return Ok(CallToolResult::structured_error(
                    self.add_observability_hint(&env_info, timeout_payload)
                        .await,
                ));
            }

            tokio::time::sleep(Duration::from_millis(250)).await;
        }
    }

    async fn runtime_payload(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<serde_json::Value, McpError> {
        let env_info = match self.ensure_runtime_ready().await {
            Ok(env_info) => env_info,
            Err(reason) => {
                return Ok(serde_json::json!({
                    "isError": true,
                    "code": "BRIDGE_UNAVAILABLE",
                    "message": reason
                }))
            }
        };

        match rpc_invoke(&env_info, tool_name, args).await {
            Ok(payload) => Ok(payload),
            Err(RpcClientError::Startup(err)) => Ok(serde_json::json!({
                "isError": true,
                "code": "BRIDGE_UNAVAILABLE",
                "message": format!(
                    "Unreal Engine is not running or LoomleBridge is not loaded. Start Unreal Editor and wait for the Bridge to initialise. ({})",
                    err.message
                )
            })),
            Err(RpcClientError::Invoke(error)) => Ok(error.payload.unwrap_or_else(|| {
                serde_json::json!({
                    "isError": true,
                    "code": error.code,
                    "message": error.message,
                    "detail": error.detail,
                    "retryable": error.retryable,
                })
            })),
            Err(RpcClientError::Protocol(message)) => Err(McpError::internal_error(
                format!("runtime RPC failed: {message}"),
                None,
            )),
        }
    }

    async fn observability_state_for_env(
        &self,
        env_info: &Environment,
    ) -> Option<serde_json::Value> {
        let empty_args = serde_json::Map::new();
        let diagnostics_high_watermark =
            current_tail_high_watermark(env_info, "diagnostic.tail", &empty_args).await;
        let logs_high_watermark =
            current_tail_high_watermark(env_info, "log.tail", &empty_args).await;

        if diagnostics_high_watermark.is_none() && logs_high_watermark.is_none() {
            return None;
        }

        Some(serde_json::json!({
            "diagnostics": {
                "tool": "diagnostic.tail",
                "highWatermark": diagnostics_high_watermark.unwrap_or(0)
            },
            "logs": {
                "tool": "log.tail",
                "highWatermark": logs_high_watermark.unwrap_or(0)
            }
        }))
    }

    async fn add_observability_hint(
        &self,
        env_info: &Environment,
        mut payload: serde_json::Value,
    ) -> serde_json::Value {
        if !payload.is_object() {
            return payload;
        }

        let should_hint = payload.get("isError").and_then(|value| value.as_bool()) == Some(true)
            || payload
                .get("status")
                .and_then(|value| value.as_str())
                .is_some_and(|status| {
                    status.eq_ignore_ascii_case("error") || status.eq_ignore_ascii_case("warn")
                });

        if !should_hint || payload.get("observability").is_some() {
            return payload;
        }

        if let Some(observability) = self.observability_state_for_env(env_info).await {
            if let Some(object) = payload.as_object_mut() {
                object.insert("observability".to_string(), observability);
            }
        }
        payload
    }

    async fn attach_project(&self, project: RuntimeProject) {
        let env_info = Environment::for_project_root(project.project_root);
        *self.env_info.lock().await = Some(Arc::new(env_info));
    }

    async fn try_auto_attach(&self) {
        let mut guard = self.env_info.lock().await;
        if guard.is_some() {
            return;
        }

        let online_projects = discover_runtime_projects(ProjectStatusFilter::Online);
        let Some(project_root) =
            infer_attached_project_root(None, self.session_cwd.clone(), &online_projects)
        else {
            return;
        };

        *guard = Some(Arc::new(Environment::for_project_root(project_root)));
    }
}

impl ServerHandler for LoomleProxyServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo::new(ServerCapabilities::builder().enable_tools().build()).with_server_info(
            Implementation::new("loomle", env!("CARGO_PKG_VERSION"))
                .with_title("LOOMLE")
                .with_description(
                    "Global LOOMLE MCP server that attaches each session to a LOOMLE-enabled Unreal project.",
                ),
        )
    }

    async fn list_tools(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<RoleServer>,
    ) -> Result<ListToolsResult, McpError> {
        let tools = manifest_declared_tools_for("native").map_err(|message| {
            McpError::internal_error(format!("invalid LOOMLE tool manifest: {message}"), None)
        })?;
        Ok(ListToolsResult::with_all_items(tools))
    }

    fn get_tool(&self, _name: &str) -> Option<Tool> {
        None
    }

    async fn call_tool(
        &self,
        request: CallToolRequestParams,
        _context: RequestContext<RoleServer>,
    ) -> Result<CallToolResult, McpError> {
        let public_name = request.name.as_ref();
        let internal_name = public_tool_name_to_internal(public_name);
        let args = request.arguments.clone().unwrap_or_default();
        if is_local_tool(&internal_name) {
            return Ok(self.call_pre_attach_tool(&internal_name, args).await);
        }
        if let Some(result) = self
            .call_public_asset_tool(&internal_name, args.clone())
            .await?
        {
            return Ok(result);
        }
        if let Some(result) = self
            .call_public_blueprint_tool(&internal_name, args.clone())
            .await?
        {
            return Ok(result);
        }
        if let Some(result) = self
            .call_public_material_tool(&internal_name, args.clone())
            .await?
        {
            return Ok(result);
        }
        if let Some(result) = self
            .call_public_pcg_tool(&internal_name, args.clone())
            .await?
        {
            return Ok(result);
        }
        if let Some(result) = self
            .call_public_widget_tool(&internal_name, args.clone())
            .await?
        {
            return Ok(result);
        }
        if internal_name == "play"
            && request
                .arguments
                .as_ref()
                .and_then(|args| args.get("action"))
                .and_then(|value| value.as_str())
                == Some("wait")
        {
            return self.call_play_wait(args).await;
        }
        self.runtime_call(&internal_name, args).await
    }
}

impl LoomleProxyServer {
    async fn call_pre_attach_tool(
        &self,
        internal_name: &str,
        args: rmcp::model::JsonObject,
    ) -> CallToolResult {
        match internal_name {
            "status" => self.call_status().await,
            "project.list" => call_project_list(&args),
            "project.attach" => self.call_project_attach(&args).await,
            "project.install" => call_project_install(&args),
            "schema.inspect" => call_schema_inspect(&args),
            _ => bridge_unavailable_result("unknown pre-attach tool"),
        }
    }

    async fn call_status(&self) -> CallToolResult {
        self.try_auto_attach().await;
        let projects = discover_runtime_projects(ProjectStatusFilter::All);
        let online_project_count = projects
            .iter()
            .filter(|project| project.status == "online")
            .count();
        let env_info = self.env_info.lock().await.clone();
        let update_check = check_for_updates();
        let observability = if let Some(env_info) = env_info.as_ref() {
            tokio::time::timeout(
                Duration::from_millis(25),
                self.observability_state_for_env(env_info),
            )
            .await
            .ok()
            .flatten()
        } else {
            None
        };
        let mut issues = Vec::new();
        let mut runtime = serde_json::json!({
            "state": "no_project",
            "rpcConnected": false,
            "listenerReady": false,
            "isPIE": false,
            "editorBusyReason": "NO_PROJECT_ATTACHED",
            "health": null,
            "capabilities": null,
        });

        if let Some(env_info) = env_info.as_ref() {
            match tokio::time::timeout(Duration::from_millis(75), rpc_health(env_info)).await {
                Ok(Ok(health)) => {
                    let capabilities =
                        tokio::time::timeout(Duration::from_millis(50), rpc_capabilities(env_info))
                            .await
                            .ok()
                            .and_then(Result::ok);
                    let is_pie = health
                        .get("isPIE")
                        .and_then(|value| value.as_bool())
                        .unwrap_or(false);
                    let editor_busy_reason = health
                        .get("editorBusyReason")
                        .and_then(|value| value.as_str())
                        .unwrap_or("");
                    let health_status = health
                        .get("status")
                        .and_then(|value| value.as_str())
                        .unwrap_or("ready");
                    let state = match health_status {
                        "ok" | "ready" => "ready",
                        "degraded" => "degraded",
                        _ => "error",
                    };
                    runtime = serde_json::json!({
                        "state": state,
                        "rpcConnected": true,
                        "listenerReady": true,
                        "isPIE": is_pie,
                        "editorBusyReason": editor_busy_reason,
                        "health": health,
                        "capabilities": capabilities,
                    });
                    if state != "ready" {
                        issues.push(serde_json::json!({
                            "code": "RUNTIME_NOT_READY",
                            "severity": "warning",
                            "domain": "runtime",
                            "message": "The LOOMLE runtime is connected but not ready."
                        }));
                    }
                }
                Err(_) => {
                    runtime = serde_json::json!({
                        "state": "degraded",
                        "rpcConnected": false,
                        "listenerReady": true,
                        "isPIE": false,
                        "editorBusyReason": "RUNTIME_BUSY",
                        "health": null,
                        "capabilities": null,
                    });
                    issues.push(serde_json::json!({
                        "code": "RUNTIME_BUSY",
                        "severity": "warning",
                        "domain": "runtime",
                        "message": "The LOOMLE runtime did not answer the status health probe before the fast-return deadline."
                    }));
                }
                Ok(Err(RpcClientError::Startup(err))) => {
                    runtime = serde_json::json!({
                        "state": "unavailable",
                        "rpcConnected": false,
                        "listenerReady": false,
                        "isPIE": false,
                        "editorBusyReason": "RUNTIME_UNAVAILABLE",
                        "health": null,
                        "capabilities": null,
                    });
                    issues.push(serde_json::json!({
                        "code": "RUNTIME_UNAVAILABLE",
                        "severity": "error",
                        "domain": "runtime",
                        "message": err.message,
                    }));
                }
                Ok(Err(RpcClientError::Invoke(error))) => {
                    runtime = serde_json::json!({
                        "state": "error",
                        "rpcConnected": true,
                        "listenerReady": false,
                        "isPIE": false,
                        "editorBusyReason": "RUNTIME_ERROR",
                        "health": error.payload,
                        "capabilities": null,
                    });
                    issues.push(serde_json::json!({
                        "code": error.code.to_string(),
                        "severity": "error",
                        "domain": "runtime",
                        "message": error.message,
                    }));
                }
                Ok(Err(RpcClientError::Protocol(message))) => {
                    runtime = serde_json::json!({
                        "state": "error",
                        "rpcConnected": true,
                        "listenerReady": false,
                        "isPIE": false,
                        "editorBusyReason": "PROTOCOL_ERROR",
                        "health": null,
                        "capabilities": null,
                    });
                    issues.push(serde_json::json!({
                        "code": "PROTOCOL_ERROR",
                        "severity": "error",
                        "domain": "runtime",
                        "message": message,
                    }));
                }
            }
        } else {
            issues.push(serde_json::json!({
                "code": "NO_PROJECT_ATTACHED",
                "severity": "error",
                "domain": "project",
                "message": "No Unreal project is attached."
            }));
        }

        let runtime_state = runtime
            .get("state")
            .and_then(|value| value.as_str())
            .unwrap_or("error");
        let status = if runtime_state == "ready" {
            "ready"
        } else if runtime_state == "no_project" || runtime_state == "unavailable" {
            "offline"
        } else {
            "degraded"
        };
        let message = match status {
            "ready" => "Loomle runtime is ready.",
            "offline" => "Loomle runtime is offline.",
            _ => "Loomle runtime is degraded.",
        };

        structured_result(serde_json::json!({
            "schemaVersion": 1,
            "status": status,
            "message": message,
            "mcp": {
                "server": "native",
                "version": env!("CARGO_PKG_VERSION"),
            },
            "project": {
                "attached": env_info.is_some(),
                "root": env_info.as_ref().map(|env_info| env_info.project_root.display().to_string()),
                "endpoint": env_info.as_ref().map(|env_info| env_info.runtime_endpoint_path.display().to_string()),
                "discovered": {
                    "online": online_project_count,
                    "total": projects.len(),
                },
            },
            "runtime": runtime,
            "update": update_check,
            "observability": observability,
            "issues": issues,
        }))
    }

    async fn call_project_attach(&self, args: &rmcp::model::JsonObject) -> CallToolResult {
        let project_id = args
            .get("projectId")
            .and_then(|value| value.as_str())
            .map(str::to_owned);
        let project_root = args
            .get("projectRoot")
            .and_then(|value| value.as_str())
            .map(str::to_owned);

        if project_id.is_none() && project_root.is_none() {
            return CallToolResult::error(vec![rmcp::model::Content::text(
                "project.attach requires projectId or projectRoot.",
            )]);
        }

        let projects = discover_runtime_projects(ProjectStatusFilter::Online);
        let selected = projects
            .into_iter()
            .find(|project| {
                project_id
                    .as_ref()
                    .is_some_and(|id| *id == project.project_id)
                    || project_root
                        .as_ref()
                        .is_some_and(|root| *root == project.project_root.display().to_string())
            })
            .or_else(|| {
                if project_id.is_some() {
                    return None;
                }
                project_root
                    .as_deref()
                    .and_then(project_root_online_project)
            });

        let Some(project) = selected else {
            return CallToolResult::error(vec![rmcp::model::Content::text(
                "No online project matched projectId/projectRoot. Use project.list with status=online.",
            )]);
        };

        if !project.attachable {
            return CallToolResult::error(vec![rmcp::model::Content::text(format!(
                "Project is not attachable: {}",
                project
                    .reason
                    .unwrap_or_else(|| "unknown reason".to_string())
            ))]);
        }

        let payload = serde_json::json!({
            "attached": true,
            "projectId": project.project_id,
            "name": project.name,
            "projectRoot": project.project_root.display().to_string(),
            "endpoint": project.endpoint.display().to_string(),
        });
        self.attach_project(project).await;
        structured_result(payload)
    }

    async fn call_public_blueprint_tool(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<Option<CallToolResult>, McpError> {
        match tool_name {
            "blueprint.graph.list" => Ok(Some(self.call_blueprint_graph_list(args).await?)),
            "blueprint.graph.inspect" => Ok(Some(self.call_blueprint_graph_inspect(args).await?)),
            "blueprint.graph.edit" => Ok(Some(self.call_blueprint_graph_edit(args).await?)),
            "blueprint.graph.layout" => Ok(Some(self.call_blueprint_graph_layout(args).await?)),
            "blueprint.node.inspect" => Ok(Some(self.call_blueprint_node_inspect(args).await?)),
            "blueprint.node.edit" => Ok(Some(self.call_blueprint_node_edit(args).await?)),
            "blueprint.compile" => Ok(Some(self.call_blueprint_compile(args).await?)),
            "blueprint.inspect" => Ok(Some(self.call_blueprint_inspect(args).await?)),
            "blueprint.class.inspect" => Ok(Some(self.call_blueprint_class_inspect(args).await?)),
            "blueprint.class.edit" => Ok(Some(self.call_blueprint_class_edit(args).await?)),
            "blueprint.member.inspect" => Ok(Some(self.call_blueprint_member_inspect(args).await?)),
            "blueprint.member.edit" => Ok(Some(
                self.runtime_call("blueprint.member.edit", args).await?,
            )),
            "blueprint.graph.palette" => Ok(Some(self.call_blueprint_palette(args).await?)),
            _ => Ok(None),
        }
    }

    async fn call_public_material_tool(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<Option<CallToolResult>, McpError> {
        match tool_name {
            "material.palette" => Ok(Some(self.call_material_palette(args).await?)),
            "material.graph.inspect" => {
                let query_args = match translate_material_graph_inspect_args(&args) {
                    Ok(value) => value,
                    Err(error) => return Ok(Some(error)),
                };
                Ok(Some(
                    self.runtime_call("material.graph.inspect", query_args)
                        .await?,
                ))
            }
            "material.graph.edit" => Ok(Some(self.call_material_graph_edit(args).await?)),
            "material.graph.layout" => Ok(Some(self.call_material_graph_layout(args).await?)),
            "material.node.edit" => Ok(Some(self.call_material_node_edit(args).await?)),
            _ => Ok(None),
        }
    }

    async fn call_public_pcg_tool(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<Option<CallToolResult>, McpError> {
        match tool_name {
            "pcg.graph.inspect" => Ok(Some(self.call_pcg_graph_inspect(args).await?)),
            "pcg.palette" => Ok(Some(self.call_pcg_palette(args).await?)),
            "pcg.node.inspect" => Ok(Some(self.call_pcg_node_inspect(args).await?)),
            "pcg.parameter.inspect" => Ok(Some(self.call_pcg_parameter_inspect(args).await?)),
            "pcg.parameter.edit" => Ok(Some(self.call_pcg_parameter_edit(args).await?)),
            "pcg.graph.layout" => Ok(Some(self.call_pcg_graph_layout(args).await?)),
            "pcg.graph.edit" => Ok(Some(self.call_pcg_graph_edit(args).await?)),
            "pcg.compile" => Ok(Some(self.call_pcg_compile(args).await?)),
            _ => Ok(None),
        }
    }

    async fn call_public_widget_tool(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<Option<CallToolResult>, McpError> {
        match tool_name {
            "widget.tree.edit" => {
                let mutate_args = match translate_widget_tree_edit_args(&args) {
                    Ok(value) => value,
                    Err(error) => return Ok(Some(error)),
                };
                Ok(Some(
                    self.runtime_call("widget.tree.edit", mutate_args).await?,
                ))
            }
            "widget.edit" => {
                let mutate_args = match translate_widget_edit_args(&args) {
                    Ok(value) => value,
                    Err(error) => return Ok(Some(error)),
                };
                Ok(Some(self.runtime_call("widget.edit", mutate_args).await?))
            }
            "widget.tree.inspect" => {
                let inspect_args = translate_widget_tree_inspect_args(&args);
                let result = self
                    .runtime_call("widget.tree.inspect", inspect_args)
                    .await?;
                Ok(Some(shape_widget_tree_inspect_result(result, &args)))
            }
            "widget.inspect" => {
                let inspect_args = match translate_widget_inspect_args(&args) {
                    Ok(value) => value,
                    Err(error) => return Ok(Some(error)),
                };
                Ok(Some(
                    self.runtime_call("widget.inspect", inspect_args).await?,
                ))
            }
            "widget.event.create" => {
                Ok(Some(self.runtime_call("widget.event.create", args).await?))
            }
            "widget.compile" => Ok(Some(self.runtime_call("widget.compile", args).await?)),
            _ => Ok(None),
        }
    }

    async fn call_blueprint_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let payload = self.runtime_payload("blueprint.inspect", args).await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }

        let variables = payload
            .get("variables")
            .and_then(|value| value.as_array())
            .map_or(0, |items| items.len());
        let functions = payload
            .get("functions")
            .and_then(|value| value.as_array())
            .map_or(0, |items| items.len());
        let interface_functions = payload
            .get("interfaceFunctions")
            .and_then(|value| value.as_array())
            .map_or(0, |items| items.len());
        let components = payload
            .get("components")
            .and_then(|value| value.as_array())
            .map_or(0, |items| items.len());
        let interfaces = payload
            .get("implementedInterfaces")
            .and_then(|value| value.as_array())
            .map_or(0, |items| items.len());
        let event_signatures = payload
            .get("eventSignatures")
            .and_then(|value| value.as_array())
            .map_or(0, |items| items.len());

        Ok(structured_result(serde_json::json!({
            "assetPath": payload.get("assetPath").cloned().unwrap_or(serde_json::Value::Null),
            "blueprintClass": payload.get("blueprintClass").cloned().unwrap_or(serde_json::Value::Null),
            "parentClass": payload.get("parentClass").cloned().unwrap_or(serde_json::Value::Null),
            "parentClassPath": payload.get("parentClassPath").cloned().unwrap_or(serde_json::Value::Null),
            "implementedInterfaces": payload.get("implementedInterfaces").cloned().unwrap_or(serde_json::json!([])),
            "variables": payload.get("variables").cloned().unwrap_or(serde_json::json!([])),
            "functions": payload.get("functions").cloned().unwrap_or(serde_json::json!([])),
            "interfaceFunctions": payload.get("interfaceFunctions").cloned().unwrap_or(serde_json::json!([])),
            "macros": payload.get("macros").cloned().unwrap_or(serde_json::json!([])),
            "dispatchers": payload.get("dispatchers").cloned().unwrap_or(serde_json::json!([])),
            "eventSignatures": payload.get("eventSignatures").cloned().unwrap_or(serde_json::json!([])),
            "components": payload.get("components").cloned().unwrap_or(serde_json::json!([])),
            "routes": {
                "class": "blueprint.class.inspect",
                "members": "blueprint.member.inspect",
                "graphs": "blueprint_graph_list"
            },
            "summary": {
                "interfaceCount": interfaces,
                "variableCount": variables,
                "functionCount": functions,
                "interfaceFunctionCount": interface_functions,
                "componentCount": components,
                "eventSignatureCount": event_signatures
            }
        })))
    }

    async fn call_blueprint_class_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let payload = self.runtime_payload("blueprint.inspect", args).await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }

        Ok(structured_result(serde_json::json!({
            "assetPath": payload.get("assetPath").cloned().unwrap_or(serde_json::Value::Null),
            "blueprintClass": payload.get("blueprintClass").cloned().unwrap_or(serde_json::Value::Null),
            "parentClass": payload.get("parentClass").cloned().unwrap_or(serde_json::Value::Null),
            "parentClassPath": payload.get("parentClassPath").cloned().unwrap_or(serde_json::Value::Null),
            "class": payload.get("class").cloned().unwrap_or(serde_json::Value::Null),
            "settings": payload.get("settings").cloned().unwrap_or(serde_json::Value::Null),
            "implementedInterfaces": payload.get("implementedInterfaces").cloned().unwrap_or(serde_json::json!([])),
            "interfaceFunctions": payload.get("interfaceFunctions").cloned().unwrap_or(serde_json::json!([])),
            "classDefaults": payload.get("classDefaults").cloned().unwrap_or(serde_json::Value::Null),
            "metadata": payload.get("metadata").cloned().unwrap_or(serde_json::Value::Null),
        })))
    }

    async fn call_blueprint_class_edit(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let Some(operation) = args.get("operation").and_then(|value| value.as_str()) else {
            return Ok(invalid_argument_result(
                "blueprint.class.edit requires operation.",
            ));
        };
        if !matches!(
            operation,
            "setParent" | "setSettings" | "setDefault" | "addInterface" | "removeInterface"
        ) {
            return Ok(invalid_argument_result(format!(
                "Unsupported blueprint.class.edit operation: {operation}."
            )));
        }
        Ok(self.runtime_call("blueprint.class.edit", args).await?)
    }

    async fn call_blueprint_member_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let Some(member_kind) = args
            .get("memberKind")
            .and_then(|value| value.as_str())
            .map(str::to_owned)
        else {
            return Ok(CallToolResult::structured_error(serde_json::json!({
                "code": "INVALID_ARGUMENT",
                "message": "blueprint.member.inspect requires memberKind.",
                "retryable": false,
            })));
        };
        let name_filter = args
            .get("name")
            .and_then(|value| value.as_str())
            .map(str::to_owned);
        let payload = self.runtime_payload("blueprint.inspect", args).await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }

        let items = match member_kind.as_str() {
            "variable" => payload
                .get("variables")
                .cloned()
                .unwrap_or(serde_json::json!([])),
            "function" => payload
                .get("functions")
                .cloned()
                .unwrap_or(serde_json::json!([])),
            "component" => payload
                .get("components")
                .cloned()
                .unwrap_or(serde_json::json!([])),
            "macro" => payload
                .get("macros")
                .cloned()
                .unwrap_or(serde_json::json!([])),
            "dispatcher" => payload
                .get("dispatchers")
                .cloned()
                .unwrap_or(serde_json::json!([])),
            "event" | "customEvent" => payload
                .get("eventSignatures")
                .cloned()
                .unwrap_or(serde_json::json!([])),
            other => {
                return Ok(CallToolResult::structured_error(serde_json::json!({
                    "code": "INVALID_ARGUMENT",
                    "message": format!("Unsupported memberKind: {other}"),
                    "retryable": false,
                })));
            }
        };

        let filtered_items = items
            .as_array()
            .cloned()
            .unwrap_or_default()
            .into_iter()
            .filter(|item| {
                if member_kind == "customEvent"
                    && item.get("isCustomEvent").and_then(|value| value.as_bool()) != Some(true)
                {
                    return false;
                }
                name_filter.as_deref().is_none_or(|needle| {
                    item.get("name").and_then(|value| value.as_str()) == Some(needle)
                })
            })
            .collect::<Vec<_>>();

        Ok(structured_result(serde_json::json!({
            "assetPath": payload.get("assetPath").cloned().unwrap_or(serde_json::Value::Null),
            "memberKind": member_kind,
            "items": filtered_items
        })))
    }

    async fn call_blueprint_graph_list(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let legacy_args = match translate_blueprint_graph_list_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        Ok(self
            .runtime_call("blueprint.graph.list", legacy_args)
            .await?)
    }

    async fn call_blueprint_graph_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        if let Err(error) = validate_blueprint_graph_inspect_args(&args) {
            return Ok(error);
        }
        let legacy_args = match translate_blueprint_graph_inspect_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("blueprint.graph.inspect", legacy_args)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }

        let mut result = payload.as_object().cloned().unwrap_or_default();
        if !result.contains_key("graph") {
            if let Some(graph_id) = result
                .get("graphRef")
                .and_then(|value| value.as_object())
                .and_then(|graph_ref| graph_ref.get("id").or_else(|| graph_ref.get("graphId")))
                .and_then(|value| value.as_str())
            {
                result.insert("graph".into(), serde_json::json!({ "id": graph_id }));
            } else if let Some(graph_name) =
                result.get("graphName").and_then(|value| value.as_str())
            {
                result.insert("graph".into(), serde_json::json!({ "name": graph_name }));
            }
        }
        if let Some(error) = validate_blueprint_graph_inspect_targets(&result, &args) {
            return Ok(error);
        }
        shape_blueprint_graph_inspect_result(&mut result, &args);
        Ok(structured_result(serde_json::Value::Object(result)))
    }

    async fn call_blueprint_graph_edit(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let legacy_args = match translate_blueprint_graph_edit_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("blueprint.graph.edit", legacy_args)
            .await?;
        Ok(structured_result(payload))
    }

    async fn call_blueprint_node_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_blueprint_node_inspect_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("blueprint.node.inspect", translated)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_blueprint_node_edit(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_blueprint_node_edit_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("blueprint.node.edit", translated)
            .await?;
        Ok(structured_result(augment_blueprint_mutate_result(payload)))
    }

    async fn call_blueprint_graph_layout(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let request = match parse_blueprint_graph_layout_request(&args) {
            Ok(request) => request,
            Err(error) => return Ok(error),
        };

        let mut inspect_args = rmcp::model::JsonObject::new();
        inspect_args.insert("assetPath".into(), serde_json::json!(request.asset_path));
        write_optional_graph_address(&mut inspect_args, request.graph_address.clone());
        inspect_args.insert("includeConnections".into(), serde_json::json!(true));
        inspect_args.insert("includePinDefaults".into(), serde_json::json!(false));
        inspect_args.insert("includeComments".into(), serde_json::json!(false));
        inspect_args.insert("layoutDetail".into(), serde_json::json!("basic"));
        inspect_args.insert("limit".into(), serde_json::json!(1000));

        let inspect_payload = self
            .runtime_payload("blueprint.graph.inspect", inspect_args)
            .await?;
        if inspect_payload
            .get("isError")
            .and_then(|value| value.as_bool())
            == Some(true)
        {
            return Ok(CallToolResult::structured_error(inspect_payload));
        }

        let plan = match build_blueprint_graph_layout_plan(&request, &inspect_payload) {
            Ok(plan) => plan,
            Err(message) => return Ok(invalid_argument_result(message)),
        };

        if request.dry_run || !plan.changed() {
            return Ok(structured_result(plan.to_json(request.dry_run)));
        }

        let commands = plan.to_move_commands();
        let mut edit_args = rmcp::model::JsonObject::new();
        edit_args.insert("assetPath".into(), serde_json::json!(request.asset_path));
        if let Err(error) =
            write_public_blueprint_graph_address(&mut edit_args, request.graph_address.clone())
        {
            return Ok(error);
        }
        edit_args.insert("commands".into(), serde_json::Value::Array(commands));
        if let Some(expected_revision) = request.expected_revision {
            edit_args.insert(
                "expectedRevision".into(),
                serde_json::json!(expected_revision),
            );
        }

        let edit_result = self.call_blueprint_graph_edit(edit_args).await?;
        if edit_result.is_error == Some(true) {
            return Ok(edit_result);
        }

        Ok(structured_result(plan.to_json_from_edit_result(
            edit_result.structured_content.as_ref(),
        )))
    }

    async fn call_blueprint_compile(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let legacy_args = match translate_blueprint_compile_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("blueprint.compile", legacy_args)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }

        let mut result = payload
            .get("compileReport")
            .and_then(|value| value.as_object())
            .cloned()
            .unwrap_or_default();
        if let Some(asset_path) = payload.get("assetPath").cloned() {
            result.insert("assetPath".into(), asset_path);
        }
        if let Some(status) = payload.get("status").cloned() {
            result.insert("status".into(), status);
        }
        if let Some(diagnostics) = payload.get("diagnostics").cloned() {
            result.insert("diagnostics".into(), diagnostics);
        }
        if !result.contains_key("compiled") {
            result.insert("compiled".into(), serde_json::json!(false));
        }

        Ok(structured_result(serde_json::Value::Object(result)))
    }

    async fn call_blueprint_palette(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_blueprint_palette_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("blueprint.palette", translated)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_pcg_palette(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_pcg_palette_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self.runtime_payload("pcg.palette", translated).await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_material_palette(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_material_palette_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self.runtime_payload("material.palette", translated).await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_material_graph_edit(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_material_graph_edit_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("material.graph.edit", translated)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_material_graph_layout(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_material_graph_layout_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("material.graph.edit", translated)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_material_node_edit(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_material_node_edit_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("material.graph.edit", translated)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_pcg_graph_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        if let Err(error) = validate_pcg_graph_inspect_args(&args) {
            return Ok(error);
        }
        let translated = match translate_pcg_graph_inspect_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("pcg.graph.inspect", translated)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }

        let mut result = payload.as_object().cloned().unwrap_or_default();
        shape_pcg_graph_inspect_result(&mut result, &args);
        Ok(structured_result(serde_json::Value::Object(result)))
    }

    async fn call_pcg_node_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_pcg_node_inspect_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self.runtime_payload("pcg.node.inspect", translated).await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(shape_pcg_node_inspect_result(payload)))
    }

    async fn call_pcg_parameter_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_asset_or_graph_args(&args, "pcg.parameter.inspect") {
            Ok(mut value) => {
                if let Some(name) = args.get("name") {
                    value.insert("name".into(), name.clone());
                }
                value
            }
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("pcg.parameter.inspect", translated)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_pcg_parameter_edit(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_pcg_parameter_edit_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("pcg.parameter.edit", translated)
            .await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
    }

    async fn call_pcg_graph_edit(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_pcg_graph_edit_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self.runtime_payload("pcg.graph.edit", translated).await?;
        Ok(structured_result(augment_blueprint_mutate_result(payload)))
    }

    async fn call_pcg_graph_layout(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_pcg_graph_layout_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self.runtime_payload("pcg.graph.edit", translated).await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(augment_blueprint_mutate_result(payload)))
    }

    async fn call_pcg_compile(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let translated = match translate_pcg_compile_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self.runtime_payload("pcg.compile", translated).await?;
        if payload.get("isError").and_then(|value| value.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(shape_pcg_compile_result(payload)))
    }
}

fn invalid_argument_result(message: impl Into<String>) -> CallToolResult {
    tool_error_result("INVALID_ARGUMENT", message)
}

fn tool_error_result(code: &str, message: impl Into<String>) -> CallToolResult {
    CallToolResult::structured_error(serde_json::json!({
        "isError": true,
        "code": code,
        "message": message.into(),
        "retryable": false,
    }))
}

async fn current_tail_high_watermark(
    env_info: &Environment,
    tool_name: &str,
    base_args: &serde_json::Map<String, serde_json::Value>,
) -> Option<u64> {
    let mut args = base_args.clone();
    args.insert("fromSeq".to_string(), serde_json::json!(0));
    args.insert("limit".to_string(), serde_json::json!(1));
    let payload = rpc_invoke(env_info, tool_name, args).await.ok()?;
    payload
        .get("highWatermark")
        .and_then(|value| value.as_u64())
}

fn read_required_asset_path(
    args: &rmcp::model::JsonObject,
    tool_name: &str,
) -> Result<String, CallToolResult> {
    args.get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .or_else(|| {
            args.get("graphRef")
                .and_then(|value| value.as_object())
                .and_then(|graph_ref| graph_ref.get("assetPath"))
                .and_then(|value| value.as_str())
                .filter(|value| !value.is_empty())
        })
        .map(str::to_owned)
        .ok_or_else(|| invalid_argument_result(format!("{tool_name} requires assetPath.")))
}

#[derive(Clone, Debug)]
enum BlueprintGraphAddress {
    Name(String),
    Ref(serde_json::Map<String, serde_json::Value>),
}

fn read_optional_graph_address(
    args: &rmcp::model::JsonObject,
    asset_path: &str,
    require_graph: bool,
    tool_name: &str,
) -> Result<Option<BlueprintGraphAddress>, CallToolResult> {
    if let Some(graph_name) = args
        .get("graphName")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
    {
        return Ok(Some(BlueprintGraphAddress::Name(graph_name.to_owned())));
    }

    if let Some(graph_ref) = args.get("graphRef").and_then(|value| value.as_object()) {
        return Ok(Some(BlueprintGraphAddress::Ref(graph_ref.clone())));
    }

    if let Some(graph_obj) = args.get("graph").and_then(|value| value.as_object()) {
        if let Some(graph_id) = graph_obj
            .get("id")
            .and_then(|value| value.as_str())
            .filter(|value| !value.is_empty())
        {
            let mut graph_ref = serde_json::Map::new();
            graph_ref.insert("kind".into(), serde_json::json!("asset"));
            graph_ref.insert("assetPath".into(), serde_json::json!(asset_path));
            graph_ref.insert("graphId".into(), serde_json::json!(graph_id));
            return Ok(Some(BlueprintGraphAddress::Ref(graph_ref)));
        }
        if let Some(graph_name) = graph_obj
            .get("name")
            .and_then(|value| value.as_str())
            .filter(|value| !value.is_empty())
        {
            return Ok(Some(BlueprintGraphAddress::Name(graph_name.to_owned())));
        }
    }

    if require_graph {
        Err(invalid_argument_result(format!(
            "{tool_name} requires graph or graphName."
        )))
    } else {
        Ok(None)
    }
}

fn write_optional_graph_address(
    target: &mut rmcp::model::JsonObject,
    address: Option<BlueprintGraphAddress>,
) {
    match address {
        Some(BlueprintGraphAddress::Name(graph_name)) => {
            target.insert("graphName".into(), serde_json::json!(graph_name));
        }
        Some(BlueprintGraphAddress::Ref(graph_ref)) => {
            target.insert("graphRef".into(), serde_json::Value::Object(graph_ref));
        }
        None => {}
    }
}

fn write_public_blueprint_graph_address(
    target: &mut rmcp::model::JsonObject,
    address: Option<BlueprintGraphAddress>,
) -> Result<(), CallToolResult> {
    match address {
        Some(BlueprintGraphAddress::Name(graph_name)) => {
            target.insert("graph".into(), serde_json::json!({ "name": graph_name }));
        }
        Some(BlueprintGraphAddress::Ref(graph_ref)) => {
            if let Some(graph_id) = graph_ref
                .get("graphId")
                .and_then(|value| value.as_str())
                .filter(|value| !value.is_empty())
            {
                target.insert("graph".into(), serde_json::json!({ "id": graph_id }));
            } else if let Some(graph_name) = graph_ref
                .get("name")
                .or_else(|| graph_ref.get("graphName"))
                .and_then(|value| value.as_str())
                .filter(|value| !value.is_empty())
            {
                target.insert("graph".into(), serde_json::json!({ "name": graph_name }));
            } else {
                return Err(invalid_argument_result(
                    "blueprint graph address cannot be written as public graph:{id|name}.",
                ));
            }
        }
        None => {}
    }
    Ok(())
}

fn copy_if_present(
    source: &rmcp::model::JsonObject,
    target: &mut rmcp::model::JsonObject,
    field: &str,
) {
    if let Some(value) = source.get(field) {
        target.insert(field.to_owned(), value.clone());
    }
}

fn validate_integer_range(
    object: &rmcp::model::JsonObject,
    field: &str,
    min: i64,
    max: i64,
    tool_name: &str,
) -> Result<(), CallToolResult> {
    let Some(value) = object.get(field) else {
        return Ok(());
    };
    let Some(number) = value.as_i64() else {
        return Err(invalid_argument_result(format!(
            "{tool_name} {field} must be an integer."
        )));
    };
    if number < min || number > max {
        return Err(invalid_argument_result(format!(
            "{tool_name} {field} must be between {min} and {max}."
        )));
    }
    Ok(())
}

fn normalize_blueprint_palette_from_pins(
    value: &serde_json::Value,
    tool_name: &str,
) -> Result<serde_json::Value, CallToolResult> {
    let Some(items) = value.as_array() else {
        return Err(invalid_argument_result(format!(
            "{tool_name} fromPins must be an array."
        )));
    };
    let mut normalized = Vec::new();
    for (index, item) in items.iter().enumerate() {
        let Some(object) = item.as_object() else {
            return Err(invalid_argument_result(format!(
                "{tool_name} fromPins[{index}] must be an object."
            )));
        };
        let node_id = object
            .get("node")
            .and_then(|value| value.as_object())
            .and_then(|node| node.get("id"))
            .and_then(|value| value.as_str())
            .filter(|value| !value.is_empty())
            .ok_or_else(|| {
                invalid_argument_result(format!("{tool_name} fromPins[{index}] requires node.id."))
            })?;
        let pin = object
            .get("pin")
            .and_then(|value| value.as_str())
            .filter(|value| !value.is_empty())
            .ok_or_else(|| {
                invalid_argument_result(format!("{tool_name} fromPins[{index}] requires pin."))
            })?;
        normalized.push(serde_json::json!({"nodeId": node_id, "pin": pin}));
    }
    Ok(serde_json::Value::Array(normalized))
}

fn copy_mutation_controls(source: &rmcp::model::JsonObject, target: &mut rmcp::model::JsonObject) {
    for field in [
        "dryRun",
        "returnDiff",
        "returnDiagnostics",
        "expectedRevision",
    ] {
        copy_if_present(source, target, field);
    }
}

fn copy_dry_run_revision_controls(
    source: &rmcp::model::JsonObject,
    target: &mut rmcp::model::JsonObject,
) {
    for field in ["dryRun", "expectedRevision"] {
        copy_if_present(source, target, field);
    }
}

fn asset_enum_args_from_top_level(args: &rmcp::model::JsonObject) -> serde_json::Value {
    if let Some(args_object) = args.get("args").and_then(|value| value.as_object()) {
        return serde_json::Value::Object(args_object.clone());
    }

    let mut enum_args = serde_json::Map::new();
    copy_if_present(args, &mut enum_args, "entries");
    copy_if_present(args, &mut enum_args, "displayNames");
    serde_json::Value::Object(enum_args)
}

fn asset_struct_args_from_top_level(args: &rmcp::model::JsonObject) -> serde_json::Value {
    if let Some(args_object) = args.get("args").and_then(|value| value.as_object()) {
        return serde_json::Value::Object(args_object.clone());
    }

    let mut struct_args = serde_json::Map::new();
    for field in [
        "fields",
        "tooltip",
        "toolTip",
        "fieldId",
        "id",
        "name",
        "fieldName",
        "newName",
        "displayName",
        "type",
        "defaultValue",
        "value",
        "metadata",
        "removeKeys",
        "relativeToFieldId",
        "targetFieldId",
        "position",
    ] {
        copy_if_present(args, &mut struct_args, field);
    }
    serde_json::Value::Object(struct_args)
}

fn extract_query_graph_asset_path(
    args: &rmcp::model::JsonObject,
    tool_name: &str,
) -> Result<Option<String>, CallToolResult> {
    let graph_obj = args
        .get("graph")
        .or_else(|| args.get("graphRef"))
        .and_then(|value| value.as_object());
    let Some(graph_obj) = graph_obj else {
        return Ok(None);
    };

    if let Some(kind) = graph_obj.get("kind").and_then(|value| value.as_str()) {
        if kind != "asset" {
            return Err(invalid_argument_result(&format!(
                "{tool_name} supports only asset graph references."
            )));
        }
    }

    let asset_path = graph_obj
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| {
            invalid_argument_result(&format!("{tool_name} graph references require assetPath."))
        })?;
    Ok(Some(asset_path.to_owned()))
}

fn translate_asset_query_args(
    args: &rmcp::model::JsonObject,
    tool_name: &str,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, tool_name)?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| {
            invalid_argument_result(&format!("{tool_name} requires assetPath or graph."))
        })?;

    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    for field in ["nodeIds", "nodeClasses", "includeConnections", "graphName"] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
}

fn translate_material_graph_inspect_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    translate_asset_query_args(args, "material.graph.inspect")
}

fn translate_material_palette_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, "material_palette")?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| invalid_argument_result("material_palette requires assetPath or graph."))?;

    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    for field in ["query", "elementTypes", "limit", "offset"] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
}

fn compile_material_add_from_palette_command(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Result<Vec<serde_json::Value>, String> {
    let entry = command
        .get("entry")
        .and_then(|value| value.as_object())
        .ok_or_else(|| "addFromPalette requires entry from material_palette.".to_owned())?;
    let entry_id = entry
        .get("id")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "addFromPalette requires entry.id.".to_owned())?;
    if entry.get("executable").and_then(|value| value.as_bool()) == Some(false) {
        return Err(format!(
            "material_palette entry is not executable: {entry_id}"
        ));
    }
    let payload = entry
        .get("payload")
        .and_then(|value| value.as_object())
        .ok_or_else(|| "addFromPalette requires entry.payload.".to_owned())?;
    let node_class_path = payload
        .get("nodeClassPath")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "material_palette entry.payload requires nodeClassPath.".to_owned())?;

    let mut op = serde_json::Map::new();
    op.insert("op".into(), serde_json::json!("addNode.byClass"));
    op.insert("nodeClassPath".into(), serde_json::json!(node_class_path));
    op.insert("entryId".into(), serde_json::json!(entry_id));
    op.insert("entry".into(), serde_json::Value::Object(entry.clone()));
    if let Some(position) = command.get("position").and_then(|value| value.as_object()) {
        if let Some(x) = position.get("x") {
            op.insert("x".into(), x.clone());
        }
        if let Some(y) = position.get("y") {
            op.insert("y".into(), y.clone());
        }
    }
    for field in ["anchor", "near", "from", "target", "parameterName"] {
        copy_if_present(command, &mut op, field);
    }
    if let Some(alias) = command.get("alias").and_then(|value| value.as_str()) {
        op.insert("clientRef".into(), serde_json::json!(alias));
    }
    Ok(vec![serde_json::Value::Object(op)])
}

fn compile_material_graph_commands(
    commands: &[serde_json::Value],
) -> Result<Vec<serde_json::Value>, CallToolResult> {
    let mut ops = Vec::new();
    for (index, command) in commands.iter().enumerate() {
        let Some(command_obj) = command.as_object() else {
            return Err(invalid_argument_result(format!(
                "material.graph.edit command at index {index} must be an object."
            )));
        };
        let Some(kind) = command_obj.get("kind").and_then(|value| value.as_str()) else {
            return Err(invalid_argument_result(format!(
                "material.graph.edit command at index {index} requires kind."
            )));
        };

        let compiled = match kind {
            "addFromPalette" => compile_material_add_from_palette_command(command_obj),
            "removeNode" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "removeNode requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let mut op = node.as_object().cloned().unwrap_or_default();
                op.insert("op".into(), serde_json::json!("removeNode"));
                Ok(vec![serde_json::Value::Object(op)])
            }
            "moveNode" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "moveNode requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let mut op = node.as_object().cloned().unwrap_or_default();
                let op_name = if let Some(position) = command_obj
                    .get("position")
                    .and_then(|value| value.as_object())
                {
                    if let Some(x) = position.get("x") {
                        op.insert("x".into(), x.clone());
                    }
                    if let Some(y) = position.get("y") {
                        op.insert("y".into(), y.clone());
                    }
                    "moveNode"
                } else if let Some(delta) =
                    command_obj.get("delta").and_then(|value| value.as_object())
                {
                    if let Some(dx) = delta.get("x") {
                        op.insert("dx".into(), dx.clone());
                    }
                    if let Some(dy) = delta.get("y") {
                        op.insert("dy".into(), dy.clone());
                    }
                    "moveNodeBy"
                } else {
                    return Err(invalid_argument_result(
                        "moveNode requires position or delta.",
                    ));
                };
                op.insert("op".into(), serde_json::json!(op_name));
                Ok(vec![serde_json::Value::Object(op)])
            }
            "connect" | "disconnect" => {
                let from = command_obj
                    .get("from")
                    .ok_or_else(|| format!("{kind} requires from."))
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let to = command_obj
                    .get("to")
                    .ok_or_else(|| format!("{kind} requires to."))
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                Ok(vec![serde_json::json!({
                    "op": if kind == "connect" { "connectPins" } else { "disconnectPins" },
                    "from": from,
                    "to": to
                })])
            }
            "breakPinLinks" => {
                let target = command_obj
                    .get("target")
                    .ok_or_else(|| "breakPinLinks requires target.".to_owned())
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let mut op = serde_json::Map::new();
                op.insert("op".into(), serde_json::json!("breakPinLinks"));
                op.insert("target".into(), target);
                Ok(vec![serde_json::Value::Object(op)])
            }
            other => Err(format!(
                "Unsupported material.graph.edit command kind: {other}"
            )),
        }
        .map_err(invalid_argument_result)?;
        ops.extend(compiled);
    }
    Ok(ops)
}

fn translate_material_graph_edit_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, "material.graph.edit")?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| {
            invalid_argument_result("material.graph.edit requires assetPath or graph.")
        })?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    for field in [
        "expectedRevision",
        "idempotencyKey",
        "dryRun",
        "continueOnError",
    ] {
        copy_if_present(args, &mut translated, field);
    }
    let commands = args
        .get("commands")
        .and_then(|value| value.as_array())
        .ok_or_else(|| invalid_argument_result("material.graph.edit requires commands."))?;
    translated.insert(
        "ops".into(),
        serde_json::Value::Array(compile_material_graph_commands(commands)?),
    );
    Ok(translated)
}

fn translate_material_node_edit_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, "material.node.edit")?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| {
            invalid_argument_result("material.node.edit requires assetPath or graph.")
        })?;

    let node = args
        .get("node")
        .ok_or_else(|| invalid_argument_result("material.node.edit requires node."))?;
    let property = args
        .get("property")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| invalid_argument_result("material.node.edit requires property."))?;
    let value = args
        .get("value")
        .ok_or_else(|| invalid_argument_result("material.node.edit requires value."))?;

    let mut op = match extract_node_token(node).map_err(invalid_argument_result)? {
        serde_json::Value::Object(object) => object,
        _ => serde_json::Map::new(),
    };
    op.insert("op".into(), serde_json::json!("setProperty"));
    op.insert("property".into(), serde_json::json!(property));
    op.insert("value".into(), value.clone());

    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    translated.insert(
        "ops".into(),
        serde_json::json!([serde_json::Value::Object(op)]),
    );
    for field in ["expectedRevision", "idempotencyKey", "dryRun"] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
}

fn translate_selection_graph_layout_args(
    args: &rmcp::model::JsonObject,
    tool_name: &str,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, tool_name)?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| {
            invalid_argument_result(format!("{tool_name} requires assetPath or graph."))
        })?;

    if let Some(operation) = args.get("operation").and_then(|value| value.as_str()) {
        if operation != "format" {
            return Err(invalid_argument_result(format!(
                "Unsupported {tool_name} operation: {operation}. Supported operations: format."
            )));
        }
    }

    let scope = args
        .get("scope")
        .and_then(|value| value.as_object())
        .ok_or_else(|| invalid_argument_result(format!("{tool_name} requires scope.")))?;
    let mode = scope
        .get("mode")
        .and_then(|value| value.as_str())
        .ok_or_else(|| invalid_argument_result(format!("{tool_name} scope requires mode.")))?;
    if mode != "selection" {
        return Err(invalid_argument_result(format!(
            "Unsupported {tool_name} scope.mode: {mode}. Supported modes: selection."
        )));
    }

    let nodes = scope
        .get("nodes")
        .and_then(|value| value.as_array())
        .ok_or_else(|| invalid_argument_result("scope.nodes is required for selection layout."))?;
    if nodes.is_empty() {
        return Err(invalid_argument_result(
            "scope.nodes must contain at least one node.",
        ));
    }

    let mut node_ids = Vec::new();
    for (index, node) in nodes.iter().enumerate() {
        node_ids.push(
            parse_node_ref_id(node, &format!("scope.nodes[{index}]"))
                .map_err(invalid_argument_result)?,
        );
    }

    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    translated.insert(
        "ops".into(),
        serde_json::json!([{
            "op": "layoutGraph",
            "scope": "selection",
            "nodeIds": node_ids
        }]),
    );
    for field in ["expectedRevision", "dryRun"] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
}

fn translate_material_graph_layout_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    translate_selection_graph_layout_args(args, "material.graph.layout")
}

fn translate_pcg_graph_inspect_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, "pcg.graph.inspect")?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| invalid_argument_result("pcg.graph.inspect requires assetPath or graph."))?;

    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    if let Some(filter) = args.get("filter").and_then(|value| value.as_object()) {
        if let Some(node_ids) = filter.get("nodeIds") {
            translated.insert("nodeIds".into(), node_ids.clone());
        }
    }

    let view = pcg_graph_inspect_view(args);
    if matches!(view, "links" | "defaults" | "full") {
        translated.insert("includeConnections".into(), serde_json::json!(true));
    }
    Ok(translated)
}

fn translate_pcg_node_inspect_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let mut translated = rmcp::model::JsonObject::new();

    if let Some(node_class) = args
        .get("nodeClass")
        .or_else(|| args.get("settingsClass"))
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
    {
        translated.insert("nodeClass".into(), serde_json::json!(node_class));
        return Ok(translated);
    }

    let graph_asset_path = extract_query_graph_asset_path(args, "pcg.node.inspect")?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| {
            invalid_argument_result("pcg.node.inspect requires assetPath or graph for node mode.")
        })?;
    translated.insert("assetPath".into(), serde_json::json!(asset_path));

    let node_id = args
        .get("node")
        .and_then(|value| extract_node_token(value).ok())
        .and_then(|value| {
            value
                .get("nodeId")
                .and_then(|node_id| node_id.as_str())
                .map(str::to_owned)
        })
        .or_else(|| {
            args.get("nodeId")
                .and_then(|value| value.as_str())
                .filter(|value| !value.is_empty())
                .map(str::to_owned)
        })
        .ok_or_else(|| invalid_argument_result("pcg.node.inspect requires node.id."))?;
    translated.insert("nodeId".into(), serde_json::json!(node_id));
    Ok(translated)
}

fn translate_pcg_compile_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, "pcg.compile")?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| invalid_argument_result("pcg.compile requires assetPath or graph."))?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    Ok(translated)
}

fn translate_asset_or_graph_args(
    args: &rmcp::model::JsonObject,
    tool_name: &str,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, tool_name)?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| {
            invalid_argument_result(&format!("{tool_name} requires assetPath or graph."))
        })?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    Ok(translated)
}

fn translate_pcg_parameter_edit_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let mut translated = translate_asset_or_graph_args(args, "pcg.parameter.edit")?;
    let operation = args
        .get("operation")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| invalid_argument_result("pcg.parameter.edit requires operation."))?;
    let operation_args = args
        .get("args")
        .and_then(|value| value.as_object())
        .ok_or_else(|| invalid_argument_result("pcg.parameter.edit requires args."))?;

    translated.insert("operation".into(), serde_json::json!(operation));
    translated.insert(
        "args".into(),
        serde_json::Value::Object(operation_args.clone()),
    );
    copy_mutation_controls(args, &mut translated);
    Ok(translated)
}

fn shape_pcg_compile_result(payload: serde_json::Value) -> serde_json::Value {
    let Some(object) = payload.as_object() else {
        return payload;
    };
    let status = object
        .get("status")
        .and_then(|value| value.as_str())
        .unwrap_or("error");
    let compile_report = object
        .get("compileReport")
        .cloned()
        .unwrap_or_else(|| serde_json::json!({}));
    let compiled = compile_report
        .get("compiled")
        .and_then(|value| value.as_bool())
        .unwrap_or(status == "ok");
    serde_json::json!({
        "assetPath": object.get("assetPath").cloned().unwrap_or(serde_json::Value::Null),
        "status": status,
        "valid": status == "ok" && compiled,
        "compiled": compiled,
        "summary": object.get("summary").cloned().unwrap_or(serde_json::Value::Null),
        "diagnostics": object.get("diagnostics").cloned().unwrap_or(serde_json::json!([])),
        "compileReport": compile_report,
        "queryReport": object.get("queryReport").cloned().unwrap_or(serde_json::Value::Null)
    })
}

fn shape_pcg_node_inspect_result(payload: serde_json::Value) -> serde_json::Value {
    let Some(object) = payload.as_object() else {
        return payload;
    };
    let mode = object
        .get("mode")
        .and_then(|value| value.as_str())
        .unwrap_or("");
    if mode == "instance" {
        let node = object
            .get("node")
            .cloned()
            .unwrap_or(serde_json::Value::Null);
        let settings = node
            .get("settings")
            .cloned()
            .or_else(|| node.get("effectiveSettings").cloned())
            .unwrap_or(serde_json::Value::Null);
        let properties = settings
            .get("properties")
            .cloned()
            .unwrap_or(serde_json::json!([]));
        return serde_json::json!({
            "mode": "instance",
            "assetPath": object.get("assetPath").cloned().unwrap_or(serde_json::Value::Null),
            "nodeId": object.get("nodeId").cloned().unwrap_or(serde_json::Value::Null),
            "node": node,
            "settings": settings,
            "properties": properties,
            "pins": object
                .get("node")
                .and_then(|node| node.get("pins"))
                .cloned()
                .unwrap_or(serde_json::json!([]))
        });
    }

    if mode == "class" {
        return serde_json::json!({
            "mode": "class",
            "nodeClass": object.get("nodeClass").cloned().unwrap_or(serde_json::Value::Null),
            "title": object.get("title").cloned().unwrap_or(serde_json::Value::Null),
            "tooltip": object.get("tooltip").cloned().unwrap_or(serde_json::Value::Null),
            "settingsType": object.get("settingsType").cloned().unwrap_or(serde_json::Value::Null),
            "inputPins": object.get("inputPins").cloned().unwrap_or(serde_json::json!([])),
            "outputPins": object.get("outputPins").cloned().unwrap_or(serde_json::json!([])),
            "properties": object.get("properties").cloned().unwrap_or(serde_json::json!([]))
        });
    }
    payload
}

fn validate_pcg_graph_inspect_args(args: &rmcp::model::JsonObject) -> Result<(), CallToolResult> {
    for key in args.keys() {
        if !matches!(
            key.as_str(),
            "assetPath" | "graph" | "view" | "filter" | "page"
        ) {
            return Err(invalid_argument_result(format!(
                "pcg.graph.inspect does not support top-level {key}; use assetPath, graph, view, filter, and page."
            )));
        }
    }
    for field in [
        "graphName",
        "graphRef",
        "nodeIds",
        "nodeClasses",
        "includeConnections",
        "limit",
        "cursor",
    ] {
        if args.contains_key(field) {
            return Err(invalid_argument_result(format!(
                "pcg.graph.inspect no longer accepts top-level {field}; use graph, view, filter, and page."
            )));
        }
    }

    let view = pcg_graph_inspect_view(args);
    if !matches!(view, "overview" | "pins" | "links" | "defaults" | "full") {
        return Err(invalid_argument_result(format!(
            "Unsupported pcg.graph.inspect view: {view}."
        )));
    }
    if args.contains_key("filter") && !args.get("filter").is_some_and(|value| value.is_object()) {
        return Err(invalid_argument_result(
            "pcg.graph.inspect filter must be an object.",
        ));
    }
    if let Some(filter) = args.get("filter").and_then(|value| value.as_object()) {
        for key in filter.keys() {
            if !matches!(key.as_str(), "nodeIds" | "text") {
                return Err(invalid_argument_result(format!(
                    "pcg.graph.inspect filter does not support {key}."
                )));
            }
        }
    }
    if args.contains_key("page") && !args.get("page").is_some_and(|value| value.is_object()) {
        return Err(invalid_argument_result(
            "pcg.graph.inspect page must be an object.",
        ));
    }
    if let Some(page) = args.get("page").and_then(|value| value.as_object()) {
        for key in page.keys() {
            if !matches!(key.as_str(), "limit" | "cursor") {
                return Err(invalid_argument_result(format!(
                    "pcg.graph.inspect page does not support {key}."
                )));
            }
        }
    }
    Ok(())
}

fn pcg_graph_inspect_view(args: &rmcp::model::JsonObject) -> &str {
    args.get("view")
        .and_then(|value| value.as_str())
        .unwrap_or("overview")
}

fn translate_pcg_palette_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, "pcg_palette")?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| invalid_argument_result("pcg_palette requires assetPath or graph."))?;

    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    for field in ["query", "elementTypes", "limit", "offset"] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
}

fn compile_pcg_add_from_palette_command(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Result<Vec<serde_json::Value>, String> {
    let entry = command
        .get("entry")
        .and_then(|value| value.as_object())
        .ok_or_else(|| "addFromPalette requires entry from pcg_palette.".to_owned())?;
    let entry_id = entry
        .get("id")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "addFromPalette requires entry.id.".to_owned())?;
    if entry.get("executable").and_then(|value| value.as_bool()) == Some(false) {
        return Err(format!("pcg_palette entry is not executable: {entry_id}"));
    }

    let mut op = serde_json::Map::new();
    op.insert("op".into(), serde_json::json!("addFromPalette"));
    op.insert("entryId".into(), serde_json::json!(entry_id));
    op.insert("entry".into(), serde_json::Value::Object(entry.clone()));
    if let Some(position) = command.get("position").and_then(|value| value.as_object()) {
        if let Some(x) = position.get("x") {
            op.insert("x".into(), x.clone());
        }
        if let Some(y) = position.get("y") {
            op.insert("y".into(), y.clone());
        }
    }
    for field in ["anchor", "near", "from", "target", "behavior"] {
        copy_if_present(command, &mut op, field);
    }
    if let Some(alias) = command.get("alias").and_then(|value| value.as_str()) {
        op.insert("clientRef".into(), serde_json::json!(alias));
    }
    Ok(vec![serde_json::Value::Object(op)])
}

fn compile_pcg_graph_commands(
    commands: &[serde_json::Value],
) -> Result<Vec<serde_json::Value>, CallToolResult> {
    let mut ops = Vec::new();
    for (index, command) in commands.iter().enumerate() {
        let Some(command_obj) = command.as_object() else {
            return Err(invalid_argument_result(format!(
                "pcg.graph.edit command at index {index} must be an object."
            )));
        };
        let Some(kind) = command_obj.get("kind").and_then(|value| value.as_str()) else {
            return Err(invalid_argument_result(format!(
                "pcg.graph.edit command at index {index} requires kind."
            )));
        };

        let compiled = match kind {
            "addFromPalette" => compile_pcg_add_from_palette_command(command_obj),
            "removeNode" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "removeNode requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let mut op = node.as_object().cloned().unwrap_or_default();
                op.insert("op".into(), serde_json::json!("removeNode"));
                Ok(vec![serde_json::Value::Object(op)])
            }
            "moveNode" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "moveNode requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let mut op = node.as_object().cloned().unwrap_or_default();
                let op_name = if let Some(position) = command_obj
                    .get("position")
                    .and_then(|value| value.as_object())
                {
                    if let Some(x) = position.get("x") {
                        op.insert("x".into(), x.clone());
                    }
                    if let Some(y) = position.get("y") {
                        op.insert("y".into(), y.clone());
                    }
                    "moveNode"
                } else if let Some(delta) =
                    command_obj.get("delta").and_then(|value| value.as_object())
                {
                    if let Some(dx) = delta.get("x") {
                        op.insert("dx".into(), dx.clone());
                    }
                    if let Some(dy) = delta.get("y") {
                        op.insert("dy".into(), dy.clone());
                    }
                    "moveNodeBy"
                } else {
                    return Err(invalid_argument_result(
                        "moveNode requires position or delta.",
                    ));
                };
                op.insert("op".into(), serde_json::json!(op_name));
                Ok(vec![serde_json::Value::Object(op)])
            }
            "connect" | "disconnect" => {
                if command_obj
                    .get("conversionPolicy")
                    .and_then(|value| value.as_str())
                    .is_some_and(|value| value != "strict")
                {
                    return Err(invalid_argument_result(
                        "pcg.graph.edit connect currently supports conversionPolicy='strict' only.",
                    ));
                }
                let from = command_obj
                    .get("from")
                    .ok_or_else(|| format!("{kind} requires from."))
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let to = command_obj
                    .get("to")
                    .ok_or_else(|| format!("{kind} requires to."))
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                Ok(vec![serde_json::json!({
                    "op": if kind == "connect" { "connectPins" } else { "disconnectPins" },
                    "from": from,
                    "to": to
                })])
            }
            "setPinDefault" => {
                let target = command_obj
                    .get("target")
                    .ok_or_else(|| "setPinDefault requires target.".to_owned())
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let value = command_obj
                    .get("value")
                    .ok_or_else(|| invalid_argument_result("setPinDefault requires value."))?;
                Ok(vec![serde_json::json!({
                    "op": "setPinDefault",
                    "target": target,
                    "value": json_string_value(value)
                })])
            }
            "setNodeProperty" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "setNodeProperty requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let property = command_obj
                    .get("property")
                    .and_then(|value| value.as_str())
                    .ok_or_else(|| invalid_argument_result("setNodeProperty requires property."))?;
                let value = command_obj
                    .get("value")
                    .ok_or_else(|| invalid_argument_result("setNodeProperty requires value."))?;
                let mut op = node.as_object().cloned().unwrap_or_default();
                op.insert("op".into(), serde_json::json!("setProperty"));
                op.insert("property".into(), serde_json::json!(property));
                op.insert("value".into(), serde_json::json!(json_string_value(value)));
                Ok(vec![serde_json::Value::Object(op)])
            }
            other => Err(format!("Unsupported pcg.graph.edit command kind: {other}")),
        }
        .map_err(invalid_argument_result)?;
        ops.extend(compiled);
    }
    Ok(ops)
}

fn translate_pcg_graph_edit_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let graph_asset_path = extract_query_graph_asset_path(args, "pcg.graph.edit")?;
    let direct_asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty());
    let asset_path = graph_asset_path
        .as_deref()
        .or(direct_asset_path)
        .ok_or_else(|| invalid_argument_result("pcg.graph.edit requires assetPath or graph."))?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    for field in [
        "expectedRevision",
        "idempotencyKey",
        "dryRun",
        "continueOnError",
    ] {
        copy_if_present(args, &mut translated, field);
    }
    let commands = args
        .get("commands")
        .and_then(|value| value.as_array())
        .ok_or_else(|| invalid_argument_result("pcg.graph.edit requires commands."))?;
    translated.insert(
        "ops".into(),
        serde_json::Value::Array(compile_pcg_graph_commands(commands)?),
    );
    Ok(translated)
}

fn translate_pcg_graph_layout_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    translate_selection_graph_layout_args(args, "pcg.graph.layout")
}

fn widget_parent_name_from_command(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Option<String> {
    if let Some(parent_name) = command.get("parentName").and_then(|value| value.as_str()) {
        return Some(parent_name.to_owned());
    }
    match command.get("parent") {
        Some(serde_json::Value::String(parent)) => Some(parent.clone()),
        Some(serde_json::Value::Object(parent)) => parent
            .get("name")
            .and_then(|value| value.as_str())
            .map(str::to_owned),
        _ => None,
    }
}

fn widget_target_name_from_command(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Option<String> {
    if let Some(name) = command.get("name").and_then(|value| value.as_str()) {
        return Some(name.to_owned());
    }
    command
        .get("target")
        .and_then(|value| value.as_object())
        .and_then(|target| target.get("name"))
        .and_then(|value| value.as_str())
        .map(str::to_owned)
}

fn compile_widget_tree_command(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Result<serde_json::Value, String> {
    let kind = command
        .get("kind")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "widget.tree.edit command requires kind.".to_owned())?;

    let mut op = serde_json::Map::new();
    let mut op_args = serde_json::Map::new();
    match kind {
        "addFromPalette" => {
            let entry = command
                .get("entry")
                .and_then(|value| value.as_object())
                .ok_or_else(|| "addFromPalette requires entry from widget_palette.".to_owned())?;
            let entry_id = entry
                .get("id")
                .and_then(|value| value.as_str())
                .ok_or_else(|| "addFromPalette requires entry.id.".to_owned())?;
            if entry.get("executable").and_then(|value| value.as_bool()) == Some(false) {
                return Err(format!(
                    "widget_palette entry is not executable: {entry_id}"
                ));
            }
            let payload = entry
                .get("payload")
                .and_then(|value| value.as_object())
                .ok_or_else(|| "addFromPalette requires entry.payload.".to_owned())?;
            let widget_class = payload
                .get("widgetClass")
                .and_then(|value| value.as_str())
                .ok_or_else(|| "widget_palette entry.payload requires widgetClass.".to_owned())?;
            let name = command
                .get("name")
                .and_then(|value| value.as_str())
                .ok_or_else(|| "addFromPalette requires name.".to_owned())?;

            op.insert("op".into(), serde_json::json!("addWidget"));
            op_args.insert("widgetClass".into(), serde_json::json!(widget_class));
            op_args.insert("name".into(), serde_json::json!(name));
            if let Some(parent_name) = widget_parent_name_from_command(command) {
                op_args.insert("parentName".into(), serde_json::json!(parent_name));
            }
            if let Some(slot) = command.get("slot").and_then(|value| value.as_object()) {
                op_args.insert("slot".into(), serde_json::Value::Object(slot.clone()));
            }
            if let Some(is_variable) = command.get("isVariable").and_then(|value| value.as_bool()) {
                op_args.insert("isVariable".into(), serde_json::json!(is_variable));
            }
        }
        "removeWidget" => {
            let name = widget_target_name_from_command(command)
                .ok_or_else(|| "removeWidget requires name or target.name.".to_owned())?;
            op.insert("op".into(), serde_json::json!("removeWidget"));
            op_args.insert("name".into(), serde_json::json!(name));
        }
        "renameWidget" => {
            let old_name = command
                .get("target")
                .and_then(|value| value.as_object())
                .and_then(|target| target.get("name"))
                .and_then(|value| value.as_str())
                .or_else(|| command.get("oldName").and_then(|value| value.as_str()))
                .ok_or_else(|| "renameWidget requires target.name or oldName.".to_owned())?;
            let new_name = command
                .get("newName")
                .or_else(|| command.get("name"))
                .or_else(|| command.get("to"))
                .and_then(|value| value.as_str())
                .ok_or_else(|| "renameWidget requires name or newName.".to_owned())?;
            op.insert("op".into(), serde_json::json!("renameWidget"));
            op_args.insert("name".into(), serde_json::json!(old_name));
            op_args.insert("newName".into(), serde_json::json!(new_name));
        }
        "reparentWidget" => {
            let name = widget_target_name_from_command(command)
                .ok_or_else(|| "reparentWidget requires name or target.name.".to_owned())?;
            let new_parent = match command.get("newParent") {
                Some(serde_json::Value::String(parent)) => Some(parent.as_str()),
                Some(serde_json::Value::Object(parent)) => {
                    parent.get("name").and_then(|value| value.as_str())
                }
                _ => None,
            }
            .ok_or_else(|| "reparentWidget requires newParent or newParent.name.".to_owned())?;
            op.insert("op".into(), serde_json::json!("reparentWidget"));
            op_args.insert("name".into(), serde_json::json!(name));
            op_args.insert("newParent".into(), serde_json::json!(new_parent));
            if let Some(slot) = command.get("slot").and_then(|value| value.as_object()) {
                op_args.insert("slot".into(), serde_json::Value::Object(slot.clone()));
            }
        }
        "setIsVariable" => {
            let name = widget_target_name_from_command(command)
                .ok_or_else(|| "setIsVariable requires name or target.name.".to_owned())?;
            let value = command
                .get("value")
                .and_then(|value| value.as_bool())
                .ok_or_else(|| "setIsVariable requires boolean value.".to_owned())?;
            op.insert("op".into(), serde_json::json!("setIsVariable"));
            op_args.insert("name".into(), serde_json::json!(name));
            op_args.insert("value".into(), serde_json::json!(value));
        }
        other => {
            return Err(format!(
                "Unsupported widget.tree.edit command kind: {other}."
            ));
        }
    }

    op.insert("args".into(), serde_json::Value::Object(op_args));
    Ok(serde_json::Value::Object(op))
}

fn translate_widget_tree_edit_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .ok_or_else(|| invalid_argument_result("widget.tree.edit requires assetPath."))?;
    let commands = args
        .get("commands")
        .and_then(|value| value.as_array())
        .ok_or_else(|| invalid_argument_result("widget.tree.edit requires commands."))?;
    if commands.is_empty() {
        return Err(invalid_argument_result(
            "widget.tree.edit commands must be non-empty.",
        ));
    }

    let mut ops = Vec::new();
    for command in commands {
        let Some(command_obj) = command.as_object() else {
            return Err(invalid_argument_result(
                "widget.tree.edit commands entries must be objects.",
            ));
        };
        match compile_widget_tree_command(command_obj) {
            Ok(op) => ops.push(op),
            Err(message) => return Err(invalid_argument_result(message)),
        }
    }

    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    translated.insert("ops".into(), serde_json::Value::Array(ops));
    copy_dry_run_revision_controls(args, &mut translated);
    Ok(translated)
}

fn compile_widget_edit_command(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Result<serde_json::Value, String> {
    let kind = command
        .get("kind")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "widget.edit command requires kind.".to_owned())?;
    if kind != "setProperty" && kind != "setSlotProperty" {
        return Err(format!("Unsupported widget.edit command kind: {kind}."));
    }
    let name = command
        .get("widget")
        .and_then(|value| value.as_object())
        .and_then(|widget| widget.get("name"))
        .and_then(|value| value.as_str())
        .or_else(|| command.get("name").and_then(|value| value.as_str()))
        .ok_or_else(|| format!("{kind} requires widget.name."))?;
    let property = command
        .get("property")
        .and_then(|value| value.as_str())
        .ok_or_else(|| format!("{kind} requires property."))?;
    let value = command
        .get("value")
        .and_then(|value| value.as_str())
        .ok_or_else(|| format!("{kind} requires string value."))?;

    Ok(serde_json::json!({
        "op": kind,
        "args": {
            "name": name,
            "property": property,
            "value": value
        }
    }))
}

fn translate_widget_edit_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = args
        .get("assetPath")
        .and_then(|value| value.as_str())
        .ok_or_else(|| invalid_argument_result("widget.edit requires assetPath."))?;
    let commands = args
        .get("commands")
        .and_then(|value| value.as_array())
        .ok_or_else(|| invalid_argument_result("widget.edit requires commands."))?;
    if commands.is_empty() {
        return Err(invalid_argument_result(
            "widget.edit commands must be non-empty.",
        ));
    }

    let mut ops = Vec::new();
    for command in commands {
        let Some(command_obj) = command.as_object() else {
            return Err(invalid_argument_result(
                "widget.edit commands entries must be objects.",
            ));
        };
        match compile_widget_edit_command(command_obj) {
            Ok(op) => ops.push(op),
            Err(message) => return Err(invalid_argument_result(message)),
        }
    }

    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    translated.insert("ops".into(), serde_json::Value::Array(ops));
    copy_dry_run_revision_controls(args, &mut translated);
    Ok(translated)
}

fn translate_widget_tree_inspect_args(args: &rmcp::model::JsonObject) -> rmcp::model::JsonObject {
    let mut translated = rmcp::model::JsonObject::new();
    copy_if_present(args, &mut translated, "assetPath");
    let include_slot_properties = args
        .get("view")
        .and_then(|value| value.as_str())
        .is_some_and(|view| view == "layout");
    translated.insert(
        "includeSlotProperties".into(),
        serde_json::json!(include_slot_properties),
    );
    translated
}

fn widget_tree_inspect_view(args: &rmcp::model::JsonObject) -> &str {
    args.get("view")
        .and_then(|value| value.as_str())
        .unwrap_or("outline")
}

fn widget_tree_filter_names(args: &rmcp::model::JsonObject) -> std::collections::HashSet<String> {
    args.get("filter")
        .and_then(|value| value.as_object())
        .and_then(|filter| filter.get("names"))
        .and_then(|value| value.as_array())
        .map(|names| {
            names
                .iter()
                .filter_map(|value| value.as_str())
                .map(str::to_owned)
                .collect()
        })
        .unwrap_or_default()
}

fn widget_tree_filter_text(args: &rmcp::model::JsonObject) -> Option<String> {
    args.get("filter")
        .and_then(|value| value.as_object())
        .and_then(|filter| filter.get("text"))
        .and_then(|value| value.as_str())
        .map(|text| text.to_lowercase())
}

fn widget_tree_node_matches(
    node: &serde_json::Map<String, serde_json::Value>,
    names: &std::collections::HashSet<String>,
    text: Option<&str>,
) -> bool {
    let name_matches = names.is_empty()
        || node
            .get("name")
            .and_then(|value| value.as_str())
            .is_some_and(|name| names.contains(name));
    let text_matches = text.is_none_or(|query| {
        serde_json::to_string(node)
            .unwrap_or_default()
            .to_lowercase()
            .contains(query)
    });
    name_matches && text_matches
}

fn prune_widget_tree_outline(node: &mut serde_json::Value) {
    let Some(object) = node.as_object_mut() else {
        return;
    };
    object.remove("slot");
    object.remove("slotClass");
    if let Some(children) = object
        .get_mut("children")
        .and_then(|value| value.as_array_mut())
    {
        for child in children {
            prune_widget_tree_outline(child);
        }
    }
}

fn collect_widget_tree_matches(
    node: &serde_json::Value,
    names: &std::collections::HashSet<String>,
    text: Option<&str>,
    out: &mut Vec<serde_json::Value>,
) {
    let Some(object) = node.as_object() else {
        return;
    };
    if widget_tree_node_matches(object, names, text) {
        out.push(node.clone());
    }
    if let Some(children) = object.get("children").and_then(|value| value.as_array()) {
        for child in children {
            collect_widget_tree_matches(child, names, text, out);
        }
    }
}

fn shape_widget_tree_inspect_payload(
    mut payload: serde_json::Value,
    args: &rmcp::model::JsonObject,
) -> serde_json::Value {
    let view = widget_tree_inspect_view(args);
    let names = widget_tree_filter_names(args);
    let text = widget_tree_filter_text(args);
    let has_filter = !names.is_empty() || text.is_some();

    if let Some(object) = payload.as_object_mut() {
        object.insert("view".into(), serde_json::json!(view));
        if view == "outline" {
            if let Some(root_widget) = object.get_mut("rootWidget") {
                prune_widget_tree_outline(root_widget);
            }
        }
        if has_filter {
            let mut matches = Vec::new();
            if let Some(root_widget) = object.get("rootWidget") {
                collect_widget_tree_matches(root_widget, &names, text.as_deref(), &mut matches);
            }
            object.insert("matches".into(), serde_json::Value::Array(matches));
        }
    }
    payload
}

fn shape_widget_tree_inspect_result(
    mut result: CallToolResult,
    args: &rmcp::model::JsonObject,
) -> CallToolResult {
    if let Some(payload) = result.structured_content.take() {
        result.structured_content = Some(shape_widget_tree_inspect_payload(payload, args));
    }
    result
}

fn translate_widget_inspect_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let mut translated = rmcp::model::JsonObject::new();
    copy_if_present(args, &mut translated, "widgetClass");
    copy_if_present(args, &mut translated, "assetPath");
    if let Some(widget) = args.get("widget").and_then(|value| value.as_object()) {
        if let Some(name) = widget.get("name").and_then(|value| value.as_str()) {
            translated.insert("widgetName".into(), serde_json::json!(name));
        }
    }
    copy_if_present(args, &mut translated, "widgetName");

    let has_class = translated.get("widgetClass").is_some();
    let has_instance =
        translated.get("assetPath").is_some() && translated.get("widgetName").is_some();
    if !has_class && !has_instance {
        return Err(invalid_argument_result(
            "widget_inspect requires widgetClass, or assetPath plus widget.name.",
        ));
    }
    Ok(translated)
}

fn translate_blueprint_graph_list_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint_graph_list")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    copy_if_present(args, &mut translated, "includeCompositeSubgraphs");
    Ok(translated)
}

fn translate_blueprint_graph_inspect_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint_graph_inspect")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    let graph_address =
        read_required_graph_object_address(args, &asset_path, "blueprint_graph_inspect")?;
    write_optional_graph_address(&mut translated, Some(graph_address));

    translated.insert("includeConnections".into(), serde_json::json!(true));
    translated.insert("limit".into(), serde_json::json!(10000));
    Ok(translated)
}

fn translate_blueprint_node_inspect_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint_node_inspect")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    let graph_address =
        read_required_graph_object_address(args, &asset_path, "blueprint_node_inspect")?;
    write_optional_graph_address(&mut translated, Some(graph_address));

    let node = args
        .get("node")
        .and_then(|value| value.as_object())
        .ok_or_else(|| invalid_argument_result("blueprint_node_inspect requires node."))?;
    let node_id = node
        .get("id")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| invalid_argument_result("blueprint_node_inspect requires node.id."))?;
    translated.insert("nodeId".into(), serde_json::json!(node_id));
    Ok(translated)
}

fn translate_blueprint_node_edit_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint.node.edit")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    let graph_address =
        read_required_graph_object_address(args, &asset_path, "blueprint.node.edit")?;
    write_optional_graph_address(&mut translated, Some(graph_address));

    let node = args
        .get("node")
        .and_then(|value| value.as_object())
        .ok_or_else(|| invalid_argument_result("blueprint.node.edit requires node."))?;
    let node_id = node
        .get("id")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| invalid_argument_result("blueprint.node.edit requires node.id."))?;
    translated.insert("nodeId".into(), serde_json::json!(node_id));

    let operation = args
        .get("operation")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| invalid_argument_result("blueprint.node.edit requires operation."))?;
    translated.insert("operation".into(), serde_json::json!(operation));
    translated.insert(
        "args".into(),
        args.get("args")
            .cloned()
            .unwrap_or_else(|| serde_json::json!({})),
    );
    for field in ["expectedRevision", "dryRun"] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
}

fn read_required_graph_object_address(
    args: &rmcp::model::JsonObject,
    asset_path: &str,
    tool_name: &str,
) -> Result<BlueprintGraphAddress, CallToolResult> {
    let Some(graph_obj) = args.get("graph").and_then(|value| value.as_object()) else {
        return Err(invalid_argument_result(format!(
            "{tool_name} requires graph."
        )));
    };
    if let Some(graph_id) = graph_obj
        .get("id")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
    {
        let mut graph_ref = serde_json::Map::new();
        graph_ref.insert("kind".into(), serde_json::json!("asset"));
        graph_ref.insert("assetPath".into(), serde_json::json!(asset_path));
        graph_ref.insert("graphId".into(), serde_json::json!(graph_id));
        return Ok(BlueprintGraphAddress::Ref(graph_ref));
    }
    if let Some(graph_name) = graph_obj
        .get("name")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
    {
        return Ok(BlueprintGraphAddress::Name(graph_name.to_owned()));
    }
    Err(invalid_argument_result(format!(
        "{tool_name} graph requires id or name."
    )))
}

fn validate_blueprint_graph_inspect_args(
    args: &rmcp::model::JsonObject,
) -> Result<(), CallToolResult> {
    for key in args.keys() {
        if !matches!(
            key.as_str(),
            "assetPath" | "graph" | "view" | "rootNode" | "rootPin" | "traversal"
        ) {
            return Err(invalid_argument_result(format!(
                "blueprint_graph_inspect does not support top-level {key}; use assetPath, graph, view, rootNode, rootPin, and traversal."
            )));
        }
    }
    for field in [
        "graphName",
        "graphRef",
        "nodeIds",
        "nodeClasses",
        "filter",
        "page",
        "limit",
        "cursor",
        "detail",
        "includePinDefaults",
        "layoutDetail",
    ] {
        if args.contains_key(field) {
            return Err(invalid_argument_result(format!(
                "blueprint_graph_inspect no longer accepts top-level {field}; use graph, view, rootNode, rootPin, and traversal."
            )));
        }
    }

    let view = blueprint_graph_inspect_view(args);
    if !matches!(view, "summary" | "exec_flow" | "data_flow") {
        return Err(invalid_argument_result(format!(
            "Unsupported blueprint_graph_inspect view: {view}."
        )));
    }

    match view {
        "summary" => {
            if args.contains_key("rootNode") || args.contains_key("rootPin") {
                return Err(invalid_argument_result(format!(
                    "blueprint_graph_inspect view=summary does not accept rootNode or rootPin."
                )));
            }
        }
        "exec_flow" => {
            let Some(root_node) = args.get("rootNode").and_then(|value| value.as_object()) else {
                return Err(invalid_argument_result(
                    "blueprint_graph_inspect view=exec_flow requires rootNode.id.",
                ));
            };
            if root_node
                .get("id")
                .and_then(|value| value.as_str())
                .filter(|value| !value.is_empty())
                .is_none()
            {
                return Err(invalid_argument_result(
                    "blueprint_graph_inspect view=exec_flow requires rootNode.id.",
                ));
            }
            if args.contains_key("rootPin") {
                return Err(invalid_argument_result(format!(
                    "blueprint_graph_inspect view=exec_flow does not accept rootPin."
                )));
            }
        }
        "data_flow" => {
            let Some(root_pin) = args.get("rootPin").and_then(|value| value.as_object()) else {
                return Err(invalid_argument_result(
                    "blueprint_graph_inspect view=data_flow requires rootPin.node.id and rootPin.pin.",
                ));
            };
            let node_ok = root_pin
                .get("node")
                .and_then(|value| value.as_object())
                .and_then(|node| node.get("id"))
                .and_then(|value| value.as_str())
                .filter(|value| !value.is_empty())
                .is_some();
            let pin_ok = root_pin
                .get("pin")
                .and_then(|value| value.as_str())
                .filter(|value| !value.is_empty())
                .is_some();
            if !node_ok || !pin_ok {
                return Err(invalid_argument_result(
                    "blueprint_graph_inspect view=data_flow requires rootPin.node.id and rootPin.pin.",
                ));
            }
            if args.contains_key("rootNode") {
                return Err(invalid_argument_result(format!(
                    "blueprint_graph_inspect view=data_flow does not accept rootNode."
                )));
            }
        }
        _ => {}
    }

    if args.contains_key("traversal")
        && !args.get("traversal").is_some_and(|value| value.is_object())
    {
        return Err(invalid_argument_result(
            "blueprint_graph_inspect traversal must be an object.",
        ));
    }
    if let Some(traversal) = args.get("traversal").and_then(|value| value.as_object()) {
        for key in traversal.keys() {
            if !matches!(key.as_str(), "direction" | "maxDepth" | "maxNodes") {
                return Err(invalid_argument_result(format!(
                    "blueprint_graph_inspect traversal does not support {key}."
                )));
            }
        }
        if let Some(direction) = traversal.get("direction").and_then(|value| value.as_str()) {
            if !matches!(direction, "upstream" | "downstream" | "both") {
                return Err(invalid_argument_result(format!(
                    "Unsupported blueprint_graph_inspect traversal.direction: {direction}."
                )));
            }
        }
        validate_blueprint_graph_traversal_bound(traversal, "maxDepth", 1, 128)?;
        validate_blueprint_graph_traversal_bound(traversal, "maxNodes", 1, 1000)?;
    }
    Ok(())
}

fn blueprint_graph_inspect_view(args: &rmcp::model::JsonObject) -> &str {
    args.get("view")
        .and_then(|value| value.as_str())
        .unwrap_or("summary")
}

fn validate_blueprint_graph_traversal_bound(
    traversal: &serde_json::Map<String, serde_json::Value>,
    field: &str,
    min: i64,
    max: i64,
) -> Result<(), CallToolResult> {
    let Some(value) = traversal.get(field) else {
        return Ok(());
    };
    let Some(number) = value.as_i64() else {
        return Err(invalid_argument_result(format!(
            "blueprint_graph_inspect traversal.{field} must be an integer."
        )));
    };
    if number < min || number > max {
        return Err(invalid_argument_result(format!(
            "blueprint_graph_inspect traversal.{field} must be between {min} and {max}."
        )));
    }
    Ok(())
}

fn copy_json_field(
    source: &serde_json::Map<String, serde_json::Value>,
    target: &mut serde_json::Map<String, serde_json::Value>,
    field: &str,
) {
    if let Some(value) = source.get(field) {
        target.insert(field.to_string(), value.clone());
    }
}

fn prune_blueprint_graph_pin(
    pin: &mut serde_json::Map<String, serde_json::Value>,
    include_pin_defaults: bool,
    include_connections: bool,
) {
    if !include_pin_defaults {
        pin.remove("defaultValue");
        pin.remove("defaultObject");
        pin.remove("defaultText");
        pin.remove("default");
    }
    if !include_connections {
        pin.remove("linkedTo");
        pin.remove("links");
    }
}

fn compact_blueprint_graph_pin(
    pin: &serde_json::Map<String, serde_json::Value>,
    include_pin_defaults: bool,
    include_connections: bool,
) -> serde_json::Value {
    let mut compact = serde_json::Map::new();
    for field in [
        "name",
        "direction",
        "category",
        "subCategory",
        "subCategoryObject",
        "type",
        "isReference",
        "isConst",
        "isArray",
    ] {
        copy_json_field(pin, &mut compact, field);
    }
    if include_pin_defaults {
        for field in ["defaultValue", "defaultObject", "defaultText", "default"] {
            copy_json_field(pin, &mut compact, field);
        }
    }
    if include_connections {
        for field in ["linkedTo", "links"] {
            copy_json_field(pin, &mut compact, field);
        }
    }
    serde_json::Value::Object(compact)
}

fn prune_blueprint_graph_comment_fields(
    node: &mut serde_json::Map<String, serde_json::Value>,
    include_comments: bool,
) {
    if include_comments {
        return;
    }
    node.remove("comment");
    node.remove("nodeComment");
    if let Some(k2_extensions) = node
        .get_mut("k2Extensions")
        .and_then(|value| value.as_object_mut())
    {
        k2_extensions.remove("comment");
    }
}

fn compact_blueprint_graph_node(
    node: &serde_json::Map<String, serde_json::Value>,
    include_pin_defaults: bool,
    include_connections: bool,
    include_comments: bool,
    include_pins: bool,
) -> serde_json::Value {
    let mut compact = serde_json::Map::new();
    for field in [
        "id",
        "guid",
        "name",
        "className",
        "nodeClassPath",
        "title",
        "nodeTitle",
        "enabled",
        "position",
        "childGraphRef",
        "graphBoundarySummary",
        "hasNodeEditCapabilities",
        "inspectWith",
    ] {
        copy_json_field(node, &mut compact, field);
    }
    prune_blueprint_graph_comment_fields(&mut compact, include_comments);

    if include_pins {
        let pins = node
            .get("pins")
            .and_then(|value| value.as_array())
            .map(|pins| {
                pins.iter()
                    .filter_map(|pin| pin.as_object())
                    .map(|pin| {
                        compact_blueprint_graph_pin(pin, include_pin_defaults, include_connections)
                    })
                    .collect::<Vec<_>>()
            })
            .unwrap_or_default();
        compact.insert("pins".to_string(), serde_json::Value::Array(pins));
    }

    serde_json::Value::Object(compact)
}

#[derive(Clone, Debug, Eq, PartialEq, Hash)]
struct BlueprintGraphLink {
    from_node_id: String,
    from_pin: String,
    to_node_id: String,
    to_pin: String,
    kind: String,
}

impl BlueprintGraphLink {
    fn to_json(&self) -> serde_json::Value {
        serde_json::json!({
            "fromNodeId": self.from_node_id,
            "fromPin": self.from_pin,
            "toNodeId": self.to_node_id,
            "toPin": self.to_pin,
            "kind": self.kind,
        })
    }

    fn from_key(&self) -> String {
        format!("{}:{}", self.from_node_id, self.from_pin)
    }

    fn to_key(&self) -> String {
        format!("{}:{}", self.to_node_id, self.to_pin)
    }
}

fn blueprint_node_ref(id: &str) -> serde_json::Value {
    serde_json::json!({ "id": id })
}

fn json_str<'a>(
    object: &'a serde_json::Map<String, serde_json::Value>,
    field: &str,
) -> Option<&'a str> {
    object.get(field).and_then(|value| value.as_str())
}

fn blueprint_node_id(node: &serde_json::Map<String, serde_json::Value>) -> Option<&str> {
    json_str(node, "id").or_else(|| json_str(node, "guid"))
}

fn blueprint_node_text(node: &serde_json::Map<String, serde_json::Value>) -> String {
    [
        json_str(node, "className"),
        json_str(node, "classPath"),
        json_str(node, "nodeClassPath"),
        json_str(node, "title"),
        json_str(node, "nodeTitle"),
        json_str(node, "name"),
    ]
    .into_iter()
    .flatten()
    .collect::<Vec<_>>()
    .join(" ")
    .to_ascii_lowercase()
}

fn is_exec_pin(pin: &serde_json::Map<String, serde_json::Value>) -> bool {
    json_str(pin, "category")
        .or_else(|| json_str(pin, "type"))
        .is_some_and(|value| value.eq_ignore_ascii_case("exec"))
}

fn pin_direction(pin: &serde_json::Map<String, serde_json::Value>) -> Option<&str> {
    json_str(pin, "direction")
}

fn is_output_pin(pin: &serde_json::Map<String, serde_json::Value>) -> bool {
    pin_direction(pin).is_some_and(|value| value.eq_ignore_ascii_case("output"))
}

fn is_input_pin(pin: &serde_json::Map<String, serde_json::Value>) -> bool {
    pin_direction(pin).is_some_and(|value| value.eq_ignore_ascii_case("input"))
}

fn pin_name(pin: &serde_json::Map<String, serde_json::Value>) -> Option<&str> {
    json_str(pin, "name").filter(|value| !value.is_empty())
}

fn peer_node_id(peer: &serde_json::Map<String, serde_json::Value>) -> Option<&str> {
    json_str(peer, "nodeId")
        .or_else(|| json_str(peer, "nodeGuid"))
        .or_else(|| json_str(peer, "guid"))
}

fn peer_pin_name(peer: &serde_json::Map<String, serde_json::Value>) -> Option<&str> {
    json_str(peer, "pin")
        .or_else(|| json_str(peer, "pinName"))
        .filter(|value| !value.is_empty())
}

fn pin_lookup_key(node_id: &str, pin: &str) -> String {
    format!("{node_id}:{pin}")
}

fn traversal_usize(args: &rmcp::model::JsonObject, field: &str, default: usize) -> usize {
    let max = match field {
        "maxDepth" => 128,
        "maxNodes" => 1000,
        _ => usize::MAX,
    };
    args.get("traversal")
        .and_then(|value| value.as_object())
        .and_then(|traversal| traversal.get(field))
        .and_then(|value| value.as_u64())
        .map(|value| value.clamp(1, max as u64) as usize)
        .unwrap_or(default)
}

fn traversal_direction<'a>(args: &'a rmcp::model::JsonObject, default: &'a str) -> &'a str {
    args.get("traversal")
        .and_then(|value| value.as_object())
        .and_then(|traversal| traversal.get("direction"))
        .and_then(|value| value.as_str())
        .unwrap_or(default)
}

fn compact_blueprint_node_summary(
    node: &serde_json::Map<String, serde_json::Value>,
) -> serde_json::Value {
    compact_blueprint_graph_node(node, false, false, false, false)
}

fn blueprint_graph_pin_maps(
    nodes: &[serde_json::Value],
) -> (
    HashMap<String, serde_json::Map<String, serde_json::Value>>,
    HashMap<String, serde_json::Map<String, serde_json::Value>>,
) {
    let mut node_map = HashMap::new();
    let mut pin_map = HashMap::new();
    for node in nodes.iter().filter_map(|value| value.as_object()) {
        let Some(node_id) = blueprint_node_id(node) else {
            continue;
        };
        node_map.insert(node_id.to_string(), node.clone());
        if let Some(pins) = node.get("pins").and_then(|value| value.as_array()) {
            for pin in pins.iter().filter_map(|value| value.as_object()) {
                if let Some(name) = pin_name(pin) {
                    pin_map.insert(pin_lookup_key(node_id, name), pin.clone());
                }
            }
        }
    }
    (node_map, pin_map)
}

fn validate_blueprint_graph_inspect_targets(
    result: &serde_json::Map<String, serde_json::Value>,
    args: &rmcp::model::JsonObject,
) -> Option<CallToolResult> {
    let view = blueprint_graph_inspect_view(args);
    if view == "summary" {
        return None;
    }

    let nodes = result
        .get("semanticSnapshot")
        .and_then(|value| value.as_object())
        .and_then(|snapshot| snapshot.get("nodes"))
        .and_then(|value| value.as_array())?;
    let (node_map, pin_map) = blueprint_graph_pin_maps(nodes);

    if view == "exec_flow" {
        let root_id = args
            .get("rootNode")
            .and_then(|value| value.as_object())
            .and_then(|node| node.get("id"))
            .and_then(|value| value.as_str())
            .unwrap_or("");
        if !node_map.contains_key(root_id) {
            return Some(tool_error_result(
                "NODE_NOT_FOUND",
                format!(
                    "blueprint_graph_inspect view=exec_flow rootNode.id was not found: {root_id}."
                ),
            ));
        }
        return None;
    }

    if view == "data_flow" {
        let root_pin_object = args.get("rootPin").and_then(|value| value.as_object());
        let root_node_id = root_pin_object
            .and_then(|root| root.get("node"))
            .and_then(|value| value.as_object())
            .and_then(|node| node.get("id"))
            .and_then(|value| value.as_str())
            .unwrap_or("");
        let root_pin = root_pin_object
            .and_then(|root| root.get("pin"))
            .and_then(|value| value.as_str())
            .unwrap_or("");
        if !node_map.contains_key(root_node_id) {
            return Some(tool_error_result(
                "NODE_NOT_FOUND",
                format!("blueprint_graph_inspect view=data_flow rootPin.node.id was not found: {root_node_id}."),
            ));
        }
        if !pin_map.contains_key(&pin_lookup_key(root_node_id, root_pin)) {
            return Some(tool_error_result(
                "PIN_NOT_FOUND",
                format!("blueprint_graph_inspect view=data_flow rootPin.pin was not found on node {root_node_id}: {root_pin}."),
            ));
        }
    }

    None
}

fn build_blueprint_graph_links(
    nodes: &[serde_json::Value],
    pin_map: &HashMap<String, serde_json::Map<String, serde_json::Value>>,
) -> Vec<BlueprintGraphLink> {
    let mut links = Vec::new();
    let mut seen = HashSet::new();
    for node in nodes.iter().filter_map(|value| value.as_object()) {
        let Some(node_id) = blueprint_node_id(node) else {
            continue;
        };
        let Some(pins) = node.get("pins").and_then(|value| value.as_array()) else {
            continue;
        };
        for pin in pins.iter().filter_map(|value| value.as_object()) {
            let Some(this_pin) = pin_name(pin) else {
                continue;
            };
            let Some(peers) = pin.get("linkedTo").and_then(|value| value.as_array()) else {
                continue;
            };
            for peer in peers.iter().filter_map(|value| value.as_object()) {
                let Some(peer_node) = peer_node_id(peer) else {
                    continue;
                };
                let Some(peer_pin) = peer_pin_name(peer) else {
                    continue;
                };
                let peer_key = pin_lookup_key(peer_node, peer_pin);
                let peer_pin_object = pin_map.get(&peer_key);
                let kind = if is_exec_pin(pin)
                    || peer_pin_object.is_some_and(|peer_pin| is_exec_pin(peer_pin))
                {
                    "exec"
                } else {
                    "data"
                };
                let (from_node_id, from_pin, to_node_id, to_pin) = if is_output_pin(pin) {
                    (node_id, this_pin, peer_node, peer_pin)
                } else if is_input_pin(pin)
                    && peer_pin_object.is_some_and(|peer_pin| is_output_pin(peer_pin))
                {
                    (peer_node, peer_pin, node_id, this_pin)
                } else if node_id <= peer_node {
                    (node_id, this_pin, peer_node, peer_pin)
                } else {
                    (peer_node, peer_pin, node_id, this_pin)
                };
                let link = BlueprintGraphLink {
                    from_node_id: from_node_id.to_string(),
                    from_pin: from_pin.to_string(),
                    to_node_id: to_node_id.to_string(),
                    to_pin: to_pin.to_string(),
                    kind: kind.to_string(),
                };
                let key = format!(
                    "{}:{}>{}:{}:{}",
                    link.from_node_id, link.from_pin, link.to_node_id, link.to_pin, link.kind
                );
                if seen.insert(key) {
                    links.push(link);
                }
            }
        }
    }
    links
}

fn node_exec_output_pin_names(node: &serde_json::Map<String, serde_json::Value>) -> Vec<String> {
    node.get("pins")
        .and_then(|value| value.as_array())
        .map(|pins| {
            pins.iter()
                .filter_map(|value| value.as_object())
                .filter(|pin| is_exec_pin(pin) && is_output_pin(pin))
                .filter_map(pin_name)
                .map(str::to_string)
                .collect()
        })
        .unwrap_or_default()
}

fn is_blueprint_entry_node(node: &serde_json::Map<String, serde_json::Value>) -> bool {
    let text = blueprint_node_text(node);
    text.contains("k2node_event")
        || text.contains("k2node_customevent")
        || text.contains("functionentry")
        || text.contains("receivebeginplay")
        || text.contains("event ")
}

fn is_blueprint_boundary_output_node(node: &serde_json::Map<String, serde_json::Value>) -> bool {
    let text = blueprint_node_text(node);
    text.contains("functionresult") || text.contains("returnnode")
}

fn is_blueprint_branch_node(
    node: &serde_json::Map<String, serde_json::Value>,
    outgoing: usize,
) -> bool {
    if outgoing > 1 {
        return true;
    }
    let text = blueprint_node_text(node);
    text.contains("branch")
        || text.contains("ifthenelse")
        || text.contains("switch")
        || text.contains("select")
}

fn blueprint_effect_kind(
    node: &serde_json::Map<String, serde_json::Value>,
) -> Option<&'static str> {
    let text = blueprint_node_text(node);
    if text.contains("variableset") || text.contains("set variable") {
        Some("variable_write")
    } else if text.contains("callfunction") {
        Some("function_call")
    } else if text.contains("spawn") {
        Some("spawn")
    } else if text.contains("delegate") || text.contains("dispatcher") {
        Some("event_dispatch")
    } else {
        None
    }
}

fn traverse_exec_flow(
    root_id: &str,
    node_map: &HashMap<String, serde_json::Map<String, serde_json::Value>>,
    links: &[BlueprintGraphLink],
    direction: &str,
    max_depth: usize,
    max_nodes: usize,
) -> (Vec<String>, Vec<BlueprintGraphLink>, bool) {
    let mut visited = HashSet::new();
    let mut ordered = Vec::new();
    let mut traversed = Vec::new();
    let mut stack = vec![(root_id.to_string(), 0usize)];
    let allow_downstream = matches!(direction, "downstream" | "both");
    let allow_upstream = matches!(direction, "upstream" | "both");
    let mut truncated = false;
    while let Some((node_id, depth)) = stack.pop() {
        if !visited.insert(node_id.clone()) {
            continue;
        }
        ordered.push(node_id.clone());
        if ordered.len() >= max_nodes || depth >= max_depth {
            truncated = ordered.len() >= max_nodes;
            continue;
        }
        let mut next_links = Vec::new();
        if allow_downstream {
            next_links.extend(
                links
                    .iter()
                    .filter(|link| link.kind == "exec" && link.from_node_id == node_id)
                    .cloned(),
            );
        }
        if allow_upstream {
            next_links.extend(
                links
                    .iter()
                    .filter(|link| link.kind == "exec" && link.to_node_id == node_id)
                    .cloned(),
            );
        }
        for link in next_links {
            let next_node = if link.from_node_id == node_id {
                &link.to_node_id
            } else {
                &link.from_node_id
            };
            if node_map.contains_key(next_node) {
                traversed.push(link.clone());
                stack.push((next_node.clone(), depth + 1));
            }
        }
    }
    (ordered, traversed, truncated)
}

fn traverse_data_flow(
    root_node_id: &str,
    root_pin: &str,
    node_map: &HashMap<String, serde_json::Map<String, serde_json::Value>>,
    pin_map: &HashMap<String, serde_json::Map<String, serde_json::Value>>,
    links: &[BlueprintGraphLink],
    direction: &str,
    max_depth: usize,
    max_nodes: usize,
) -> (Vec<String>, Vec<BlueprintGraphLink>, bool) {
    let mut visited_pins = HashSet::new();
    let mut visited_nodes = HashSet::new();
    let mut ordered_nodes = Vec::new();
    let mut traversed = Vec::new();
    let mut stack = vec![(pin_lookup_key(root_node_id, root_pin), 0usize)];
    let allow_upstream = matches!(direction, "upstream" | "both");
    let allow_downstream = matches!(direction, "downstream" | "both");
    let mut truncated = false;

    while let Some((key, depth)) = stack.pop() {
        if !visited_pins.insert(key.clone()) {
            continue;
        }
        let Some((node_id, pin)) = key.split_once(':') else {
            continue;
        };
        if visited_nodes.insert(node_id.to_string()) {
            ordered_nodes.push(node_id.to_string());
            if ordered_nodes.len() >= max_nodes {
                truncated = true;
                continue;
            }
        }
        if depth >= max_depth {
            continue;
        }

        if allow_upstream {
            for link in links
                .iter()
                .filter(|link| link.kind == "data" && link.to_key() == key)
            {
                traversed.push(link.clone());
                stack.push((link.from_key(), depth + 1));
                if let Some(source_node) = node_map.get(&link.from_node_id) {
                    if let Some(pins) = source_node.get("pins").and_then(|value| value.as_array()) {
                        for input_pin in pins.iter().filter_map(|value| value.as_object()) {
                            if !is_exec_pin(input_pin) && is_input_pin(input_pin) {
                                if let Some(name) = pin_name(input_pin) {
                                    stack.push((
                                        pin_lookup_key(&link.from_node_id, name),
                                        depth + 1,
                                    ));
                                }
                            }
                        }
                    }
                }
            }
            if pin_map.get(&key).is_some_and(|pin| is_output_pin(pin))
                && node_map.contains_key(node_id)
            {
                if let Some(owner) = node_map.get(node_id) {
                    if let Some(pins) = owner.get("pins").and_then(|value| value.as_array()) {
                        for input_pin in pins.iter().filter_map(|value| value.as_object()) {
                            if !is_exec_pin(input_pin) && is_input_pin(input_pin) {
                                if let Some(name) = pin_name(input_pin) {
                                    stack.push((pin_lookup_key(node_id, name), depth + 1));
                                }
                            }
                        }
                    }
                }
            }
        }

        if allow_downstream {
            for link in links
                .iter()
                .filter(|link| link.kind == "data" && link.from_key() == key)
            {
                traversed.push(link.clone());
                stack.push((link.to_key(), depth + 1));
                if let Some(target_node) = node_map.get(&link.to_node_id) {
                    if let Some(pins) = target_node.get("pins").and_then(|value| value.as_array()) {
                        for output_pin in pins.iter().filter_map(|value| value.as_object()) {
                            if !is_exec_pin(output_pin) && is_output_pin(output_pin) {
                                if let Some(name) = pin_name(output_pin) {
                                    stack.push((pin_lookup_key(&link.to_node_id, name), depth + 1));
                                }
                            }
                        }
                    }
                }
            }
        }
        let _ = pin;
    }

    (ordered_nodes, traversed, truncated)
}

fn blueprint_graph_boundary_summary(
    node_map: &HashMap<String, serde_json::Map<String, serde_json::Value>>,
    exec_links: &[BlueprintGraphLink],
) -> serde_json::Value {
    let incoming = exec_links
        .iter()
        .map(|link| link.to_node_id.as_str())
        .collect::<HashSet<_>>();
    let outgoing = exec_links
        .iter()
        .map(|link| link.from_node_id.as_str())
        .collect::<HashSet<_>>();
    let entries = node_map
        .iter()
        .filter(|(id, node)| {
            is_blueprint_entry_node(node)
                || (!incoming.contains(id.as_str()) && outgoing.contains(id.as_str()))
        })
        .map(|(id, _)| blueprint_node_ref(id))
        .collect::<Vec<_>>();
    let outputs = node_map
        .iter()
        .filter(|(id, node)| {
            is_blueprint_boundary_output_node(node)
                || (incoming.contains(id.as_str()) && !outgoing.contains(id.as_str()))
        })
        .map(|(id, _)| blueprint_node_ref(id))
        .collect::<Vec<_>>();
    serde_json::json!({
        "entries": entries,
        "outputs": outputs,
    })
}

fn shape_blueprint_graph_summary(
    result: &mut serde_json::Map<String, serde_json::Value>,
    nodes: &[serde_json::Value],
    args: &rmcp::model::JsonObject,
) {
    let (node_map, pin_map) = blueprint_graph_pin_maps(nodes);
    let links = build_blueprint_graph_links(nodes, &pin_map);
    let exec_links = links
        .iter()
        .filter(|link| link.kind == "exec")
        .cloned()
        .collect::<Vec<_>>();
    let incoming = exec_links
        .iter()
        .map(|link| link.to_node_id.as_str())
        .collect::<HashSet<_>>();
    let outgoing = exec_links
        .iter()
        .map(|link| link.from_node_id.as_str())
        .collect::<HashSet<_>>();
    let mut root_ids = node_map
        .iter()
        .filter(|(id, node)| {
            is_blueprint_entry_node(node)
                || (!incoming.contains(id.as_str()) && outgoing.contains(id.as_str()))
        })
        .map(|(id, _)| id.clone())
        .collect::<Vec<_>>();
    root_ids.sort();
    let max_depth = traversal_usize(args, "maxDepth", 64);
    let max_nodes = traversal_usize(args, "maxNodes", 250);
    let mut covered = HashSet::new();
    let chains = root_ids
        .iter()
        .filter_map(|root_id| {
            let root_node = node_map.get(root_id)?;
            let (ordered, traversed, truncated) = traverse_exec_flow(
                root_id,
                &node_map,
                &exec_links,
                "downstream",
                max_depth,
                max_nodes,
            );
            covered.extend(ordered.iter().cloned());
            let branch_count = ordered
                .iter()
                .filter_map(|id| node_map.get(id))
                .filter(|node| {
                    let outgoing_count = traversed
                        .iter()
                        .filter(|link| link.from_node_id == blueprint_node_id(node).unwrap_or(""))
                        .count();
                    is_blueprint_branch_node(node, outgoing_count)
                })
                .count();
            let effects = ordered
                .iter()
                .filter_map(|id| node_map.get(id))
                .filter_map(|node| {
                    let node_id = blueprint_node_id(node)?;
                    blueprint_effect_kind(node).map(|kind| {
                        serde_json::json!({
                            "kind": kind,
                            "node": {"id": node_id},
                        })
                    })
                })
                .collect::<Vec<_>>();
            let path = ordered
                .iter()
                .take(12)
                .map(|id| blueprint_node_ref(id))
                .collect::<Vec<_>>();
            Some(serde_json::json!({
                "root": {"id": blueprint_node_id(root_node).unwrap_or(root_id)},
                "nodeCount": ordered.len(),
                "linkCount": traversed.len(),
                "branchCount": branch_count,
                "effects": effects,
                "path": path,
                "truncated": truncated,
            }))
        })
        .collect::<Vec<_>>();
    let loose = node_map
        .iter()
        .filter(|(id, _)| !covered.contains(*id))
        .map(|(id, _)| blueprint_node_ref(id))
        .collect::<Vec<_>>();
    let summary_nodes = node_map
        .iter()
        .map(|(id, node)| (id.clone(), compact_blueprint_node_summary(node)))
        .collect::<serde_json::Map<_, _>>();
    result.remove("semanticSnapshot");
    result.insert("view".into(), serde_json::json!("summary"));
    result.insert("nodes".into(), serde_json::Value::Object(summary_nodes));
    result.insert(
        "boundary".into(),
        blueprint_graph_boundary_summary(&node_map, &exec_links),
    );
    result.insert(
        "roots".into(),
        root_ids
            .iter()
            .map(|id| blueprint_node_ref(id))
            .collect::<Vec<_>>()
            .into(),
    );
    result.insert("chains".into(), chains.into());
    result.insert("looseNodes".into(), loose.into());
    result.insert(
        "linkCounts".into(),
        serde_json::json!({
            "exec": exec_links.len(),
            "data": links.iter().filter(|link| link.kind == "data").count(),
        }),
    );
}

fn shape_blueprint_exec_flow(
    result: &mut serde_json::Map<String, serde_json::Value>,
    nodes: &[serde_json::Value],
    args: &rmcp::model::JsonObject,
) {
    let (node_map, pin_map) = blueprint_graph_pin_maps(nodes);
    let links = build_blueprint_graph_links(nodes, &pin_map);
    let exec_links = links
        .iter()
        .filter(|link| link.kind == "exec")
        .cloned()
        .collect::<Vec<_>>();
    let root_id = args
        .get("rootNode")
        .and_then(|value| value.as_object())
        .and_then(|node| node.get("id"))
        .and_then(|value| value.as_str())
        .unwrap_or("");
    let direction = traversal_direction(args, "downstream");
    let (ordered, traversed, truncated) = traverse_exec_flow(
        root_id,
        &node_map,
        &exec_links,
        direction,
        traversal_usize(args, "maxDepth", 64),
        traversal_usize(args, "maxNodes", 250),
    );
    let mut open_exec_outputs = Vec::new();
    for node in ordered.iter().filter_map(|id| node_map.get(id)) {
        let node_id = blueprint_node_id(node).unwrap_or("").to_string();
        for pin in node_exec_output_pin_names(node) {
            if !traversed
                .iter()
                .any(|link| link.from_node_id == node_id && link.from_pin == pin)
            {
                open_exec_outputs.push(serde_json::json!({"node": {"id": node_id}, "pin": pin}));
            }
        }
    }
    result.remove("semanticSnapshot");
    result.insert("view".into(), serde_json::json!("exec_flow"));
    result.insert(
        "rootNode".into(),
        if node_map.contains_key(root_id) {
            serde_json::json!({"id": root_id})
        } else {
            serde_json::json!({"id": root_id, "missing": true})
        },
    );
    result.insert("direction".into(), serde_json::json!(direction));
    result.insert(
        "nodes".into(),
        ordered
            .iter()
            .filter_map(|id| node_map.get(id))
            .map(compact_blueprint_node_summary)
            .collect::<Vec<_>>()
            .into(),
    );
    result.insert(
        "links".into(),
        traversed
            .iter()
            .map(BlueprintGraphLink::to_json)
            .collect::<Vec<_>>()
            .into(),
    );
    result.insert("openExecOutputs".into(), open_exec_outputs.into());
    result.insert("truncated".into(), serde_json::json!(truncated));
}

fn shape_blueprint_data_flow(
    result: &mut serde_json::Map<String, serde_json::Value>,
    nodes: &[serde_json::Value],
    args: &rmcp::model::JsonObject,
) {
    let (node_map, pin_map) = blueprint_graph_pin_maps(nodes);
    let links = build_blueprint_graph_links(nodes, &pin_map);
    let data_links = links
        .iter()
        .filter(|link| link.kind == "data")
        .cloned()
        .collect::<Vec<_>>();
    let root_pin_object = args.get("rootPin").and_then(|value| value.as_object());
    let root_node_id = root_pin_object
        .and_then(|root| root.get("node"))
        .and_then(|value| value.as_object())
        .and_then(|node| node.get("id"))
        .and_then(|value| value.as_str())
        .unwrap_or("");
    let root_pin = root_pin_object
        .and_then(|root| root.get("pin"))
        .and_then(|value| value.as_str())
        .unwrap_or("");
    let direction = traversal_direction(args, "upstream");
    let (ordered, traversed, truncated) = traverse_data_flow(
        root_node_id,
        root_pin,
        &node_map,
        &pin_map,
        &data_links,
        direction,
        traversal_usize(args, "maxDepth", 64),
        traversal_usize(args, "maxNodes", 250),
    );
    let traversed_to_keys = traversed
        .iter()
        .map(BlueprintGraphLink::to_key)
        .collect::<HashSet<_>>();
    let mut open_inputs = Vec::new();
    for node in ordered.iter().filter_map(|id| node_map.get(id)) {
        let node_id = blueprint_node_id(node).unwrap_or("").to_string();
        let Some(pins) = node.get("pins").and_then(|value| value.as_array()) else {
            continue;
        };
        for pin in pins.iter().filter_map(|value| value.as_object()) {
            if is_exec_pin(pin) || !is_input_pin(pin) {
                continue;
            }
            let Some(name) = pin_name(pin) else {
                continue;
            };
            let key = pin_lookup_key(&node_id, name);
            if !traversed_to_keys.contains(&key) {
                open_inputs.push(serde_json::json!({"node": {"id": node_id}, "pin": name}));
            }
        }
    }
    result.remove("semanticSnapshot");
    result.insert("view".into(), serde_json::json!("data_flow"));
    result.insert(
        "rootPin".into(),
        serde_json::json!({"node": {"id": root_node_id}, "pin": root_pin}),
    );
    result.insert("direction".into(), serde_json::json!(direction));
    result.insert(
        "nodes".into(),
        ordered
            .iter()
            .filter_map(|id| node_map.get(id))
            .map(compact_blueprint_node_summary)
            .collect::<Vec<_>>()
            .into(),
    );
    result.insert(
        "links".into(),
        traversed
            .iter()
            .map(BlueprintGraphLink::to_json)
            .collect::<Vec<_>>()
            .into(),
    );
    result.insert("openInputs".into(), open_inputs.into());
    result.insert("truncated".into(), serde_json::json!(truncated));
}

fn shape_blueprint_graph_inspect_result(
    result: &mut serde_json::Map<String, serde_json::Value>,
    args: &rmcp::model::JsonObject,
) {
    let view = blueprint_graph_inspect_view(args);
    let nodes = result
        .get("semanticSnapshot")
        .and_then(|value| value.as_object())
        .and_then(|snapshot| snapshot.get("nodes"))
        .and_then(|value| value.as_array())
        .cloned()
        .unwrap_or_default();

    match view {
        "exec_flow" => shape_blueprint_exec_flow(result, &nodes, args),
        "data_flow" => shape_blueprint_data_flow(result, &nodes, args),
        _ => shape_blueprint_graph_summary(result, &nodes, args),
    }

    if let Some(meta) = result
        .get_mut("meta")
        .and_then(|value| value.as_object_mut())
    {
        meta.insert("view".to_string(), serde_json::json!(view));
    }
}

fn compact_pcg_graph_pin(
    pin: &serde_json::Map<String, serde_json::Value>,
    include_pin_defaults: bool,
    include_connections: bool,
) -> serde_json::Value {
    let mut compact = serde_json::Map::new();
    for field in ["name", "direction", "category", "type", "isArray"] {
        copy_json_field(pin, &mut compact, field);
    }
    if include_pin_defaults {
        for field in ["defaultValue", "defaultObject", "defaultText", "default"] {
            copy_json_field(pin, &mut compact, field);
        }
    }
    if include_connections {
        for field in ["links", "linkedTo"] {
            copy_json_field(pin, &mut compact, field);
        }
    }
    serde_json::Value::Object(compact)
}

fn compact_pcg_graph_node(
    node: &serde_json::Map<String, serde_json::Value>,
    view: &str,
    include_pin_defaults: bool,
    include_connections: bool,
) -> serde_json::Value {
    if view == "full" {
        let mut full = node.clone();
        if let Some(pins) = full.get_mut("pins").and_then(|value| value.as_array_mut()) {
            for pin in pins {
                if let Some(pin_object) = pin.as_object_mut() {
                    prune_blueprint_graph_pin(
                        pin_object,
                        include_pin_defaults,
                        include_connections,
                    );
                }
            }
        }
        return serde_json::Value::Object(full);
    }

    let mut compact = serde_json::Map::new();
    for field in [
        "id",
        "guid",
        "nodeClassPath",
        "title",
        "enabled",
        "position",
        "layout",
        "childGraphRef",
        "settings",
    ] {
        copy_json_field(node, &mut compact, field);
    }

    if matches!(view, "pins" | "links" | "defaults") {
        let pins = node
            .get("pins")
            .and_then(|value| value.as_array())
            .map(|pins| {
                pins.iter()
                    .filter_map(|pin| pin.as_object())
                    .map(|pin| {
                        compact_pcg_graph_pin(pin, include_pin_defaults, include_connections)
                    })
                    .collect::<Vec<_>>()
            })
            .unwrap_or_default();
        compact.insert("pins".to_string(), serde_json::Value::Array(pins));
    }

    serde_json::Value::Object(compact)
}

fn pcg_node_matches_text(node: &serde_json::Map<String, serde_json::Value>, text: &str) -> bool {
    let needle = text.to_lowercase();
    if needle.is_empty() {
        return true;
    }
    for field in ["id", "guid", "nodeClassPath", "title"] {
        if node
            .get(field)
            .and_then(|value| value.as_str())
            .is_some_and(|value| value.to_lowercase().contains(&needle))
        {
            return true;
        }
    }
    serde_json::to_string(node)
        .map(|value| value.to_lowercase().contains(&needle))
        .unwrap_or(false)
}

fn parse_page_cursor(args: &rmcp::model::JsonObject) -> usize {
    args.get("page")
        .and_then(|value| value.as_object())
        .and_then(|page| page.get("cursor"))
        .and_then(|value| value.as_str())
        .and_then(|value| value.parse::<usize>().ok())
        .unwrap_or(0)
}

fn parse_page_limit(args: &rmcp::model::JsonObject, default_limit: usize) -> usize {
    args.get("page")
        .and_then(|value| value.as_object())
        .and_then(|page| page.get("limit"))
        .and_then(|value| value.as_u64())
        .map(|value| value.clamp(1, 1000) as usize)
        .unwrap_or(default_limit)
}

fn shape_pcg_graph_inspect_result(
    result: &mut serde_json::Map<String, serde_json::Value>,
    args: &rmcp::model::JsonObject,
) {
    let view = pcg_graph_inspect_view(args);
    let include_pin_defaults = matches!(view, "defaults" | "full");
    let include_connections = matches!(view, "links" | "defaults" | "full");
    let text_filter = args
        .get("filter")
        .and_then(|value| value.as_object())
        .and_then(|filter| filter.get("text"))
        .and_then(|value| value.as_str())
        .map(str::to_owned);
    let cursor = parse_page_cursor(args);
    let limit = parse_page_limit(args, 50);

    let mut returned_nodes = 0usize;
    let mut total_nodes_after_filter = 0usize;
    let mut kept_node_ids = std::collections::HashSet::new();
    let mut next_cursor = String::new();

    if let Some(snapshot) = result
        .get_mut("semanticSnapshot")
        .and_then(|value| value.as_object_mut())
    {
        let mut filtered_nodes = snapshot
            .get("nodes")
            .and_then(|value| value.as_array())
            .cloned()
            .unwrap_or_default()
            .into_iter()
            .filter(|node| {
                let Some(node_object) = node.as_object() else {
                    return false;
                };
                text_filter
                    .as_deref()
                    .is_none_or(|text| pcg_node_matches_text(node_object, text))
            })
            .collect::<Vec<_>>();

        total_nodes_after_filter = filtered_nodes.len();
        let end = (cursor + limit).min(total_nodes_after_filter);
        if end < total_nodes_after_filter {
            next_cursor = end.to_string();
        }
        filtered_nodes = filtered_nodes
            .into_iter()
            .skip(cursor)
            .take(limit)
            .map(|node| {
                if let Some(node_object) = node.as_object() {
                    if let Some(id) = node_object.get("id").and_then(|value| value.as_str()) {
                        kept_node_ids.insert(id.to_owned());
                    }
                    compact_pcg_graph_node(
                        node_object,
                        view,
                        include_pin_defaults,
                        include_connections,
                    )
                } else {
                    node
                }
            })
            .collect::<Vec<_>>();
        returned_nodes = filtered_nodes.len();
        snapshot.insert(
            "nodes".to_string(),
            serde_json::Value::Array(filtered_nodes),
        );

        if include_connections {
            let filtered_edges = snapshot
                .get("edges")
                .and_then(|value| value.as_array())
                .cloned()
                .unwrap_or_default()
                .into_iter()
                .filter(|edge| {
                    let Some(edge_object) = edge.as_object() else {
                        return false;
                    };
                    let from_kept = edge_object
                        .get("fromNodeId")
                        .and_then(|value| value.as_str())
                        .is_some_and(|id| kept_node_ids.contains(id));
                    let to_kept = edge_object
                        .get("toNodeId")
                        .and_then(|value| value.as_str())
                        .is_some_and(|id| kept_node_ids.contains(id));
                    from_kept || to_kept
                })
                .collect::<Vec<_>>();
            snapshot.insert(
                "edges".to_string(),
                serde_json::Value::Array(filtered_edges),
            );
        } else {
            snapshot.insert("edges".to_string(), serde_json::json!([]));
        }
    }

    if let Some(meta) = result
        .get_mut("meta")
        .and_then(|value| value.as_object_mut())
    {
        meta.insert("view".to_string(), serde_json::json!(view));
        meta.insert(
            "totalNodes".to_string(),
            serde_json::json!(total_nodes_after_filter),
        );
        meta.insert(
            "returnedNodes".to_string(),
            serde_json::json!(returned_nodes),
        );
        meta.insert(
            "truncated".to_string(),
            serde_json::json!(!next_cursor.is_empty()),
        );
    }
    result.insert("nextCursor".to_string(), serde_json::json!(next_cursor));
}

fn translate_blueprint_palette_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    if args.contains_key("graphName") || args.contains_key("graphRef") {
        return Err(invalid_argument_result(
            "blueprint.graph.palette uses graph:{id|name}; graphName and graphRef are not public inputs.",
        ));
    }
    let asset_path = read_required_asset_path(args, "blueprint.graph.palette")?;
    let graph_address =
        read_optional_graph_address(args, &asset_path, true, "blueprint.graph.palette")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    write_optional_graph_address(&mut translated, graph_address);
    for field in ["query", "contextSensitive", "limit", "offset"] {
        copy_if_present(args, &mut translated, field);
    }
    if let Some(context) = args.get("context") {
        translated.insert("context".into(), context.clone());
    }
    if let Some(from_pins) = args.get("fromPins") {
        translated.insert(
            "fromPins".into(),
            normalize_blueprint_palette_from_pins(from_pins, "blueprint.graph.palette")?,
        );
    }
    validate_integer_range(&translated, "limit", 1, 500, "blueprint.graph.palette")?;
    validate_integer_range(
        &translated,
        "offset",
        0,
        i64::MAX,
        "blueprint.graph.palette",
    )?;
    Ok(translated)
}

fn translate_blueprint_compile_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint.compile")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    let graph_address = read_optional_graph_address(args, &asset_path, false, "blueprint.compile")?
        .unwrap_or_else(|| BlueprintGraphAddress::Name("EventGraph".to_string()));
    write_optional_graph_address(&mut translated, Some(graph_address));
    for field in ["limit", "cursor", "layoutDetail"] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
}

#[derive(Clone, Debug)]
struct BlueprintGraphLayoutRequest {
    asset_path: String,
    graph_address: Option<BlueprintGraphAddress>,
    root: String,
    spacing_x: i64,
    spacing_y: i64,
    origin: Option<(i64, i64)>,
    dry_run: bool,
    expected_revision: Option<String>,
}

#[derive(Clone, Debug)]
struct BlueprintLayoutNode {
    id: String,
    x: i64,
    y: i64,
    exec_edges: Vec<BlueprintLayoutExecEdge>,
    data_inputs: Vec<BlueprintLayoutDataInput>,
    pin_layout_sources: Vec<String>,
}

#[derive(Clone, Debug)]
struct BlueprintLayoutExecEdge {
    target_id: String,
    source_pin: String,
    source_offset_y: i64,
    target_pin: Option<String>,
}

#[derive(Clone, Debug)]
struct BlueprintLayoutDataInput {
    source_id: String,
}

#[derive(Clone, Debug)]
struct BlueprintLayoutPlacedNode {
    x: i64,
    y: i64,
    role: String,
    constraints: Vec<String>,
}

#[derive(Clone, Debug)]
struct BlueprintLayoutMove {
    node_id: String,
    role: String,
    constraints: Vec<String>,
    from_x: i64,
    from_y: i64,
    to_x: i64,
    to_y: i64,
}

#[derive(Clone, Debug)]
struct BlueprintGraphLayoutPlan {
    root: String,
    resolved_node_count: usize,
    support_node_count: usize,
    moves: Vec<BlueprintLayoutMove>,
    diagnostics: Vec<serde_json::Value>,
    previous_revision: Option<String>,
    graph_ref: Option<serde_json::Value>,
}

impl BlueprintGraphLayoutPlan {
    fn changed(&self) -> bool {
        !self.moves.is_empty()
    }

    fn to_move_commands(&self) -> Vec<serde_json::Value> {
        self.moves
            .iter()
            .map(|movement| {
                serde_json::json!({
                    "kind": "moveNode",
                    "node": { "id": movement.node_id },
                    "position": { "x": movement.to_x, "y": movement.to_y }
                })
            })
            .collect()
    }

    fn to_json_with_revisions(
        &self,
        dry_run: bool,
        previous_revision_override: Option<String>,
        new_revision_override: Option<String>,
    ) -> serde_json::Value {
        let previous_revision = previous_revision_override
            .or_else(|| self.previous_revision.clone())
            .unwrap_or_else(|| "unknown".to_string());
        let new_revision = new_revision_override
            .or_else(|| {
                if dry_run {
                    Some(previous_revision.clone())
                } else {
                    None
                }
            })
            .or_else(|| self.previous_revision.clone())
            .unwrap_or_else(|| previous_revision.clone());
        serde_json::json!({
            "isError": false,
            "valid": true,
            "applied": !dry_run && self.changed(),
            "dryRun": dry_run,
            "operation": "blueprint.graph.layout",
            "root": { "id": self.root },
            "previousRevision": previous_revision,
            "newRevision": new_revision,
            "graphRef": self.graph_ref,
            "planned": {
                "style": "exec_tree",
                "resolvedNodeCount": self.resolved_node_count,
                "supportNodeCount": self.support_node_count,
                "moves": self.moves.iter().map(|movement| serde_json::json!({
                    "node": { "id": movement.node_id },
                    "role": movement.role,
                    "from": { "x": movement.from_x, "y": movement.from_y },
                    "to": { "x": movement.to_x, "y": movement.to_y },
                    "constraints": movement.constraints,
                })).collect::<Vec<_>>(),
            },
            "diff": {
                "scope": "blueprint.graph.layout",
                "changes": self.moves.iter().map(|movement| serde_json::json!({
                    "kind": "update",
                    "target": { "type": "node", "id": movement.node_id },
                    "before": { "position": { "x": movement.from_x, "y": movement.from_y } },
                    "after": { "position": { "x": movement.to_x, "y": movement.to_y } },
                })).collect::<Vec<_>>(),
            },
            "diagnostics": self.diagnostics,
        })
    }

    fn to_json(&self, dry_run: bool) -> serde_json::Value {
        self.to_json_with_revisions(dry_run, None, None)
    }
}

fn blueprint_layout_revision_pair(payload: &serde_json::Value) -> (Option<String>, Option<String>) {
    let previous_revision = payload
        .get("previousRevision")
        .and_then(|value| value.as_str())
        .map(str::to_owned);
    let new_revision = payload
        .get("newRevision")
        .and_then(|value| value.as_str())
        .map(str::to_owned);
    (previous_revision, new_revision)
}

impl BlueprintGraphLayoutPlan {
    fn to_json_from_edit_result(
        &self,
        edit_payload: Option<&serde_json::Value>,
    ) -> serde_json::Value {
        let (previous_revision, new_revision) = edit_payload
            .map(blueprint_layout_revision_pair)
            .unwrap_or((None, None));
        self.to_json_with_revisions(false, previous_revision, new_revision)
    }
}

fn parse_node_ref_id(value: &serde_json::Value, field_name: &str) -> Result<String, String> {
    value
        .as_object()
        .and_then(|object| object.get("id"))
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
        .ok_or_else(|| format!("{field_name} requires a node reference with id."))
}

fn parse_i64_object_field(
    object: &serde_json::Map<String, serde_json::Value>,
    object_name: &str,
    field_name: &str,
) -> Result<i64, String> {
    object
        .get(field_name)
        .and_then(|value| {
            value
                .as_i64()
                .or_else(|| value.as_f64().map(|number| number as i64))
        })
        .ok_or_else(|| format!("{object_name}.{field_name} must be a number."))
}

fn parse_blueprint_graph_layout_request(
    args: &rmcp::model::JsonObject,
) -> Result<BlueprintGraphLayoutRequest, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint.graph.layout")?;
    let graph_address =
        read_optional_graph_address(args, &asset_path, true, "blueprint.graph.layout")?;

    for retired_field in ["operation", "scope", "direction", "style"] {
        if args.contains_key(retired_field) {
            return Err(invalid_argument_result(format!(
                "blueprint.graph.layout no longer accepts {retired_field}; pass root instead."
            )));
        }
    }
    let root = args
        .get("root")
        .ok_or_else(|| invalid_argument_result("blueprint.graph.layout requires root."))?;
    let root = parse_node_ref_id(root, "root").map_err(invalid_argument_result)?;

    let (spacing_x, spacing_y) =
        if let Some(spacing) = args.get("spacing").and_then(|value| value.as_object()) {
            (
                parse_i64_object_field(spacing, "spacing", "x").map_err(invalid_argument_result)?,
                parse_i64_object_field(spacing, "spacing", "y").map_err(invalid_argument_result)?,
            )
        } else {
            (360, 180)
        };

    let origin = if let Some(origin) = args.get("origin").and_then(|value| value.as_object()) {
        Some((
            parse_i64_object_field(origin, "origin", "x").map_err(invalid_argument_result)?,
            parse_i64_object_field(origin, "origin", "y").map_err(invalid_argument_result)?,
        ))
    } else {
        None
    };

    Ok(BlueprintGraphLayoutRequest {
        asset_path,
        graph_address,
        root,
        spacing_x,
        spacing_y,
        origin,
        dry_run: args
            .get("dryRun")
            .and_then(|value| value.as_bool())
            .unwrap_or(false),
        expected_revision: args
            .get("expectedRevision")
            .and_then(|value| value.as_str())
            .map(str::to_owned),
    })
}

fn blueprint_layout_node_id(node: &serde_json::Map<String, serde_json::Value>) -> Option<String> {
    node.get("id")
        .or_else(|| node.get("guid"))
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
}

fn blueprint_layout_node_position(
    node: &serde_json::Map<String, serde_json::Value>,
) -> Option<(i64, i64)> {
    if let Some(position) = node.get("position").and_then(|value| value.as_object()) {
        let x = position.get("x").and_then(|value| {
            value
                .as_i64()
                .or_else(|| value.as_f64().map(|number| number as i64))
        });
        let y = position.get("y").and_then(|value| {
            value
                .as_i64()
                .or_else(|| value.as_f64().map(|number| number as i64))
        });
        if let (Some(x), Some(y)) = (x, y) {
            return Some((x, y));
        }
    }
    let x = node.get("nodePosX").and_then(|value| {
        value
            .as_i64()
            .or_else(|| value.as_f64().map(|number| number as i64))
    });
    let y = node.get("nodePosY").and_then(|value| {
        value
            .as_i64()
            .or_else(|| value.as_f64().map(|number| number as i64))
    });
    x.zip(y)
}

fn blueprint_layout_pin_name(pin: &serde_json::Map<String, serde_json::Value>) -> Option<String> {
    pin.get("name")
        .or_else(|| pin.get("pinName"))
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
}

fn blueprint_layout_pin_category(
    pin: &serde_json::Map<String, serde_json::Value>,
) -> Option<String> {
    pin.get("category")
        .or_else(|| pin.get("pinCategory"))
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
}

fn blueprint_layout_pin_direction(
    pin: &serde_json::Map<String, serde_json::Value>,
) -> Option<String> {
    pin.get("direction")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
}

fn blueprint_layout_number(value: Option<&serde_json::Value>) -> Option<i64> {
    value.and_then(|value| {
        value
            .as_i64()
            .or_else(|| value.as_f64().map(|number| number as i64))
    })
}

fn blueprint_layout_pin_offset_y(
    pin: &serde_json::Map<String, serde_json::Value>,
    node_y: i64,
) -> (i64, Option<String>) {
    let layout = pin.get("layout").and_then(|value| value.as_object());
    let source = layout
        .and_then(|object| object.get("source"))
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned);
    if let Some(offset_y) = layout
        .and_then(|object| object.get("offset"))
        .and_then(|value| value.as_object())
        .and_then(|object| blueprint_layout_number(object.get("y")))
    {
        return (offset_y, source);
    }
    if let Some(anchor_y) = layout
        .and_then(|object| object.get("anchor"))
        .and_then(|value| value.as_object())
        .and_then(|object| blueprint_layout_number(object.get("y")))
    {
        return (anchor_y - node_y, source);
    }
    (31, source.or_else(|| Some("unsupported".to_string())))
}

fn blueprint_layout_target_pin_offset_y(
    nodes: &std::collections::HashMap<String, BlueprintLayoutNode>,
    raw_nodes: &std::collections::HashMap<String, serde_json::Map<String, serde_json::Value>>,
    target_id: &str,
    target_pin_name: Option<&str>,
) -> i64 {
    let Some(target_node) = nodes.get(target_id) else {
        return 31;
    };
    let Some(raw_node) = raw_nodes.get(target_id) else {
        return 31;
    };
    let Some(pins) = raw_node.get("pins").and_then(|value| value.as_array()) else {
        return 31;
    };
    for pin in pins {
        let Some(pin_obj) = pin.as_object() else {
            continue;
        };
        let is_exec_input = blueprint_layout_pin_direction(pin_obj)
            .is_some_and(|value| value.eq_ignore_ascii_case("input"))
            && blueprint_layout_pin_category(pin_obj)
                .is_some_and(|value| value.eq_ignore_ascii_case("exec"));
        if !is_exec_input {
            continue;
        }
        let name_matches = target_pin_name
            .and_then(|target_pin| {
                blueprint_layout_pin_name(pin_obj).map(|name| name == target_pin)
            })
            .unwrap_or(true);
        if name_matches {
            return blueprint_layout_pin_offset_y(pin_obj, target_node.y).0;
        }
    }
    31
}

fn build_blueprint_layout_nodes(
    inspect_payload: &serde_json::Value,
) -> Result<
    (
        std::collections::HashMap<String, BlueprintLayoutNode>,
        std::collections::HashMap<String, serde_json::Map<String, serde_json::Value>>,
        Option<String>,
        Option<serde_json::Value>,
    ),
    String,
> {
    let nodes = inspect_payload
        .get("semanticSnapshot")
        .and_then(|value| value.get("nodes"))
        .and_then(|value| value.as_array())
        .ok_or_else(|| {
            "blueprint_graph_inspect result missing semanticSnapshot.nodes.".to_string()
        })?;
    let mut result = std::collections::HashMap::new();
    let mut raw_nodes = std::collections::HashMap::new();

    for node_value in nodes {
        let Some(node_obj) = node_value.as_object() else {
            continue;
        };
        let Some(id) = blueprint_layout_node_id(node_obj) else {
            continue;
        };
        let Some((_x, _y)) = blueprint_layout_node_position(node_obj) else {
            continue;
        };
        raw_nodes.insert(id.clone(), node_obj.clone());
    }

    for node_value in nodes {
        let Some(node_obj) = node_value.as_object() else {
            continue;
        };
        let Some(id) = blueprint_layout_node_id(node_obj) else {
            continue;
        };
        let Some((x, y)) = blueprint_layout_node_position(node_obj) else {
            continue;
        };
        let mut exec_edges = Vec::new();
        let mut data_inputs = Vec::new();
        let mut pin_layout_sources = Vec::new();
        if let Some(pins) = node_obj.get("pins").and_then(|value| value.as_array()) {
            for pin in pins {
                let Some(pin_obj) = pin.as_object() else {
                    continue;
                };
                let category = blueprint_layout_pin_category(pin_obj).unwrap_or_default();
                let direction = blueprint_layout_pin_direction(pin_obj).unwrap_or_default();
                let pin_name = blueprint_layout_pin_name(pin_obj).unwrap_or_default();
                let (_, source) = blueprint_layout_pin_offset_y(pin_obj, y);
                if let Some(source) = source {
                    pin_layout_sources.push(source);
                }
                let is_exec_output = pin_obj
                    .get("direction")
                    .and_then(|value| value.as_str())
                    .is_some_and(|value| value.eq_ignore_ascii_case("output"))
                    && category.eq_ignore_ascii_case("exec");
                if let Some(linked_to) = pin_obj.get("linkedTo").and_then(|value| value.as_array())
                {
                    for link in linked_to {
                        if let Some(target_id) = link
                            .as_object()
                            .and_then(|object| object.get("nodeGuid"))
                            .and_then(|value| value.as_str())
                            .filter(|value| !value.is_empty())
                        {
                            if target_id == id {
                                continue;
                            }
                            let target_pin = link
                                .as_object()
                                .and_then(|object| {
                                    object.get("pin").or_else(|| object.get("pinName"))
                                })
                                .and_then(|value| value.as_str())
                                .map(str::to_owned);
                            if is_exec_output {
                                let source_offset_y = blueprint_layout_pin_offset_y(pin_obj, y).0;
                                if !exec_edges.iter().any(|edge: &BlueprintLayoutExecEdge| {
                                    edge.target_id == target_id && edge.source_pin == pin_name
                                }) {
                                    exec_edges.push(BlueprintLayoutExecEdge {
                                        target_id: target_id.to_owned(),
                                        source_pin: pin_name.clone(),
                                        source_offset_y,
                                        target_pin,
                                    });
                                }
                            } else if direction.eq_ignore_ascii_case("input")
                                && !category.eq_ignore_ascii_case("exec")
                            {
                                data_inputs.push(BlueprintLayoutDataInput {
                                    source_id: target_id.to_owned(),
                                });
                            }
                        }
                    }
                }
            }
        }
        pin_layout_sources.sort();
        pin_layout_sources.dedup();
        result.insert(
            id.clone(),
            BlueprintLayoutNode {
                id,
                x,
                y,
                exec_edges,
                data_inputs,
                pin_layout_sources,
            },
        );
    }

    let previous_revision = inspect_payload
        .get("revision")
        .and_then(|value| value.as_str())
        .map(str::to_owned)
        .or_else(|| {
            inspect_payload
                .get("meta")
                .and_then(|value| value.get("revision"))
                .and_then(|value| value.as_str())
                .map(str::to_owned)
        });
    let graph_ref = inspect_payload
        .get("graphRef")
        .cloned()
        .or_else(|| inspect_payload.get("graph").cloned());
    Ok((result, raw_nodes, previous_revision, graph_ref))
}

fn collect_blueprint_layout_tree_order(
    root: &str,
    nodes: &std::collections::HashMap<String, BlueprintLayoutNode>,
) -> Vec<String> {
    fn visit(
        node_id: &str,
        nodes: &std::collections::HashMap<String, BlueprintLayoutNode>,
        visited: &mut std::collections::HashSet<String>,
        order: &mut Vec<String>,
    ) {
        if !visited.insert(node_id.to_owned()) {
            return;
        }
        order.push(node_id.to_owned());
        let Some(node) = nodes.get(node_id) else {
            return;
        };
        for edge in &node.exec_edges {
            if nodes.contains_key(&edge.target_id) {
                visit(&edge.target_id, nodes, visited, order);
            }
        }
    }

    let mut visited = std::collections::HashSet::new();
    let mut order = Vec::new();
    visit(root, nodes, &mut visited, &mut order);
    order
}

fn build_blueprint_graph_layout_plan(
    request: &BlueprintGraphLayoutRequest,
    inspect_payload: &serde_json::Value,
) -> Result<BlueprintGraphLayoutPlan, String> {
    let (nodes, raw_nodes, previous_revision, graph_ref) =
        build_blueprint_layout_nodes(inspect_payload)?;
    if !nodes.contains_key(&request.root) {
        return Err(format!("root node not found in graph: {}", request.root));
    }

    let origin = if let Some(origin) = request.origin {
        origin
    } else {
        let root_node = nodes
            .get(&request.root)
            .ok_or_else(|| format!("root node not found in graph: {}", request.root))?;
        (root_node.x, root_node.y)
    };

    let tree_order = collect_blueprint_layout_tree_order(&request.root, &nodes);
    let tree_set = tree_order
        .iter()
        .cloned()
        .collect::<std::collections::HashSet<_>>();
    let mut placed = std::collections::HashMap::<String, BlueprintLayoutPlacedNode>::new();
    let mut diagnostics = Vec::<serde_json::Value>::new();

    fn place_exec_tree(
        node_id: &str,
        desired_x: i64,
        desired_y: i64,
        nodes: &std::collections::HashMap<String, BlueprintLayoutNode>,
        raw_nodes: &std::collections::HashMap<String, serde_json::Map<String, serde_json::Value>>,
        tree_set: &std::collections::HashSet<String>,
        request: &BlueprintGraphLayoutRequest,
        placed: &mut std::collections::HashMap<String, BlueprintLayoutPlacedNode>,
        active: &mut std::collections::HashSet<String>,
    ) {
        if placed.contains_key(node_id) || !active.insert(node_id.to_owned()) {
            return;
        }
        placed.insert(
            node_id.to_owned(),
            BlueprintLayoutPlacedNode {
                x: desired_x,
                y: desired_y,
                role: "exec".to_string(),
                constraints: vec!["exec_tree".to_string()],
            },
        );
        let Some(node) = nodes.get(node_id) else {
            active.remove(node_id);
            return;
        };
        for (lane_index, edge) in node.exec_edges.iter().enumerate() {
            if !tree_set.contains(&edge.target_id) {
                continue;
            }
            let target_input_offset = blueprint_layout_target_pin_offset_y(
                nodes,
                raw_nodes,
                &edge.target_id,
                edge.target_pin.as_deref(),
            );
            let child_x = desired_x + request.spacing_x;
            let child_y = desired_y + edge.source_offset_y - target_input_offset
                + lane_index as i64 * request.spacing_y;
            place_exec_tree(
                &edge.target_id,
                child_x,
                child_y,
                nodes,
                raw_nodes,
                tree_set,
                request,
                placed,
                active,
            );
        }
        active.remove(node_id);
    }

    place_exec_tree(
        &request.root,
        origin.0,
        origin.1,
        &nodes,
        &raw_nodes,
        &tree_set,
        request,
        &mut placed,
        &mut std::collections::HashSet::new(),
    );

    let mut support_consumers = std::collections::HashMap::<String, Vec<String>>::new();
    for node_id in &tree_order {
        let Some(node) = nodes.get(node_id) else {
            continue;
        };
        for data_input in &node.data_inputs {
            if tree_set.contains(&data_input.source_id)
                || !nodes.contains_key(&data_input.source_id)
            {
                continue;
            }
            support_consumers
                .entry(data_input.source_id.clone())
                .or_default()
                .push(node_id.clone());
        }
    }
    let mut support_by_consumer = std::collections::HashMap::<String, Vec<String>>::new();
    for (support_id, consumers) in support_consumers {
        let unique_consumers = consumers
            .into_iter()
            .collect::<std::collections::HashSet<_>>()
            .into_iter()
            .collect::<Vec<_>>();
        if unique_consumers.len() == 1 {
            support_by_consumer
                .entry(unique_consumers[0].clone())
                .or_default()
                .push(support_id);
        } else {
            diagnostics.push(serde_json::json!({
                "code": "SHARED_SUPPORT_NODE",
                "severity": "warning",
                "message": "A data/support node has multiple execution-tree consumers and was not moved.",
                "node": { "id": support_id },
            }));
        }
    }

    for (consumer_id, mut support_ids) in support_by_consumer {
        support_ids.sort();
        let Some(consumer_place) = placed.get(&consumer_id).cloned() else {
            continue;
        };
        for (index, support_id) in support_ids.iter().enumerate() {
            placed
                .entry(support_id.clone())
                .or_insert(BlueprintLayoutPlacedNode {
                    x: consumer_place.x,
                    y: consumer_place.y + request.spacing_y + index as i64 * request.spacing_y,
                    role: "support".to_string(),
                    constraints: vec!["single_consumer_data_support".to_string()],
                });
        }
    }

    for node_id in placed.keys() {
        let Some(node) = nodes.get(node_id) else {
            continue;
        };
        if node
            .pin_layout_sources
            .iter()
            .any(|source| source == "unsupported")
        {
            diagnostics.push(serde_json::json!({
                "code": "PIN_LAYOUT_UNSUPPORTED",
                "severity": "warning",
                "message": "Some pins do not expose layout data; used conservative fallback offsets. Open the Blueprint editor and retry for more accurate anchors.",
                "node": { "id": node_id },
            }));
        } else if node
            .pin_layout_sources
            .iter()
            .any(|source| source == "estimate")
        {
            diagnostics.push(serde_json::json!({
                "code": "PIN_LAYOUT_ESTIMATED",
                "severity": "info",
                "message": "Used estimated pin layout. Open the Blueprint editor window and retry if precise visual alignment is required.",
                "node": { "id": node_id },
            }));
        }
    }

    let mut moves = Vec::new();
    let mut placed_ids = placed.keys().cloned().collect::<Vec<_>>();
    placed_ids.sort();
    for node_id in placed_ids {
        let place = placed
            .get(&node_id)
            .ok_or_else(|| format!("layout node not found in plan: {node_id}"))?;
        let node = nodes
            .get(&node_id)
            .ok_or_else(|| format!("layout node not found in graph: {node_id}"))?;
        if node.x != place.x || node.y != place.y {
            moves.push(BlueprintLayoutMove {
                node_id: node.id.clone(),
                role: place.role.clone(),
                constraints: place.constraints.clone(),
                from_x: node.x,
                from_y: node.y,
                to_x: place.x,
                to_y: place.y,
            });
        }
    }
    diagnostics.sort_by(|a, b| a.to_string().cmp(&b.to_string()));
    diagnostics.dedup_by(|a, b| a == b);

    Ok(BlueprintGraphLayoutPlan {
        root: request.root.clone(),
        resolved_node_count: tree_order.len(),
        support_node_count: placed
            .values()
            .filter(|node| node.role == "support")
            .count(),
        moves,
        diagnostics,
        previous_revision,
        graph_ref,
    })
}

fn json_string_value(value: &serde_json::Value) -> String {
    match value {
        serde_json::Value::Null => String::new(),
        serde_json::Value::Bool(value) => value.to_string(),
        serde_json::Value::Number(value) => value.to_string(),
        serde_json::Value::String(value) => value.clone(),
        other => serde_json::to_string(other).unwrap_or_default(),
    }
}

fn extract_node_token(node: &serde_json::Value) -> Result<serde_json::Value, String> {
    let Some(node_obj) = node.as_object() else {
        return Err("Node reference must be an object.".into());
    };
    if let Some(id) = node_obj.get("id").and_then(|value| value.as_str()) {
        return Ok(serde_json::json!({ "nodeId": id }));
    }
    if let Some(alias) = node_obj.get("alias").and_then(|value| value.as_str()) {
        return Ok(serde_json::json!({ "nodeRef": alias }));
    }
    Err("Node reference requires id or alias.".into())
}

fn extract_pin_endpoint(endpoint: &serde_json::Value) -> Result<serde_json::Value, String> {
    let Some(endpoint_obj) = endpoint.as_object() else {
        return Err("Pin endpoint must be an object.".into());
    };
    let node = endpoint_obj
        .get("node")
        .ok_or_else(|| "Pin endpoint requires node.".to_owned())?;
    let mut result = match extract_node_token(node)? {
        serde_json::Value::Object(object) => object,
        _ => serde_json::Map::new(),
    };
    let pin_name = endpoint_obj
        .get("pin")
        .and_then(|value| value.as_str())
        .or_else(|| endpoint_obj.get("pinName").and_then(|value| value.as_str()))
        .ok_or_else(|| "Pin endpoint requires pin.".to_owned())?;
    result.insert("pin".into(), serde_json::json!(pin_name));
    Ok(serde_json::Value::Object(result))
}

fn compile_add_from_palette_command(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Result<Vec<serde_json::Value>, String> {
    let entry = command
        .get("entry")
        .and_then(|value| value.as_object())
        .ok_or_else(|| "addFromPalette requires entry.".to_owned())?;
    let entry_id = entry
        .get("id")
        .and_then(|value| value.as_str())
        .ok_or_else(|| "addFromPalette requires entry.id.".to_owned())?;
    let mut args = serde_json::Map::new();
    args.insert("entryId".into(), serde_json::json!(entry_id));
    args.insert("entry".into(), serde_json::Value::Object(entry.clone()));
    for field in [
        "position",
        "anchor",
        "from",
        "contextSensitive",
        "context",
        "eventName",
    ] {
        copy_if_present(command, &mut args, field);
    }
    if !args.contains_key("context") {
        if let Some(context) = entry.get("context") {
            args.insert("context".into(), context.clone());
        }
    }
    if let Some(from_pins) = command.get("fromPins") {
        args.insert(
            "fromPins".into(),
            normalize_blueprint_graph_edit_from_pins(from_pins)?,
        );
    }
    if !args.contains_key("contextSensitive") {
        if let Some(context_sensitive) = entry
            .get("contextSensitive")
            .and_then(|value| value.as_bool())
        {
            args.insert(
                "contextSensitive".into(),
                serde_json::json!(context_sensitive),
            );
        }
    }
    let mut op = serde_json::Map::new();
    op.insert("op".into(), serde_json::json!("addFromPalette"));
    if let Some(alias) = command.get("alias").and_then(|value| value.as_str()) {
        op.insert("clientRef".into(), serde_json::json!(alias));
    }
    op.insert("args".into(), serde_json::Value::Object(args));

    let mut ops = vec![serde_json::Value::Object(op)];
    if let Some(defaults) = command.get("defaults").and_then(|value| value.as_array()) {
        if let Some(alias) = command.get("alias").and_then(|value| value.as_str()) {
            for default in defaults {
                let Some(default_obj) = default.as_object() else {
                    continue;
                };
                let Some(pin_name) = default_obj
                    .get("pin")
                    .and_then(|value| value.as_str())
                    .or_else(|| default_obj.get("pinName").and_then(|value| value.as_str()))
                else {
                    continue;
                };
                let mut set_default_args = serde_json::Map::new();
                set_default_args.insert(
                    "target".into(),
                    serde_json::json!({
                        "nodeRef": alias,
                        "pin": pin_name
                    }),
                );
                set_default_args.insert(
                    "value".into(),
                    serde_json::json!(json_string_value(
                        default_obj.get("value").unwrap_or(&serde_json::Value::Null)
                    )),
                );
                ops.push(serde_json::json!({
                    "op": "setPinDefault",
                    "args": set_default_args
                }));
            }
        }
    }
    Ok(ops)
}

fn normalize_blueprint_graph_edit_from_pins(
    value: &serde_json::Value,
) -> Result<serde_json::Value, String> {
    let Some(items) = value.as_array() else {
        return Err("addFromPalette fromPins must be an array.".to_owned());
    };
    let mut normalized = Vec::new();
    for (index, item) in items.iter().enumerate() {
        let Some(object) = item.as_object() else {
            return Err(format!(
                "addFromPalette fromPins[{index}] must be an object."
            ));
        };
        let node_id = object
            .get("node")
            .and_then(|value| value.as_object())
            .and_then(|node| node.get("id"))
            .and_then(|value| value.as_str())
            .filter(|value| !value.is_empty())
            .ok_or_else(|| format!("addFromPalette fromPins[{index}] requires node.id."))?;
        let pin = object
            .get("pin")
            .and_then(|value| value.as_str())
            .filter(|value| !value.is_empty())
            .ok_or_else(|| format!("addFromPalette fromPins[{index}] requires pin."))?;
        normalized.push(serde_json::json!({"nodeId": node_id, "pin": pin}));
    }
    Ok(serde_json::Value::Array(normalized))
}

fn compile_blueprint_graph_commands(
    commands: &[serde_json::Value],
) -> Result<Vec<serde_json::Value>, CallToolResult> {
    let mut ops = Vec::new();
    for (index, command) in commands.iter().enumerate() {
        let Some(command_obj) = command.as_object() else {
            return Err(invalid_argument_result(format!(
                "graph command at index {index} must be an object."
            )));
        };
        let Some(kind) = command_obj.get("kind").and_then(|value| value.as_str()) else {
            return Err(invalid_argument_result(format!(
                "graph command at index {index} requires kind."
            )));
        };

        let compiled = match kind {
            "addFromPalette" => compile_add_from_palette_command(command_obj),
            "removeNode" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "removeNode requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                Ok(vec![serde_json::json!({"op":"removeNode","args": node})])
            }
            "reconstructNode" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "reconstructNode requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let mut args = node.as_object().cloned().unwrap_or_default();
                args.insert(
                    "preserveLinks".into(),
                    serde_json::json!(command_obj
                        .get("preserveLinks")
                        .and_then(|value| value.as_bool())
                        .unwrap_or(true)),
                );
                Ok(vec![
                    serde_json::json!({"op":"reconstructNode","args": args}),
                ])
            }
            "duplicateNode" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "duplicateNode requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let mut args = node.as_object().cloned().unwrap_or_default();
                if let Some(offset) = command_obj
                    .get("offset")
                    .and_then(|value| value.as_object())
                {
                    if let Some(dx) = offset.get("x") {
                        args.insert("dx".into(), dx.clone());
                    }
                    if let Some(dy) = offset.get("y") {
                        args.insert("dy".into(), dy.clone());
                    }
                }
                let mut op = serde_json::Map::new();
                op.insert("op".into(), serde_json::json!("duplicateNode"));
                if let Some(alias) = command_obj.get("alias").and_then(|value| value.as_str()) {
                    op.insert("clientRef".into(), serde_json::json!(alias));
                }
                op.insert("args".into(), serde_json::Value::Object(args));
                Ok(vec![serde_json::Value::Object(op)])
            }
            "moveNode" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "moveNode requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let mut args = node.as_object().cloned().unwrap_or_default();
                let op_name = if let Some(position) = command_obj
                    .get("position")
                    .and_then(|value| value.as_object())
                {
                    if let Some(x) = position.get("x") {
                        args.insert("x".into(), x.clone());
                    }
                    if let Some(y) = position.get("y") {
                        args.insert("y".into(), y.clone());
                    }
                    "moveNode"
                } else if let Some(delta) =
                    command_obj.get("delta").and_then(|value| value.as_object())
                {
                    if let Some(dx) = delta.get("x") {
                        args.insert("dx".into(), dx.clone());
                    }
                    if let Some(dy) = delta.get("y") {
                        args.insert("dy".into(), dy.clone());
                    }
                    "moveNodeBy"
                } else {
                    return Err(invalid_argument_result(
                        "moveNode requires position or delta.",
                    ));
                };
                Ok(vec![serde_json::json!({"op": op_name, "args": args})])
            }
            "connect" | "disconnect" => {
                let from = command_obj
                    .get("from")
                    .ok_or_else(|| format!("{kind} requires from."))
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let to = command_obj
                    .get("to")
                    .ok_or_else(|| format!("{kind} requires to."))
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                Ok(vec![serde_json::json!({
                    "op": if kind == "connect" { "connectPins" } else { "disconnectPins" },
                    "args": {
                        "from": from,
                        "to": to
                    }
                })])
            }
            "insertExec" => {
                let from = command_obj
                    .get("from")
                    .ok_or_else(|| "insertExec requires from.".to_owned())
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let to = command_obj
                    .get("to")
                    .ok_or_else(|| "insertExec requires to.".to_owned())
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "insertExec requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let input_pin = command_obj
                    .get("inputPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("execute");
                let output_pin = command_obj
                    .get("outputPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("then");
                Ok(vec![serde_json::json!({
                    "op": "insertExec",
                    "args": {
                        "from": from,
                        "to": to,
                        "node": node,
                        "inputPin": input_pin,
                        "outputPin": output_pin
                    }
                })])
            }
            "bypassExec" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "bypassExec requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let input_pin = command_obj
                    .get("inputPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("execute");
                let output_pin = command_obj
                    .get("outputPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("then");
                Ok(vec![serde_json::json!({
                    "op": "bypassExec",
                    "args": {
                        "node": node,
                        "inputPin": input_pin,
                        "outputPin": output_pin
                    }
                })])
            }
            "breakLinks" => {
                let target = command_obj
                    .get("target")
                    .ok_or_else(|| "breakLinks requires target.".to_owned())
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                Ok(vec![
                    serde_json::json!({"op":"breakPinLinks","args":{"target": target}}),
                ])
            }
            "setPinDefault" => {
                let target = command_obj
                    .get("target")
                    .ok_or_else(|| "setPinDefault requires target.".to_owned())
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let value = command_obj
                    .get("value")
                    .ok_or_else(|| invalid_argument_result("setPinDefault requires value."))?;
                let mut args = serde_json::Map::new();
                args.insert("target".into(), target);
                match value {
                    serde_json::Value::Object(value_obj) => {
                        if let Some(default_value) = value_obj.get("value") {
                            args.insert(
                                "value".into(),
                                serde_json::json!(json_string_value(default_value)),
                            );
                        }
                        if let Some(object) = value_obj
                            .get("object")
                            .or_else(|| value_obj.get("defaultObject"))
                            .or_else(|| value_obj.get("defaultObjectPath"))
                            .or_else(|| value_obj.get("assetPath"))
                            .or_else(|| value_obj.get("classPath"))
                        {
                            args.insert(
                                "object".into(),
                                serde_json::json!(json_string_value(object)),
                            );
                        }
                        if let Some(text) = value_obj.get("text") {
                            args.insert("text".into(), serde_json::json!(json_string_value(text)));
                        }
                        if !args.contains_key("value")
                            && !args.contains_key("object")
                            && !args.contains_key("text")
                        {
                            args.insert(
                                "value".into(),
                                serde_json::json!(json_string_value(value)),
                            );
                        }
                    }
                    _ => {
                        args.insert("value".into(), serde_json::json!(json_string_value(value)));
                    }
                }
                Ok(vec![serde_json::json!({
                    "op":"setPinDefault",
                    "args": args
                })])
            }
            "setNodeComment" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "setNodeComment requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let comment = command_obj
                    .get("comment")
                    .and_then(|value| value.as_str())
                    .unwrap_or_default();
                let mut args = node.as_object().cloned().unwrap_or_default();
                args.insert("comment".into(), serde_json::json!(comment));
                Ok(vec![
                    serde_json::json!({"op":"setNodeComment","args": args}),
                ])
            }
            "setNodeEnabled" => {
                let node = command_obj
                    .get("node")
                    .ok_or_else(|| "setNodeEnabled requires node.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let enabled = command_obj
                    .get("enabled")
                    .and_then(|value| value.as_bool())
                    .unwrap_or(true);
                let mut args = node.as_object().cloned().unwrap_or_default();
                args.insert("enabled".into(), serde_json::json!(enabled));
                Ok(vec![
                    serde_json::json!({"op":"setNodeEnabled","args": args}),
                ])
            }
            "addReroute" => {
                let mut args = serde_json::Map::new();
                if let Some(position) = command_obj.get("position") {
                    args.insert("position".into(), position.clone());
                }
                let mut op = serde_json::Map::new();
                op.insert("op".into(), serde_json::json!("addReroute"));
                if let Some(alias) = command_obj.get("alias").and_then(|value| value.as_str()) {
                    op.insert("clientRef".into(), serde_json::json!(alias));
                }
                op.insert("args".into(), serde_json::Value::Object(args));
                Ok(vec![serde_json::Value::Object(op)])
            }
            "addCommentBox" => {
                let mut args = serde_json::Map::new();
                if let Some(bounds) = command_obj
                    .get("bounds")
                    .and_then(|value| value.as_object())
                {
                    args.insert(
                        "position".into(),
                        serde_json::json!({
                            "x": bounds.get("x").cloned().unwrap_or(serde_json::json!(0)),
                            "y": bounds.get("y").cloned().unwrap_or(serde_json::json!(0))
                        }),
                    );
                    if let Some(width) = bounds.get("w") {
                        args.insert("width".into(), width.clone());
                    }
                    if let Some(height) = bounds.get("h") {
                        args.insert("height".into(), height.clone());
                    }
                }
                if let Some(text) = command_obj.get("text").and_then(|value| value.as_str()) {
                    args.insert("text".into(), serde_json::json!(text));
                }
                let mut op = serde_json::Map::new();
                op.insert("op".into(), serde_json::json!("addCommentBox"));
                if let Some(alias) = command_obj.get("alias").and_then(|value| value.as_str()) {
                    op.insert("clientRef".into(), serde_json::json!(alias));
                }
                op.insert("args".into(), serde_json::Value::Object(args));
                Ok(vec![serde_json::Value::Object(op)])
            }
            other => Err(format!("Unsupported graph.edit command kind: {other}")),
        }
        .map_err(invalid_argument_result)?;
        ops.extend(compiled);
    }
    Ok(ops)
}

fn translate_blueprint_graph_edit_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    if args.contains_key("graphName") || args.contains_key("graphRef") {
        return Err(invalid_argument_result(
            "blueprint.graph.edit uses graph:{id|name}; graphName and graphRef are not public inputs.",
        ));
    }
    if !args.contains_key("graph") {
        return Err(invalid_argument_result(
            "blueprint.graph.edit requires graph:{id|name}.",
        ));
    }
    let asset_path = read_required_asset_path(args, "blueprint.graph.edit")?;
    let graph_address =
        read_optional_graph_address(args, &asset_path, true, "blueprint.graph.edit")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    write_optional_graph_address(&mut translated, graph_address);
    for field in ["expectedRevision", "idempotencyKey", "dryRun"] {
        copy_if_present(args, &mut translated, field);
    }
    let commands = args
        .get("commands")
        .and_then(|value| value.as_array())
        .ok_or_else(|| invalid_argument_result("blueprint.graph.edit requires commands."))?;
    translated.insert(
        "ops".into(),
        serde_json::Value::Array(compile_blueprint_graph_commands(commands)?),
    );
    Ok(translated)
}

fn augment_blueprint_mutate_result(payload: serde_json::Value) -> serde_json::Value {
    let Some(mut object) = payload.as_object().cloned() else {
        return payload;
    };
    if let Some(op_results) = object.get("opResults").cloned() {
        object.entry("commandResults").or_insert(op_results);
    }
    serde_json::Value::Object(object)
}

#[allow(dead_code)]
fn pre_attach_tools() -> Vec<Tool> {
    use std::sync::Arc;
    vec![
        Tool::new(
            "status",
            "Return the current Loomle MCP, project, and runtime status.",
            Arc::new(empty_schema()),
        ),
        Tool::new(
            "project_list",
            "List Unreal Engine projects known to LOOMLE. Defaults to online projects.",
            Arc::new(project_list_schema()),
        ),
        Tool::new(
            "project_attach",
            "Attach this MCP session to an online LOOMLE-enabled Unreal Engine project.",
            Arc::new(project_attach_schema()),
        ),
        Tool::new(
            "project_install",
            "Install or update LOOMLE support for an Unreal Engine project, including the LoomleBridge plugin and required project settings.",
            Arc::new(project_install_schema()),
        ),
        Tool::new(
            "schema_inspect",
            "Inspect documented second-level Loomle tool schemas, such as Blueprint graph edit command schemas.",
            Arc::new(schema_inspect_schema()),
        ),
    ]
}

fn all_declared_tools() -> Vec<Tool> {
    manifest_declared_tools_for("native").expect("valid embedded LOOMLE tool manifest")
}

const TOOL_MANIFEST_JSON: &str = include_str!("../../mcp/manifest/manifest.json");

fn manifest_declared_tools_for(target: &str) -> Result<Vec<Tool>, String> {
    let manifest: serde_json::Value = serde_json::from_str(TOOL_MANIFEST_JSON)
        .map_err(|error| format!("manifest JSON parse failed: {error}"))?;
    if manifest.get("product").and_then(|value| value.as_str()) != Some("loomle") {
        return Err("manifest product must be loomle".to_string());
    }
    let tools = manifest
        .get("tools")
        .and_then(|value| value.as_array())
        .ok_or_else(|| "manifest.tools must be an array".to_string())?;
    let mut declared = Vec::new();
    for tool in tools {
        let availability = tool
            .get("availability")
            .and_then(|value| value.as_array())
            .ok_or_else(|| "tool availability must be an array".to_string())?;
        if !availability
            .iter()
            .any(|value| value.as_str() == Some(target))
        {
            continue;
        }
        let name = tool
            .get("name")
            .and_then(|value| value.as_str())
            .ok_or_else(|| "tool name must be a string".to_string())?;
        let description = tool
            .get("description")
            .and_then(|value| value.as_str())
            .ok_or_else(|| format!("{name}.description must be a string"))?;
        let input_schema = tool
            .get("inputSchema")
            .and_then(|value| value.as_object())
            .cloned()
            .ok_or_else(|| format!("{name}.inputSchema must be an object"))?;
        let listed_input_schema = tools_list_input_schema(name, &input_schema);
        let mut declared_tool = Tool::new(
            name.to_string(),
            description.to_string(),
            Arc::new(listed_input_schema),
        );
        if let Some(title) = tool.get("title").and_then(|value| value.as_str()) {
            declared_tool.title = Some(title.to_string());
        }
        declared.push(declared_tool);
    }
    Ok(declared)
}

fn tools_list_input_schema(
    name: &str,
    full_schema: &rmcp::model::JsonObject,
) -> rmcp::model::JsonObject {
    if name == "schema_inspect" {
        return full_schema.clone();
    }

    let mut schema = rmcp::model::JsonObject::new();
    schema.insert("type".to_string(), serde_json::json!("object"));
    if let Some(required) = full_schema
        .get("required")
        .and_then(|value| value.as_array())
    {
        schema.insert(
            "required".to_string(),
            serde_json::Value::Array(required.clone()),
        );
    }

    let Some(properties) = full_schema
        .get("properties")
        .and_then(|value| value.as_object())
    else {
        return schema;
    };
    let required_names = full_schema
        .get("required")
        .and_then(|value| value.as_array())
        .map(|items| {
            items
                .iter()
                .filter_map(|value| value.as_str())
                .collect::<std::collections::HashSet<_>>()
        })
        .unwrap_or_default();
    let mut thin_properties = serde_json::Map::new();
    for (property_name, property_schema) in properties {
        if required_names.contains(property_name.as_str())
            || tools_list_high_signal_optional(property_name)
        {
            thin_properties.insert(
                property_name.clone(),
                tools_list_thin_property(property_schema),
            );
        }
    }
    if !thin_properties.is_empty() || full_schema.contains_key("properties") {
        schema.insert(
            "properties".to_string(),
            serde_json::Value::Object(thin_properties),
        );
    }
    schema
}

fn tools_list_high_signal_optional(property_name: &str) -> bool {
    matches!(
        property_name,
        "kind"
            | "view"
            | "operation"
            | "memberKind"
            | "action"
            | "fn"
            | "tool"
            | "domain"
            | "dryRun"
            | "expectedRevision"
    )
}

fn tools_list_thin_property(property_schema: &serde_json::Value) -> serde_json::Value {
    let Some(object) = property_schema.as_object() else {
        return serde_json::json!({});
    };
    let mut thin = serde_json::Map::new();
    for field in ["type", "enum", "const", "minLength", "default"] {
        if let Some(value) = object.get(field) {
            thin.insert(field.to_string(), value.clone());
        }
    }
    if let Some(description) = object.get("description").and_then(|value| value.as_str()) {
        thin.insert(
            "description".to_string(),
            serde_json::json!(tools_list_short_description(description)),
        );
    }
    if thin.is_empty() {
        thin.insert("type".to_string(), serde_json::json!("object"));
    }
    serde_json::Value::Object(thin)
}

fn tools_list_short_description(description: &str) -> String {
    const LIMIT: usize = 120;
    if description.len() <= LIMIT {
        return description.to_string();
    }
    let mut end = LIMIT;
    while !description.is_char_boundary(end) {
        end -= 1;
    }
    format!("{}...", description[..end].trim_end())
}

fn runtime_declared_tools() -> Vec<Tool> {
    all_declared_tools()
}

fn bridge_unavailable_result(reason: &str) -> CallToolResult {
    CallToolResult::error(vec![rmcp::model::Content::text(format!(
        "LOOMLE Bridge unavailable: {reason}"
    ))])
}

fn is_local_tool(name: &str) -> bool {
    matches!(
        name,
        "status" | "project.list" | "project.attach" | "project.install" | "schema.inspect"
    )
}

fn public_tool_name_to_internal(name: &str) -> String {
    match name {
        "project_list" => "project.list",
        "project_attach" => "project.attach",
        "project_install" => "project.install",
        "schema_inspect" => "schema.inspect",
        "asset_create" => "asset.create",
        "asset_inspect" => "asset.inspect",
        "asset_edit" => "asset.edit",
        "editor_open" => "editor.open",
        "editor_focus" => "editor.focus",
        "editor_screenshot" => "editor.screenshot",
        "blueprint_graph_list" => "blueprint.graph.list",
        "blueprint_inspect" => "blueprint.inspect",
        "blueprint_class_inspect" => "blueprint.class.inspect",
        "blueprint_class_edit" => "blueprint.class.edit",
        "blueprint_member_inspect" => "blueprint.member.inspect",
        "blueprint_member_edit" => "blueprint.member.edit",
        "blueprint_graph_inspect" => "blueprint.graph.inspect",
        "blueprint_node_inspect" => "blueprint.node.inspect",
        "blueprint_graph_layout" => "blueprint.graph.layout",
        "blueprint_node_edit" => "blueprint.node.edit",
        "blueprint_graph_palette" => "blueprint.graph.palette",
        "blueprint_compile" => "blueprint.compile",
        "blueprint_graph_edit" => "blueprint.graph.edit",
        "material_list" => "material.list",
        "material_graph_inspect" => "material.graph.inspect",
        "material_graph_edit" => "material.graph.edit",
        "material_graph_layout" => "material.graph.layout",
        "material_compile" => "material.compile",
        "material_node_inspect" => "material.node.inspect",
        "material_node_edit" => "material.node.edit",
        "material_palette" => "material.palette",
        "pcg_palette" => "pcg.palette",
        "pcg_graph_inspect" => "pcg.graph.inspect",
        "pcg_node_inspect" => "pcg.node.inspect",
        "pcg_parameter_inspect" => "pcg.parameter.inspect",
        "pcg_parameter_edit" => "pcg.parameter.edit",
        "pcg_compile" => "pcg.compile",
        "pcg_graph_edit" => "pcg.graph.edit",
        "pcg_graph_layout" => "pcg.graph.layout",
        "widget_palette" => "widget.palette",
        "diagnostic_tail" => "diagnostic.tail",
        "log_tail" => "log.tail",
        "widget_tree_inspect" => "widget.tree.inspect",
        "widget_tree_edit" => "widget.tree.edit",
        "widget_edit" => "widget.edit",
        "widget_event_create" => "widget.event.create",
        "widget_inspect" => "widget.inspect",
        "widget_compile" => "widget.compile",
        _ => name,
    }
    .to_string()
}

fn runtime_invoke_failure_payload(error: loomle::RpcInvokeFailure) -> serde_json::Value {
    if let Some(payload) = error.payload {
        payload
    } else {
        serde_json::json!({
            "code": error.code,
            "message": error.message,
            "retryable": error.retryable,
            "detail": error.detail,
        })
    }
}

fn empty_schema() -> rmcp::model::JsonObject {
    serde_json::from_str(r#"{"type":"object","properties":{},"additionalProperties":false}"#)
        .expect("valid schema")
}

fn project_list_schema() -> rmcp::model::JsonObject {
    serde_json::from_str(
        r#"{
          "type":"object",
          "properties":{
            "status":{"type":"string","enum":["online","offline","all"],"default":"online"},
            "includeDiagnostics":{"type":"boolean","default":false}
          },
          "additionalProperties":false
        }"#,
    )
    .expect("valid schema")
}

fn project_attach_schema() -> rmcp::model::JsonObject {
    serde_json::from_str(
        r#"{
          "type":"object",
          "properties":{
            "projectId":{"type":"string","minLength":1},
            "projectRoot":{"type":"string","minLength":1}
          },
          "additionalProperties":false
        }"#,
    )
    .expect("valid schema")
}

fn project_install_schema() -> rmcp::model::JsonObject {
    serde_json::from_str(
        r#"{
          "type":"object",
          "properties":{
            "projectRoot":{"type":"string","minLength":1},
            "force":{"type":"boolean","default":false}
          },
          "required":["projectRoot"],
          "additionalProperties":false
        }"#,
    )
    .expect("valid schema")
}

fn schema_from_value(value: serde_json::Value) -> rmcp::model::JsonObject {
    serde_json::from_value(value).expect("valid schema")
}

fn asset_path_only_schema(description: &str) -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": { "type": "string", "minLength": 1, "description": description }
        },
        "required": ["assetPath"],
        "additionalProperties": false
    }))
}

fn context_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{},
        "additionalProperties": false
    }))
}

fn execute_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "language":{"type":"string","enum":["python"]},
            "mode":{"type":"string","enum":["exec","eval"]},
            "code":{"type":"string","minLength":1},
            "execution":{
                "type":"object",
                "properties":{
                    "mode":{"type":"string","enum":["sync","job"]},
                    "idempotencyKey":{"type":"string"},
                    "label":{"type":"string"},
                    "waitMs":{"type":"integer"},
                    "resultTtlMs":{"type":"integer"}
                },
                "additionalProperties": false
            }
        },
        "required":["language","mode","code"],
        "additionalProperties": false
    }))
}

fn jobs_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "action":{"type":"string"},
            "jobId":{"type":"string"},
            "tool":{"type":"string"},
            "status":{"type":"string"},
            "limit":{"type":"integer"}
        },
        "additionalProperties": false
    }))
}

fn profiling_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "action":{"type":"string"},
            "world":{"type":"string"},
            "target":{
                "type":"object",
                "properties":{
                    "sessionId":{"type":"string"},
                    "participant":{"type":"string"},
                    "role":{"type":"string","enum":["server","client","editor","standalone"]},
                    "index":{"type":"integer"}
                },
                "additionalProperties": false
            },
            "group":{"type":"string"},
            "capturePath":{"type":"string"}
        },
        "additionalProperties": true
    }))
}

fn play_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "action":{"type":"string","enum":["status","start","stop","wait"]},
            "backend":{"type":"string","enum":["pie"]},
            "sessionId":{"type":"string"},
            "map":{"type":"string"},
            "ifActive":{"type":"string","enum":["error","returnStatus"]},
            "timeoutMs":{"type":"integer"},
            "participant":{"type":"string"},
            "role":{"type":"string","enum":["server","client","editor","standalone"]},
            "count":{"type":"integer"},
            "layout":{
                "type":"object",
                "properties":{
                    "preset":{"type":"string","enum":["horizontal","vertical"]},
                    "originX":{"type":"integer"},
                    "originY":{"type":"integer"},
                    "width":{"type":"integer"},
                    "height":{"type":"integer"},
                    "gap":{"type":"integer"}
                },
                "additionalProperties": false
            },
            "strict":{
                "type":"object",
                "properties":{
                    "window":{"type":"boolean"}
                },
                "additionalProperties": false
            },
            "until":{
                "type":"object",
                "properties":{
                    "session":{"type":"string","enum":["inactive","starting","ready","stopping"]},
                    "participants":{
                        "type":"array",
                        "items":{
                            "type":"object",
                            "properties":{
                                "participant":{"type":"string"},
                                "role":{"type":"string","enum":["server","client","editor","standalone"]},
                                "count":{"type":"integer"},
                                "state":{"type":"string","enum":["ready"]}
                            },
                            "additionalProperties": false
                        }
                    }
                },
                "additionalProperties": false
            },
            "defaultClientWindow":{
                "type":"object",
                "properties":{
                    "width":{"type":"integer"},
                    "height":{"type":"integer"},
                    "x":{"type":"integer"},
                    "y":{"type":"integer"}
                },
                "additionalProperties": false
            },
            "topology":{
                "type":"object",
                "properties":{
                    "server":{
                        "type":"object",
                        "properties":{
                            "kind":{"type":"string","enum":["standalone","listen","dedicated"]},
                            "launchArgs":{"type":"string"}
                        },
                        "additionalProperties": false
                    },
                    "clientCount":{"type":"integer"},
                    "clients":{
                        "type":"array",
                        "items":{
                            "type":"object",
                            "properties":{
                                "index":{"type":"integer"},
                                "window":{
                                    "type":"object",
                                    "properties":{
                                        "width":{"type":"integer"},
                                        "height":{"type":"integer"},
                                        "x":{"type":"integer"},
                                        "y":{"type":"integer"}
                                    },
                                    "additionalProperties": false
                                }
                            },
                            "additionalProperties": false
                        }
                    }
                },
                "additionalProperties": false
            }
        },
        "required":["action"],
        "additionalProperties": false
    }))
}

fn editor_open_schema() -> rmcp::model::JsonObject {
    asset_path_only_schema("Asset path to open in the Unreal Editor.")
}

fn editor_focus_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "panel":{"type":"string","minLength":1}
        },
        "required":["assetPath","panel"],
        "additionalProperties": false
    }))
}

fn editor_screenshot_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "path":{"type":"string"}
        },
        "additionalProperties": false
    }))
}

fn asset_create_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "kind".into(),
        serde_json::json!({
            "type":"string",
            "enum":["blueprint","enum","userDefinedStruct","material","materialFunction","pcgGraph","widgetBlueprint"],
            "description":"Asset category to create."
        }),
    );
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "parentClassPath".into(),
        serde_json::json!({"type":"string","minLength":1,"description":"Blueprint parent class path. Defaults to /Script/Engine.Actor for kind=blueprint. WidgetBlueprint currently defaults to /Script/UMG.UserWidget."}),
    );
    properties.insert(
        "parentClass".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "entries".into(),
        serde_json::json!({"type":"array","description":"Enum entries for kind=enum.","items":{"oneOf":[{"type":"string"},{"type":"object"}]}}),
    );
    properties.insert(
        "displayNames".into(),
        serde_json::json!({"type":"object","description":"Optional enum display names keyed by entry name."}),
    );
    properties.insert(
        "fields".into(),
        serde_json::json!({"type":"array","description":"UserDefinedStruct fields for kind=userDefinedStruct. First version requires at least one field.","items":{"type":"object"}}),
    );
    properties.insert(
        "tooltip".into(),
        serde_json::json!({"type":"string","description":"UserDefinedStruct tooltip for kind=userDefinedStruct."}),
    );
    properties.insert("args".into(), serde_json::json!({"type":"object"}));
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["kind","assetPath"],
        "additionalProperties": false
    }))
}

fn asset_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "kind":{
                "type":"string",
                "enum":["blueprint","enum","userDefinedStruct","material","materialFunction","pcgGraph","widgetBlueprint"],
                "description":"Asset category to inspect. Use userDefinedStruct for UUserDefinedStruct, material for UMaterial, materialFunction for UMaterialFunction, pcgGraph for UPCGGraph, and widgetBlueprint for UWidgetBlueprint."
            },
            "assetPath":{"type":"string","minLength":1},
            "view":{
                "type":"string",
                "enum":["overview","pins","links","defaults","full","outline","layout","details"],
                "description":"PCG graph view when kind=pcgGraph; WidgetTree view when kind=widgetBlueprint."
            },
            "filter":{
                "type":"object",
                "description":"PCG graph filter when kind=pcgGraph; WidgetTree filter when kind=widgetBlueprint.",
                "properties":{
                    "nodeIds":{"type":"array","items":{"type":"string"}},
                    "names":{"type":"array","items":{"type":"string"}},
                    "text":{"type":"string","minLength":1}
                },
                "additionalProperties": false
            },
            "page":{
                "type":"object",
                "description":"PCG graph pagination when kind=pcgGraph.",
                "properties":{
                    "limit":{"type":"integer","minimum":1,"maximum":1000,"default":50},
                    "cursor":{"type":"string"}
                },
                "additionalProperties": false
            },
            "includeConnections":{"type":"boolean","default":false,"description":"Material graph connection detail when kind=material or materialFunction."},
            "nodeIds":{"type":"array","items":{"type":"string"},"description":"Optional Material expression ids when kind=material or materialFunction."}
        },
        "required":["kind","assetPath"],
        "additionalProperties": false
    }))
}

fn asset_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "kind".into(),
        serde_json::json!({
            "type":"string",
            "enum":["blueprint","enum","userDefinedStruct","material","materialFunction","pcgGraph","widgetBlueprint"],
            "description":"Optional for operation=updateMetadata. Required for enum updateEntries and userDefinedStruct field operations."
        }),
    );
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "operation".into(),
        serde_json::json!({
            "type":"string",
            "enum":["updateMetadata","updateEntries","setTooltip","addField","removeField","renameField","changeFieldType","setFieldDefault","setFieldTooltip","setFieldMetadata","moveField"],
            "description":"Use updateMetadata for generic asset metadata. updateEntries is an enum-only compatibility special case. Field operations apply to kind=userDefinedStruct."
        }),
    );
    properties.insert(
        "metadata".into(),
        serde_json::json!({
            "type":"object",
            "description":"Metadata key/value pairs to set on the asset object. Values are stored as strings.",
            "additionalProperties":{"type":"string"}
        }),
    );
    properties.insert(
        "removeKeys".into(),
        serde_json::json!({
            "type":"array",
            "description":"Metadata keys to remove from the asset object.",
            "items":{"type":"string","minLength":1}
        }),
    );
    properties.insert(
        "clearMetadata".into(),
        serde_json::json!({
            "type":"boolean",
            "default":false,
            "description":"Remove all current metadata keys from the asset object before applying metadata."
        }),
    );
    properties.insert(
        "entries".into(),
        serde_json::json!({"type":"array","description":"Enum entries for enum-only operation=updateEntries.","items":{"oneOf":[{"type":"string"},{"type":"object"}]}}),
    );
    properties.insert("displayNames".into(), serde_json::json!({"type":"object"}));
    properties.insert("fields".into(), serde_json::json!({"type":"array","items":{"type":"object"},"description":"UserDefinedStruct fields for create-like args forwarding."}));
    properties.insert(
        "args".into(),
        serde_json::json!({
            "type":"object",
            "description":"Optional envelope for operation-specific fields. For updateMetadata it may contain metadata, removeKeys, or clearMetadata."
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","operation"],
        "additionalProperties": false
    }))
}

fn blueprint_list_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "includeCompositeSubgraphs":{"type":"boolean","default":false}
        },
        "required":["assetPath"],
        "additionalProperties": false
    }))
}

fn graph_ref_schema() -> serde_json::Value {
    serde_json::json!({
        "type":"object",
        "description":"Recommended graph address. Prefer {\"id\":\"...\"} when available from blueprint_graph_list or blueprint_graph_inspect; use {\"name\":\"EventGraph\"} when id is not available.",
        "properties":{
            "id":{"type":"string","minLength":1},
            "name":{"type":"string","minLength":1}
        },
        "additionalProperties": false,
        "anyOf":[
            {"required":["id"]},
            {"required":["name"]}
        ]
    })
}

fn mutation_control_fields(properties: &mut serde_json::Map<String, serde_json::Value>) {
    properties.insert(
        "dryRun".into(),
        serde_json::json!({"type":"boolean","default":false}),
    );
    properties.insert(
        "returnDiff".into(),
        serde_json::json!({"type":"boolean","default":false}),
    );
    properties.insert(
        "returnDiagnostics".into(),
        serde_json::json!({"type":"boolean","default":true}),
    );
    properties.insert(
        "expectedRevision".into(),
        serde_json::json!({"type":"string"}),
    );
}

fn execution_control_fields(properties: &mut serde_json::Map<String, serde_json::Value>) {
    properties.insert(
        "dryRun".into(),
        serde_json::json!({"type":"boolean","default":false}),
    );
    properties.insert(
        "expectedRevision".into(),
        serde_json::json!({"type":"string"}),
    );
}

fn blueprint_inspect_schema() -> rmcp::model::JsonObject {
    asset_path_only_schema("Blueprint asset path.")
}

fn blueprint_class_inspect_schema() -> rmcp::model::JsonObject {
    asset_path_only_schema("Blueprint asset path.")
}

fn blueprint_class_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "operation".into(),
        serde_json::json!({"type":"string","enum":["setParent","setSettings","setDefault","addInterface","removeInterface"]}),
    );
    properties.insert(
        "args".into(),
        serde_json::json!({
            "type":"object",
            "properties":{
                "parentClassPath":{
                    "type":"string",
                    "description":"Required for setParent. UE class path for the new Blueprint parent class."
                },
                "interfaceClassPath":{
                    "type":"string",
                    "description":"Required for addInterface and removeInterface. UE generated interface class path."
                },
                "preserveFunctions":{
                    "type":"boolean",
                    "default":false,
                    "description":"removeInterface only. Preserve interface function graphs when removing the interface contract."
                },
                "settings":{
                    "type":"object",
                    "description":"Required for setSettings. Editable Blueprint Class Settings.",
                    "properties":{
                        "displayName":{"type":"string"},
                        "description":{"type":"string"},
                        "namespace":{"type":"string"},
                        "category":{"type":"string"},
                        "hideCategories":{"type":"array","items":{"type":"string"}},
                        "runConstructionScriptOnDrag":{"type":"boolean"},
                        "runConstructionScriptInSequencer":{"type":"boolean"},
                        "generateConstClass":{"type":"boolean"},
                        "generateAbstractClass":{"type":"boolean"},
                        "deprecated":{"type":"boolean"},
                        "shouldCookPropertyGuids":{"type":"string"},
                        "compileMode":{"type":"string"}
                    },
                    "additionalProperties":false
                },
                "property":{
                    "type":"string",
                    "description":"Required for setDefault. Editable generated-class CDO property name."
                },
                "value":{
                    "description":"Required for setDefault. UE import-text value, or a JSON string/number/boolean converted to import text.",
                    "oneOf":[{"type":"string"},{"type":"number"},{"type":"boolean"}]
                }
            },
            "additionalProperties":false
        }),
    );
    execution_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","operation"],
        "additionalProperties": false
    }))
}

fn blueprint_member_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "memberKind":{"type":"string","enum":["variable","function","macro","dispatcher","event","customEvent","component"]},
            "name":{"type":"string","minLength":1}
        },
        "required":["assetPath","memberKind"],
        "additionalProperties": false
    }))
}

fn blueprint_member_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "memberKind".into(),
        serde_json::json!({
            "type":"string",
            "enum":["variable","function","macro","dispatcher","event","component"],
            "description":"Blueprint member domain. Use schema_inspect with domain='blueprint' and tool='blueprint_member_edit' to list supported memberKind.operation entries."
        }),
    );
    properties.insert(
        "operation".into(),
        serde_json::json!({
            "type":"string",
            "minLength":1,
            "description":"Operation within memberKind. Use schema_inspect with domain='blueprint', tool='blueprint_member_edit', and operation='<memberKind>.<operation>' for the operation-specific request schema."
        }),
    );
    properties.insert(
        "args".into(),
        serde_json::json!({
            "type":"object",
            "description":"Operation-specific arguments. The shape is intentionally omitted from tools/list; call schema_inspect for the selected memberKind.operation."
        }),
    );
    properties.insert(
        "dryRun".into(),
        serde_json::json!({"type":"boolean","default":false}),
    );
    properties.insert(
        "expectedRevision".into(),
        serde_json::json!({"type":"string"}),
    );
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","memberKind","operation"],
        "additionalProperties": false
    }))
}

fn blueprint_graph_list_schema() -> rmcp::model::JsonObject {
    blueprint_list_schema()
}

fn blueprint_graph_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "graph": graph_ref_schema(),
            "view":{"type":"string","enum":["summary","exec_flow","data_flow"],"default":"summary","description":"Task-oriented result view. summary returns graph boundary, entry/root refs, chain summaries, and a de-duplicated nodes dictionary. exec_flow traces execution links from rootNode.id and returns lightweight nodes[] plus links[]. data_flow traces data links from rootPin.node.id/rootPin.pin with the same nodes[] plus links[] shape. Use blueprint_node_inspect for exact pins, defaults, and wiring preparation."},
            "rootNode":{
                "type":"object",
                "properties":{
                    "id":{"type":"string","minLength":1}
                },
                "required":["id"],
                "additionalProperties": false
            },
            "rootPin":{
                "type":"object",
                "properties":{
                    "node":{
                        "type":"object",
                        "properties":{"id":{"type":"string","minLength":1}},
                        "required":["id"],
                        "additionalProperties": false
                    },
                    "pin":{"type":"string","minLength":1}
                },
                "required":["node","pin"],
                "additionalProperties": false
            },
            "traversal":{
                "type":"object",
                "properties":{
                    "direction":{"type":"string","enum":["upstream","downstream","both"],"description":"exec_flow defaults to downstream; data_flow defaults to upstream."},
                    "maxDepth":{"type":"integer","minimum":1,"maximum":128,"default":64},
                    "maxNodes":{"type":"integer","minimum":1,"maximum":1000,"default":250}
                },
                "additionalProperties": false
            }
        },
        "required":["assetPath","graph"],
        "additionalProperties": false
    }))
}

fn blueprint_node_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "graph": graph_ref_schema(),
            "node":{
                "type":"object",
                "description":"Blueprint graph node reference from blueprint_graph_inspect. Use this when a graph node has hasNodeEditCapabilities=true or when full pin/default details are needed for one node.",
                "properties":{
                    "id":{"type":"string","minLength":1}
                },
                "required":["id"],
                "additionalProperties": false
            }
        },
        "required":["assetPath","graph","node"],
        "additionalProperties": false
    }))
}

fn blueprint_node_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), graph_ref_schema());
    properties.insert(
        "node".into(),
        serde_json::json!({
            "type":"object",
            "description":"Blueprint graph node reference. Inspect the node first with blueprint_node_inspect to confirm editCapabilities and current pin names.",
            "properties":{"id":{"type":"string","minLength":1}},
            "required":["id"],
            "additionalProperties": false
        }),
    );
    properties.insert(
        "operation".into(),
        serde_json::json!({
            "type":"string",
            "enum":["addPin","removePin","insertPin","renamePin","movePin","restorePins","setDelegateFunction"],
            "description":"Node-local structural edit. Use schema_inspect with domain='blueprint', tool='blueprint_node_edit', and operation='<operation>' for operation-specific args."
        }),
    );
    properties.insert(
        "args".into(),
        serde_json::json!({
            "type":"object",
            "description":"Operation-specific arguments. The shape is intentionally omitted from tools/list; call schema_inspect for the selected operation."
        }),
    );
    properties.insert(
        "dryRun".into(),
        serde_json::json!({"type":"boolean","default":false}),
    );
    properties.insert(
        "expectedRevision".into(),
        serde_json::json!({"type":"string"}),
    );
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","graph","node","operation","args"],
        "additionalProperties": false
    }))
}

fn pcg_graph_ref_schema() -> serde_json::Value {
    serde_json::json!({
        "type":"object",
        "description":"Optional PCG graph reference. For PCG this resolves to graph.assetPath.",
        "properties":{
            "kind":{"type":"string","enum":["asset"]},
            "assetPath":{"type":"string","minLength":1}
        },
        "required":["assetPath"],
        "additionalProperties": true
    })
}

fn pcg_graph_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "graph": pcg_graph_ref_schema(),
            "view":{"type":"string","enum":["overview","pins","links","defaults","full"],"default":"overview","description":"Task-oriented PCG graph view. overview omits pins and edges; pins adds pin signatures; links adds pins plus edge/link refs; defaults adds pin defaults and link refs; full preserves the legacy full node payload."},
            "filter":{
                "type":"object",
                "properties":{
                    "nodeIds":{"type":"array","items":{"type":"string"}},
                    "text":{"type":"string","minLength":1,"description":"Case-insensitive fuzzy match over node id, title, class, and compact node JSON."}
                },
                "additionalProperties": false
            },
            "page":{
                "type":"object",
                "properties":{
                    "limit":{"type":"integer","minimum":1,"maximum":1000,"default":50},
                    "cursor":{"type":"string"}
                },
                "additionalProperties": false
            }
        },
        "required":["assetPath"],
        "additionalProperties": false
    }))
}

fn blueprint_graph_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), graph_ref_schema());
    properties.insert(
        "commands".into(),
        serde_json::json!({
            "type":"array",
            "description":"Ordered Blueprint graph edit commands. Each command requires kind. If unsure which kinds are available, call schema_inspect with domain='blueprint' and tool='blueprint_graph_edit'. For a command-specific schema, call schema_inspect with operation=<kind>.",
            "items":{
                "type":"object",
                "description":"Command envelope. Command-specific fields are intentionally omitted from tools/list and documented through schema_inspect.",
                "properties":{
                    "kind":{"type":"string","minLength":1},
                    "alias":{"type":"string","minLength":1}
                },
                "required":["kind"],
                "additionalProperties": true
            }
        }),
    );
    properties.insert(
        "dryRun".into(),
        serde_json::json!({"type":"boolean","default":false}),
    );
    properties.insert(
        "expectedRevision".into(),
        serde_json::json!({"type":"string"}),
    );
    properties.insert(
        "idempotencyKey".into(),
        serde_json::json!({"type":"string"}),
    );
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","graph","commands"],
        "additionalProperties": false
    }))
}

fn pcg_graph_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), pcg_graph_ref_schema());
    properties.insert(
        "commands".into(),
        serde_json::json!({
            "type":"array",
            "description":"Ordered PCG graph edit commands. Each command requires kind. Use schema_inspect with domain='pcg' and tool='pcg_graph_edit' to list supported command kinds. For a command-specific schema, call schema_inspect with operation=<kind>. Use pcg_palette first and pass the selected entry to addFromPalette instead of guessing settings classes.",
            "items":{
                "type":"object",
                "description":"Command envelope. Command-specific fields are intentionally omitted from tools/list and documented through schema_inspect.",
                "properties":{
                    "kind":{"type":"string","minLength":1},
                    "alias":{"type":"string","minLength":1}
                },
                "required":["kind"],
                "additionalProperties": true
            }
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","commands"],
        "additionalProperties": false
    }))
}

fn pcg_graph_layout_schema() -> rmcp::model::JsonObject {
    selection_graph_layout_schema(pcg_graph_ref_schema())
}

fn material_graph_ref_schema() -> serde_json::Value {
    serde_json::json!({
        "type":"object",
        "description":"Optional Material graph reference. For Material this resolves to graph.assetPath.",
        "properties":{
            "kind":{"type":"string","enum":["asset"]},
            "assetPath":{"type":"string","minLength":1}
        },
        "required":["assetPath"],
        "additionalProperties": true
    })
}

fn material_graph_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), material_graph_ref_schema());
    properties.insert(
        "commands".into(),
        serde_json::json!({
            "type":"array",
            "description":"Ordered Material graph edit commands. Each command requires kind. Use schema_inspect with domain='material' and tool='material_graph_edit' to list supported command kinds. For a command-specific schema, call schema_inspect with operation=<kind>. Use material_palette first and pass the selected entry to addFromPalette instead of guessing expression classes.",
            "items":{
                "type":"object",
                "description":"Command envelope. Command-specific fields are intentionally omitted from tools/list and documented through schema_inspect.",
                "properties":{
                    "kind":{"type":"string","minLength":1},
                    "alias":{"type":"string","minLength":1}
                },
                "required":["kind"],
                "additionalProperties": true
            },
            "minItems":1
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","commands"],
        "additionalProperties": false
    }))
}

fn selection_graph_layout_schema(graph_ref: serde_json::Value) -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), graph_ref);
    properties.insert(
        "scope".into(),
        serde_json::json!({
            "type":"object",
            "description":"Choose what nodes to format. Only selection is supported: it moves exactly the explicit node list.",
            "properties":{
                "mode":{"type":"string","enum":["selection"],"description":"Format only the explicit nodes array."},
                "nodes":{
                    "type":"array",
                    "description":"Node ids from the matching graph.inspect tool. Only these nodes are moved.",
                    "minItems":1,
                    "items":{
                        "type":"object",
                        "properties":{
                            "id":{"type":"string","minLength":1,"description":"Stable node id returned by graph.inspect."}
                        },
                        "required":["id"],
                        "additionalProperties":false
                    }
                }
            },
            "required":["mode","nodes"],
            "additionalProperties":false
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","scope"],
        "additionalProperties": false
    }))
}

fn material_graph_layout_schema() -> rmcp::model::JsonObject {
    selection_graph_layout_schema(material_graph_ref_schema())
}

fn blueprint_graph_layout_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), graph_ref_schema());
    properties.insert(
        "root".into(),
        serde_json::json!({
            "$ref": "#/$defs/nodeRef",
            "description": "Root node id from blueprint_graph_inspect. Layout follows Blueprint exec output pins downstream from this node."
        }),
    );
    properties.insert(
        "spacing".into(),
        serde_json::json!({
            "type":"object",
            "description":"Optional spacing between formatted nodes. Defaults to {x:360,y:180}.",
            "properties":{
                "x":{"type":"number","description":"Horizontal spacing between layout columns. Default 360."},
                "y":{"type":"number","description":"Vertical spacing between layout rows. Default 180."}
            },
            "required":["x","y"],
            "additionalProperties":false
        }),
    );
    properties.insert(
        "origin".into(),
        serde_json::json!({
            "type":"object",
            "description":"Optional top-left anchor. If omitted, selection uses the selected nodes' current bounding-box top-left; tree keeps the root at its current position.",
            "properties":{
                "x":{"type":"number"},
                "y":{"type":"number"}
            },
            "required":["x","y"],
            "additionalProperties":false
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","graph","root"],
        "additionalProperties": false,
        "$defs": {
            "nodeRef": {
                "type":"object",
                "description":"Stable Blueprint graph node reference from blueprint_graph_inspect.",
                "properties": {
                    "id": { "type":"string", "minLength": 1, "description":"Stable node id returned by blueprint_graph_inspect." }
                },
                "required": ["id"],
                "additionalProperties": false
            }
        }
    }))
}

fn blueprint_compile_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), graph_ref_schema());
    properties.insert(
        "graphName".into(),
        serde_json::json!({"type":"string","minLength":1,"description":"Legacy compatibility graph address. Prefer graph:{id|name} for new calls."}),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath"],
        "additionalProperties": false
    }))
}

fn blueprint_palette_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "graph": graph_ref_schema(),
            "query":{"type":"string"},
            "contextSensitive":{"type":"boolean","default":true},
            "fromPins":{
                "type":"array",
                "items":{
                    "type":"object",
                    "properties":{
                        "node":{
                            "type":"object",
                            "properties":{"id":{"type":"string","minLength":1}},
                            "required":["id"],
                            "additionalProperties": false
                        },
                        "pin":{"type":"string","minLength":1}
                    },
                    "required":["node","pin"],
                    "additionalProperties": false
                }
            },
            "context":{
                "type":"object",
                "description":"Optional UE action-menu context. Use selectedObjects for bound component/widget events and member-context actions.",
                "properties":{
                    "selectedObjects":{
                        "type":"array",
                        "items":{
                            "type":"object",
                            "properties":{
                                "kind":{"type":"string","enum":["component_property"]},
                                "name":{"type":"string","minLength":1}
                            },
                            "required":["kind","name"],
                            "additionalProperties": false
                        }
                    },
                    "component":{
                        "type":"object",
                        "description":"Shortcut for selectedObjects:[{kind:'component_property',name}].",
                        "properties":{"name":{"type":"string","minLength":1}},
                        "required":["name"],
                        "additionalProperties": false
                    }
                },
                "additionalProperties": false
            },
            "limit":{"type":"integer","minimum":1,"maximum":500,"default":50},
            "offset":{"type":"integer","minimum":0,"default":0}
        },
        "required":["assetPath"],
        "additionalProperties": false
    }))
}

fn material_graph_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": { "type": "string", "minLength": 1, "description": "Material or MaterialFunction asset path." },
            "graphName": { "type": "string", "minLength": 1 },
            "graph": { "$ref": "#/$defs/materialGraphRef" },
            "graphRef": { "$ref": "#/$defs/materialGraphRef" },
            "nodeIds": { "type": "array", "items": { "type": "string" } },
            "nodeClasses": { "type": "array", "items": { "type": "string" } },
            "includeConnections": { "type": "boolean", "default": false }
        },
        "$defs": {
            "materialGraphRef": {
                "type": "object",
                "properties": {
                    "kind": { "type": "string", "enum": ["asset"] },
                    "assetPath": { "type": "string", "minLength": 1 },
                    "graphName": { "type": "string" }
                },
                "required": ["assetPath"],
                "additionalProperties": true
            }
        },
        "additionalProperties": false
    }))
}

fn material_node_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "nodeId":{"type":"string","minLength":1},
            "nodeClass":{"type":"string","minLength":1}
        },
        "additionalProperties": false
    }))
}

fn material_node_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), material_graph_ref_schema());
    properties.insert(
        "node".into(),
        serde_json::json!({
            "type":"object",
            "description":"Material expression node reference. Use id from material_graph_inspect/material_graph_edit results, or alias from an earlier edit command in the same request.",
            "properties":{
                "id":{"type":"string","minLength":1},
                "alias":{"type":"string","minLength":1}
            },
            "additionalProperties": false
        }),
    );
    properties.insert(
        "property".into(),
        serde_json::json!({
            "type":"string",
            "minLength":1,
            "description":"Editable property name from material.node.inspect properties[].name."
        }),
    );
    properties.insert(
        "value".into(),
        serde_json::json!({
            "description":"New property value. Use JSON string/number/boolean/null for scalar properties, or {\"importText\":\"...\"} for UE import text values."
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","node","property","value"],
        "additionalProperties": false
    }))
}

fn material_palette_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "Material or MaterialFunction asset path."
            },
            "graph": {
                "type": "object",
                "description": "Optional Material graph reference. For Material this currently resolves to graph.assetPath.",
                "properties": {
                    "kind": { "type": "string", "enum": ["asset"] },
                    "assetPath": { "type": "string", "minLength": 1 }
                },
                "required": ["assetPath"],
                "additionalProperties": true
            },
            "query": {
                "type": "string",
                "description": "Case-insensitive fuzzy search over UE Material palette label, category, tooltip, keywords, and action payload."
            },
            "elementTypes": {
                "type": "array",
                "description": "UE Material palette element families to include. Defaults to all.",
                "items": {
                    "type": "string",
                    "enum": ["expression"]
                }
            },
            "limit": { "type": "integer", "minimum": 1, "maximum": 500, "default": 50 },
            "offset": { "type": "integer", "minimum": 0, "default": 0 }
        },
        "additionalProperties": false
    }))
}

fn pcg_palette_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "PCG graph asset path."
            },
            "graph": {
                "type": "object",
                "description": "Optional PCG graph reference. For PCG this currently resolves to graph.assetPath.",
                "properties": {
                    "kind": { "type": "string", "enum": ["asset"] },
                    "assetPath": { "type": "string", "minLength": 1 }
                },
                "required": ["assetPath"],
                "additionalProperties": true
            },
            "query": {
                "type": "string",
                "description": "Case-insensitive fuzzy search over UE palette label, category, tooltip, keywords, and action payload."
            },
            "elementTypes": {
                "type": "array",
                "description": "UE PCG palette element families to include. Defaults to all.",
                "items": {
                    "type": "string",
                    "enum": ["native", "blueprint", "subgraph", "settings", "asset", "dataAsset", "other"]
                }
            },
            "limit": { "type": "integer", "minimum": 1, "maximum": 500, "default": 50 },
            "offset": { "type": "integer", "minimum": 0, "default": 0 }
        },
        "additionalProperties": false
    }))
}

fn pcg_node_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "PCG graph asset path for instance mode."
            },
            "graph": pcg_graph_ref_schema(),
            "node": {
                "type": "object",
                "description": "PCG node reference for instance mode.",
                "properties": {
                    "id": { "type": "string", "minLength": 1 }
                },
                "required": ["id"],
                "additionalProperties": false
            },
            "nodeClass": {
                "type": "string",
                "minLength": 1,
                "description": "PCG settings class path for class mode."
            },
            "settingsClass": {
                "type": "string",
                "minLength": 1,
                "description": "Alias for nodeClass."
            }
        },
        "additionalProperties": false
    }))
}

fn pcg_parameter_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "PCG graph asset path."
            },
            "graph": pcg_graph_ref_schema(),
            "name": {
                "type": "string",
                "minLength": 1,
                "description": "Optional exact parameter name filter."
            }
        },
        "additionalProperties": false
    }))
}

fn pcg_parameter_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({
            "type": "string",
            "minLength": 1,
            "description": "PCG graph asset path."
        }),
    );
    properties.insert("graph".into(), pcg_graph_ref_schema());
    properties.insert(
        "operation".into(),
        serde_json::json!({
            "type": "string",
            "enum": ["create", "update", "rename", "delete", "setDefault"],
            "description": "PCG parameter edit operation. Use schema_inspect with domain='pcg', tool='pcg_parameter_edit', and operation='<operation>' for operation-specific args."
        }),
    );
    properties.insert(
        "args".into(),
        serde_json::json!({
            "type": "object",
            "description": "Operation-specific arguments. The shape is intentionally omitted from tools/list; call schema_inspect for the selected pcg_parameter_edit operation.",
            "additionalProperties": true
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": properties,
        "required": ["operation", "args"],
        "additionalProperties": false
    }))
}

fn pcg_compile_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "PCG graph asset path."
            },
            "graph": pcg_graph_ref_schema()
        },
        "additionalProperties": false
    }))
}

fn diagnostic_tail_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "fromSeq":{"type":"integer","minimum":0,"default":0},
            "limit":{"type":"integer","minimum":1,"maximum":1000,"default":200},
            "filters":{
                "type":"object",
                "properties":{
                    "severity":{"type":"string"},
                    "category":{"type":"string"},
                    "source":{"type":"string"},
                    "assetPathPrefix":{"type":"string"}
                },
                "additionalProperties": false
            }
        },
        "additionalProperties": false
    }))
}

fn log_tail_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "fromSeq":{"type":"integer","minimum":0,"default":0},
            "limit":{"type":"integer","minimum":1,"maximum":1000,"default":200},
            "filters":{
                "type":"object",
                "properties":{
                    "minVerbosity":{"type":"string"},
                    "category":{"type":"string"},
                    "categories":{
                        "type":"array",
                        "items":{"type":"string"}
                    },
                    "source":{"type":"string"},
                    "contains":{"type":"string"}
                },
                "additionalProperties": false
            }
        },
        "additionalProperties": false
    }))
}

fn widget_palette_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "Optional WidgetBlueprint asset path. Widget Palette search is mostly global; this is reserved for context-specific filtering."
            },
            "query": {
                "type": "string",
                "description": "Case-insensitive fuzzy search over UE Widget Palette label, category, tooltip, keywords, and payload."
            },
            "elementTypes": {
                "type": "array",
                "description": "Widget Palette entry families to include. Defaults to all.",
                "items": {
                    "type": "string",
                    "enum": ["native", "user"]
                }
            },
            "limit": { "type": "integer", "minimum": 1, "maximum": 500, "default": 50 },
            "offset": { "type": "integer", "minimum": 0, "default": 0 }
        },
        "additionalProperties": false
    }))
}

fn widget_tree_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "WidgetBlueprint asset path."
            },
            "view": {
                "type": "string",
                "enum": ["outline", "layout"],
                "default": "outline",
                "description": "outline returns hierarchy and widget identity; layout also includes slot/layout details."
            },
            "filter": {
                "type": "object",
                "properties": {
                    "names": {
                        "type": "array",
                        "items": { "type": "string", "minLength": 1 },
                        "description": "Exact widget names to return in matches."
                    },
                    "text": {
                        "type": "string",
                        "minLength": 1,
                        "description": "Case-insensitive fuzzy search over serialized widget tree entries."
                    }
                },
                "additionalProperties": false
            }
        },
        "required": ["assetPath"],
        "additionalProperties": false
    }))
}

fn widget_tree_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({
            "type": "string",
            "minLength": 1,
            "description": "WidgetBlueprint asset path."
        }),
    );
    properties.insert(
        "commands".into(),
        serde_json::json!({
            "type": "array",
            "description": "Ordered WidgetTree edit commands. Each command requires kind. Use schema_inspect with domain='widget' and tool='widget_tree_edit' to list supported command kinds. For a command-specific schema, call schema_inspect with operation=<kind>. Use widget_palette first and pass the selected entry to addFromPalette instead of guessing widget classes.",
            "items": {
                "type": "object",
                "description": "Command envelope. Command-specific fields are intentionally omitted from tools/list and documented through schema_inspect.",
                "properties": {
                    "kind": { "type": "string", "minLength": 1 }
                },
                "required": ["kind"],
                "additionalProperties": true
            },
            "minItems": 1
        }),
    );
    properties.insert(
        "dryRun".into(),
        serde_json::json!({
            "type": "boolean",
            "default": false
        }),
    );
    properties.insert(
        "expectedRevision".into(),
        serde_json::json!({
            "type": "string"
        }),
    );
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": properties,
        "required": ["assetPath", "commands"],
        "additionalProperties": false
    }))
}

fn widget_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({
            "type": "string",
            "minLength": 1,
            "description": "WidgetBlueprint asset path."
        }),
    );
    properties.insert(
        "commands".into(),
        serde_json::json!({
            "type": "array",
            "description": "Ordered widget instance edit commands. Each command requires kind. Use schema_inspect with domain='widget' and tool='widget_edit' to list supported command kinds. Use widget_inspect to discover writable property names and serialized value expectations.",
            "items": {
                "type": "object",
                "description": "Command envelope. Command-specific fields are intentionally omitted from tools/list and documented through schema_inspect.",
                "properties": {
                    "kind": { "type": "string", "minLength": 1 }
                },
                "required": ["kind"],
                "additionalProperties": true
            },
            "minItems": 1
        }),
    );
    properties.insert(
        "dryRun".into(),
        serde_json::json!({"type": "boolean", "default": false}),
    );
    properties.insert(
        "expectedRevision".into(),
        serde_json::json!({"type": "string"}),
    );
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": properties,
        "required": ["assetPath", "commands"],
        "additionalProperties": false
    }))
}

fn widget_event_create_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "WidgetBlueprint asset path."
            },
            "widget": {
                "type": "object",
                "description": "WidgetTree instance reference.",
                "properties": {
                    "name": {
                        "type": "string",
                        "minLength": 1
                    }
                },
                "required": ["name"],
                "additionalProperties": false
            },
            "event": {
                "type": "string",
                "minLength": 1,
                "description": "Multicast delegate property name on the widget class, such as OnClicked."
            },
            "dryRun": {
                "type": "boolean",
                "default": false
            }
        },
        "required": ["assetPath", "widget", "event"],
        "additionalProperties": false
    }))
}

fn widget_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "widgetClass": {
                "type": "string",
                "minLength": 1,
                "description": "UMG widget class path or short class name, such as /Script/UMG.TextBlock or TextBlock."
            },
            "assetPath": {
                "type": "string",
                "minLength": 1,
                "description": "WidgetBlueprint asset path when inspecting a concrete WidgetTree instance."
            },
            "widget": {
                "type": "object",
                "description": "WidgetTree instance reference.",
                "properties": {
                    "name": { "type": "string", "minLength": 1 }
                },
                "required": ["name"],
                "additionalProperties": false
            }
        },
        "additionalProperties": false
    }))
}

fn play_participant_wait_conditions_met(
    payload: &serde_json::Value,
    conditions: &[serde_json::Value],
) -> bool {
    let Some(participants) = payload
        .get("participants")
        .and_then(|value| value.as_array())
    else {
        return false;
    };

    conditions.iter().all(|condition| {
        let Some(condition) = condition.as_object() else {
            return false;
        };
        if let Some(participant_id) = condition
            .get("participant")
            .and_then(|value| value.as_str())
        {
            return participants.iter().any(|participant| {
                participant.get("id").and_then(|value| value.as_str()) == Some(participant_id)
                    && play_participant_matches_wait_state(participant, condition)
            });
        }

        let role = condition.get("role").and_then(|value| value.as_str());
        let expected_count = condition
            .get("count")
            .and_then(|value| value.as_u64())
            .unwrap_or(1);
        let actual_count = participants
            .iter()
            .filter(|participant| {
                role.is_none_or(|role| {
                    participant.get("role").and_then(|value| value.as_str()) == Some(role)
                }) && play_participant_matches_wait_state(participant, condition)
            })
            .count() as u64;
        actual_count >= expected_count
    })
}

fn play_wait_participant_conditions_from_args(
    args: &rmcp::model::JsonObject,
) -> Option<Vec<serde_json::Value>> {
    if args
        .get("until")
        .and_then(|value| value.as_object())
        .and_then(|until| until.get("participants"))
        .is_some()
    {
        return None;
    }

    let participant = args.get("participant").and_then(|value| value.as_str());
    let role = args.get("role").and_then(|value| value.as_str());
    if participant.is_none() && role.is_none() {
        return None;
    }

    let mut condition = serde_json::Map::new();
    if let Some(participant) = participant {
        condition.insert("participant".into(), serde_json::json!(participant));
    }
    if let Some(role) = role {
        condition.insert("role".into(), serde_json::json!(role));
    }
    if let Some(count) = args.get("count").and_then(|value| value.as_u64()) {
        condition.insert("count".into(), serde_json::json!(count));
    }
    condition.insert("state".into(), serde_json::json!("ready"));
    Some(vec![serde_json::Value::Object(condition)])
}

fn play_participant_matches_wait_state(
    participant: &serde_json::Value,
    condition: &serde_json::Map<String, serde_json::Value>,
) -> bool {
    match condition.get("state").and_then(|value| value.as_str()) {
        Some("ready") => participant
            .get("ready")
            .and_then(|value| value.as_bool())
            .unwrap_or(false),
        Some(_) => false,
        None => true,
    }
}

fn structured_result(value: serde_json::Value) -> CallToolResult {
    CallToolResult::structured(value)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ProjectStatusFilter {
    Online,
    Offline,
    All,
}

impl ProjectStatusFilter {
    fn parse(value: Option<&str>) -> Result<Self, String> {
        match value.unwrap_or("online") {
            "online" => Ok(Self::Online),
            "offline" => Ok(Self::Offline),
            "all" => Ok(Self::All),
            other => Err(format!("invalid project.list status: {other}")),
        }
    }
}

#[derive(Debug, Clone, serde::Deserialize)]
struct RuntimeRecord {
    #[serde(rename = "runtimeId")]
    runtime_id: Option<String>,
    #[serde(rename = "projectId")]
    project_id: Option<String>,
    name: Option<String>,
    #[serde(rename = "projectRoot")]
    project_root: PathBuf,
    uproject: Option<PathBuf>,
    endpoint: Option<PathBuf>,
    #[serde(rename = "pluginPath")]
    plugin_path: Option<PathBuf>,
    #[serde(rename = "pluginInstallScope")]
    plugin_install_scope: Option<String>,
    #[serde(rename = "pluginManagedBy")]
    plugin_managed_by: Option<String>,
    #[serde(rename = "pluginVersion")]
    plugin_version: Option<String>,
    #[serde(rename = "protocolVersion")]
    protocol_version: Option<u64>,
    #[serde(rename = "lastSeenAt")]
    last_seen_at: Option<String>,
}

#[derive(Debug, Clone, serde::Deserialize)]
struct ProjectRecord {
    #[serde(rename = "projectId")]
    project_id: Option<String>,
    name: Option<String>,
    #[serde(rename = "projectRoot")]
    project_root: PathBuf,
    uproject: Option<PathBuf>,
    #[serde(rename = "pluginPath")]
    plugin_path: Option<PathBuf>,
    #[serde(rename = "pluginInstallScope")]
    plugin_install_scope: Option<String>,
    #[serde(rename = "pluginManagedBy")]
    plugin_managed_by: Option<String>,
    #[serde(rename = "pluginVersion")]
    plugin_version: Option<String>,
    #[serde(rename = "lastSeenAt")]
    last_seen_at: Option<String>,
}

#[derive(Debug, Clone)]
struct RuntimeProject {
    project_id: String,
    name: String,
    project_root: PathBuf,
    uproject: Option<PathBuf>,
    endpoint: PathBuf,
    status: String,
    attachable: bool,
    plugin_installed: bool,
    plugin_path: Option<PathBuf>,
    plugin_install_scope: Option<String>,
    plugin_managed_by: Option<String>,
    plugin_version: Option<String>,
    protocol_version: Option<u64>,
    last_seen_at: Option<String>,
    reason: Option<String>,
}

fn call_project_list(args: &rmcp::model::JsonObject) -> CallToolResult {
    let filter =
        match ProjectStatusFilter::parse(args.get("status").and_then(|value| value.as_str())) {
            Ok(filter) => filter,
            Err(message) => {
                return CallToolResult::error(vec![rmcp::model::Content::text(message)])
            }
        };

    let include_diagnostics = args
        .get("includeDiagnostics")
        .and_then(|value| value.as_bool())
        .unwrap_or(false);
    let projects = discover_runtime_projects(filter);
    let projects_json: Vec<serde_json::Value> = projects
        .into_iter()
        .map(|project| project_to_json(project, include_diagnostics))
        .collect();
    structured_result(serde_json::json!({ "projects": projects_json }))
}

fn call_project_install(args: &rmcp::model::JsonObject) -> CallToolResult {
    let Some(project_root_raw) = args.get("projectRoot").and_then(|value| value.as_str()) else {
        return CallToolResult::error(vec![rmcp::model::Content::text(
            "project.install requires projectRoot.",
        )]);
    };
    let force = args
        .get("force")
        .and_then(|value| value.as_bool())
        .unwrap_or(false);

    let project_root = match validate_project_root(Path::new(project_root_raw)) {
        Ok(project_root) => project_root,
        Err(message) => return CallToolResult::error(vec![rmcp::model::Content::text(message)]),
    };

    if discover_runtime_projects(ProjectStatusFilter::Online)
        .into_iter()
        .any(|project| same_path(&project.project_root, &project_root))
    {
        return CallToolResult::error(vec![rmcp::model::Content::text(
            "Project is online. Close Unreal Editor before installing or updating LOOMLE project support.",
        )]);
    }

    let active_version = match read_global_active_version() {
        Ok(version) => version,
        Err(message) => return CallToolResult::error(vec![rmcp::model::Content::text(message)]),
    };
    match sync_project_support_to_version(&project_root, &active_version, force) {
        Ok(outcome) => structured_result(outcome.to_project_install_json()),
        Err(message) => CallToolResult::error(vec![rmcp::model::Content::text(message)]),
    }
}

fn infer_attached_project_root(
    explicit_project_root: Option<PathBuf>,
    cwd: Option<PathBuf>,
    online_projects: &[RuntimeProject],
) -> Option<PathBuf> {
    if explicit_project_root.is_some() {
        return explicit_project_root;
    }

    let cwd = cwd
        .map(|path| path.canonicalize().unwrap_or(path))
        .unwrap_or_default();
    if !cwd.as_os_str().is_empty() {
        if let Some(project) = online_projects
            .iter()
            .filter(|project| cwd.starts_with(&project.project_root))
            .max_by_key(|project| project.project_root.components().count())
        {
            return Some(project.project_root.clone());
        }
    }

    if online_projects.len() == 1 {
        return Some(online_projects[0].project_root.clone());
    }

    None
}

fn discover_runtime_projects(filter: ProjectStatusFilter) -> Vec<RuntimeProject> {
    let mut runtimes = read_runtime_records();
    let mut projects = Vec::new();

    for record in read_project_records() {
        let project_id = project_record_project_id(&record);
        let runtime = runtimes.remove(&project_id);
        let project = project_record_to_project(record, runtime);
        let include = match filter {
            ProjectStatusFilter::Online => project.status == "online",
            ProjectStatusFilter::Offline => project.status == "offline",
            ProjectStatusFilter::All => true,
        };
        if include {
            projects.push(project);
        }
    }

    for record in runtimes.into_values() {
        let project = runtime_record_to_project(record);
        let include = match filter {
            ProjectStatusFilter::Online => project.status == "online",
            ProjectStatusFilter::Offline => project.status == "offline",
            ProjectStatusFilter::All => true,
        };
        if include {
            projects.push(project);
        }
    }

    projects.sort_by(|left, right| left.name.cmp(&right.name));
    projects
}

fn read_runtime_records() -> HashMap<String, RuntimeRecord> {
    let runtime_dir = loomle_root().join("state").join("runtimes");
    let Ok(entries) = std::fs::read_dir(runtime_dir) else {
        return HashMap::new();
    };

    let mut records = HashMap::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|ext| ext.to_str()) != Some("json") {
            continue;
        }
        let Ok(raw) = std::fs::read_to_string(&path) else {
            continue;
        };
        let Ok(record) = serde_json::from_str::<RuntimeRecord>(strip_utf8_bom(&raw)) else {
            continue;
        };
        records.insert(runtime_record_project_id(&record), record);
    }
    records
}

fn read_project_records() -> Vec<ProjectRecord> {
    let project_dir = project_registry_dir();
    let Ok(entries) = std::fs::read_dir(project_dir) else {
        return Vec::new();
    };

    let mut records = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|ext| ext.to_str()) != Some("json") {
            continue;
        }
        let Ok(raw) = std::fs::read_to_string(&path) else {
            continue;
        };
        let Ok(record) = serde_json::from_str::<ProjectRecord>(strip_utf8_bom(&raw)) else {
            continue;
        };
        records.push(record);
    }
    records
}

fn runtime_record_project_id(record: &RuntimeRecord) -> String {
    record
        .project_id
        .clone()
        .or_else(|| record.runtime_id.clone())
        .unwrap_or_else(|| stable_project_id(&record.project_root))
}

fn project_record_project_id(record: &ProjectRecord) -> String {
    record
        .project_id
        .clone()
        .unwrap_or_else(|| stable_project_id(&record.project_root))
}

fn project_record_to_project(
    record: ProjectRecord,
    runtime: Option<RuntimeRecord>,
) -> RuntimeProject {
    let project_root = record.project_root;
    let runtime_endpoint = runtime.as_ref().and_then(|record| record.endpoint.clone());
    let endpoint = runtime_endpoint.unwrap_or_else(|| {
        Environment::for_project_root(project_root.clone()).runtime_endpoint_path
    });
    let endpoint_available = runtime
        .as_ref()
        .is_some_and(|_| runtime_endpoint_available(&endpoint));
    let status = if endpoint_available {
        "online"
    } else {
        "offline"
    }
    .to_string();
    let reason = if endpoint_available {
        None
    } else {
        Some("LOOMLE runtime endpoint is not available".to_string())
    };
    let project_id = record
        .project_id
        .unwrap_or_else(|| stable_project_id(&project_root));
    let name = runtime
        .as_ref()
        .and_then(|runtime| runtime.name.clone())
        .or(record.name)
        .unwrap_or_else(|| {
            project_root
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or("Unreal Project")
                .to_string()
        });
    RuntimeProject {
        project_id,
        name,
        project_root: project_root.clone(),
        uproject: runtime
            .as_ref()
            .and_then(|runtime| runtime.uproject.clone())
            .or(record.uproject),
        endpoint,
        status,
        attachable: endpoint_available,
        plugin_installed: runtime
            .as_ref()
            .and_then(|runtime| runtime.plugin_path.clone())
            .or_else(|| record.plugin_path.clone())
            .is_some_and(|path| path.is_dir())
            || project_root.join("Plugins").join("LoomleBridge").is_dir(),
        plugin_path: runtime
            .as_ref()
            .and_then(|runtime| runtime.plugin_path.clone())
            .or(record.plugin_path),
        plugin_install_scope: runtime
            .as_ref()
            .and_then(|runtime| runtime.plugin_install_scope.clone())
            .or(record.plugin_install_scope),
        plugin_managed_by: runtime
            .as_ref()
            .and_then(|runtime| runtime.plugin_managed_by.clone())
            .or(record.plugin_managed_by),
        plugin_version: runtime
            .as_ref()
            .and_then(|runtime| runtime.plugin_version.clone())
            .or(record.plugin_version),
        protocol_version: runtime
            .as_ref()
            .and_then(|runtime| runtime.protocol_version),
        last_seen_at: runtime
            .as_ref()
            .and_then(|runtime| runtime.last_seen_at.clone())
            .or(record.last_seen_at),
        reason,
    }
}

fn runtime_record_to_project(record: RuntimeRecord) -> RuntimeProject {
    let project_id = runtime_record_project_id(&record);
    let project_root = record.project_root;
    let endpoint = record.endpoint.unwrap_or_else(|| {
        Environment::for_project_root(project_root.clone()).runtime_endpoint_path
    });
    let endpoint_available = runtime_endpoint_available(&endpoint);
    let status = if endpoint_available {
        "online"
    } else {
        "offline"
    }
    .to_string();
    let reason = if endpoint_available {
        None
    } else {
        Some("LOOMLE runtime endpoint is not available".to_string())
    };
    let name = record.name.unwrap_or_else(|| {
        project_root
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("Unreal Project")
            .to_string()
    });
    RuntimeProject {
        project_id,
        name,
        project_root: project_root.clone(),
        uproject: record.uproject,
        endpoint,
        status,
        attachable: endpoint_available,
        plugin_installed: record
            .plugin_path
            .as_ref()
            .is_some_and(|path| path.is_dir())
            || project_root.join("Plugins").join("LoomleBridge").is_dir(),
        plugin_path: record.plugin_path,
        plugin_install_scope: record.plugin_install_scope,
        plugin_managed_by: record.plugin_managed_by,
        plugin_version: record.plugin_version,
        protocol_version: record.protocol_version,
        last_seen_at: record.last_seen_at,
        reason,
    }
}

fn project_root_online_project(project_root_raw: &str) -> Option<RuntimeProject> {
    let project_root = validate_project_root(Path::new(project_root_raw)).ok()?;
    let endpoint = Environment::for_project_root(project_root.clone()).runtime_endpoint_path;
    if !runtime_endpoint_available(&endpoint) {
        return None;
    }
    Some(RuntimeProject {
        project_id: stable_project_id(&project_root),
        name: project_name_from_root(&project_root),
        project_root: project_root.clone(),
        uproject: find_project_uproject(&project_root),
        endpoint,
        status: "online".to_string(),
        attachable: true,
        plugin_installed: project_root.join("Plugins").join("LoomleBridge").is_dir(),
        plugin_path: Some(project_root.join("Plugins").join("LoomleBridge")),
        plugin_install_scope: Some("project".to_string()),
        plugin_managed_by: Some("native".to_string()),
        plugin_version: None,
        protocol_version: None,
        last_seen_at: None,
        reason: None,
    })
}

fn project_to_json(project: RuntimeProject, include_diagnostics: bool) -> serde_json::Value {
    let mut value = serde_json::json!({
        "projectId": project.project_id,
        "name": project.name,
        "projectRoot": project.project_root.display().to_string(),
        "uproject": project.uproject.map(|path| path.display().to_string()),
        "status": project.status,
        "attachable": project.attachable,
        "pluginInstalled": project.plugin_installed,
        "pluginPath": project.plugin_path.map(|path| path.display().to_string()),
        "pluginInstallScope": project.plugin_install_scope,
        "pluginManagedBy": project.plugin_managed_by,
        "pluginVersion": project.plugin_version,
        "protocolVersion": project.protocol_version,
        "lastSeenAt": project.last_seen_at,
        "reason": project.reason,
    });
    if include_diagnostics {
        value["diagnostics"] = serde_json::json!({
            "endpoint": project.endpoint.display().to_string(),
            "endpointExists": runtime_endpoint_available(&project.endpoint),
        });
    }
    value
}

fn runtime_endpoint_available(endpoint: &Path) -> bool {
    #[cfg(target_os = "windows")]
    {
        match OpenOptions::new().read(true).write(true).open(endpoint) {
            Ok(_) => true,
            Err(error) => error.raw_os_error() == Some(231),
        }
    }

    #[cfg(not(target_os = "windows"))]
    {
        endpoint.exists()
    }
}

fn read_global_active_version() -> Result<String, String> {
    let active_path = loomle_root().join("install").join("active.json");
    let raw = fs::read_to_string(&active_path).map_err(|error| {
        format!(
            "failed to read global active install state {}: {error}",
            active_path.display()
        )
    })?;
    let value = parse_active_install_state_json(&raw, &active_path)?;
    for key in ["activeVersion", "installedVersion"] {
        if let Some(version) = value.get(key).and_then(|value| value.as_str()) {
            if !version.trim().is_empty() {
                return Ok(version.to_string());
            }
        }
    }
    Err(format!(
        "global active install state missing activeVersion: {}",
        active_path.display()
    ))
}

fn read_global_active_install_state() -> Result<Option<loomle::ActiveInstallState>, String> {
    let active_path = loomle_root().join("install").join("active.json");
    if !active_path.exists() {
        return Ok(None);
    }
    let raw = fs::read_to_string(&active_path).map_err(|error| {
        format!(
            "failed to read global active install state {}: {error}",
            active_path.display()
        )
    })?;
    let value = parse_active_install_state_json(&raw, &active_path)?;
    active_install_state_from_json(value, &active_path).map(Some)
}

fn parse_active_install_state_json(
    raw: &str,
    active_path: &Path,
) -> Result<serde_json::Value, String> {
    serde_json::from_str(strip_utf8_bom(raw)).map_err(|error| {
        format!(
            "failed to parse global active install state {}: {error}",
            active_path.display()
        )
    })
}

fn strip_utf8_bom(raw: &str) -> &str {
    raw.strip_prefix('\u{feff}').unwrap_or(raw)
}

fn active_install_state_from_json(
    value: serde_json::Value,
    active_path: &Path,
) -> Result<loomle::ActiveInstallState, String> {
    let active_version = value
        .get("activeVersion")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| {
            format!(
                "global active install state missing activeVersion: {}",
                active_path.display()
            )
        })?
        .to_string();
    let launcher_path = value
        .get("launcherPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| {
            format!(
                "global active install state missing launcherPath: {}",
                active_path.display()
            )
        })?;
    let active_client_path = value
        .get("activeClientPath")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| {
            format!(
                "global active install state missing activeClientPath: {}",
                active_path.display()
            )
        })?;
    Ok(loomle::ActiveInstallState {
        active_version,
        launcher_path: PathBuf::from(launcher_path),
        active_client_path: PathBuf::from(active_client_path),
    })
}

fn check_for_updates() -> serde_json::Value {
    let current = env!("CARGO_PKG_VERSION");
    match read_or_fetch_latest_version() {
        Ok(latest) => {
            let update_available = compare_semver(&latest, current)
                .map(|ordering| ordering == std::cmp::Ordering::Greater)
                .unwrap_or(latest != current);
            let mut value = serde_json::json!({
                "status": "ok",
                "latestVersion": latest,
                "updateAvailable": update_available,
                "updateCommand": if update_available { Some("loomle update".to_string()) } else { None::<String> },
            });
            if update_available {
                value["updateSteps"] = update_steps_for_agents();
            }
            value
        }
        Err(error) => serde_json::json!({
            "status": "unavailable",
            "message": error,
            "updateAvailable": false,
        }),
    }
}

fn update_steps_for_agents() -> serde_json::Value {
    serde_json::json!([
        "Close Unreal Editor for registered LOOMLE projects.",
        "Run `loomle update`.",
        "Restart Codex, Claude, or other MCP host sessions so they use the updated LOOMLE client.",
        "Reopen Unreal Editor after the update completes."
    ])
}

fn read_or_fetch_latest_version() -> Result<String, String> {
    let cache_path = loomle_root().join("state").join("update-check.json");
    if let Some(cached) = read_cached_latest_version(&cache_path, Duration::from_secs(6 * 60 * 60))
    {
        return Ok(cached);
    }

    let latest = fetch_latest_version()?;
    write_latest_version_cache(&cache_path, &latest);
    Ok(latest)
}

fn read_cached_latest_version(path: &Path, ttl: Duration) -> Option<String> {
    let raw = fs::read_to_string(path).ok()?;
    let value: serde_json::Value = serde_json::from_str(strip_utf8_bom(&raw)).ok()?;
    let checked_at = value.get("checkedAt").and_then(|value| value.as_u64())?;
    let now = unix_timestamp_secs();
    if now.saturating_sub(checked_at) > ttl.as_secs() {
        return None;
    }
    value
        .get("latestVersion")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
}

fn write_latest_version_cache(path: &Path, latest: &str) {
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    let value = serde_json::json!({
        "checkedAt": unix_timestamp_secs(),
        "latestVersion": latest,
    });
    if let Ok(raw) = serde_json::to_string_pretty(&value) {
        let _ = fs::write(path, raw + "\n");
    }
}

fn fetch_latest_version() -> Result<String, String> {
    let platform = current_platform_name();
    let url = format!(
        "https://github.com/loomle/loomle/releases/latest/download/loomle-manifest-{platform}.json"
    );
    let output = Command::new("curl")
        .args(["-fsSL", "--connect-timeout", "2", "--max-time", "3", &url])
        .output()
        .map_err(|error| format!("failed to start update check: {error}"))?;
    if !output.status.success() {
        return Err(format!("update check failed with status {}", output.status));
    }
    let value: serde_json::Value = serde_json::from_slice(&output.stdout)
        .map_err(|error| format!("update manifest is not valid JSON: {error}"))?;
    value
        .get("latest")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
        .ok_or_else(|| "update manifest is missing latest version".to_string())
}

fn unix_timestamp_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn compare_semver(left: &str, right: &str) -> Option<std::cmp::Ordering> {
    let left_parts = parse_semver_core(left)?;
    let right_parts = parse_semver_core(right)?;
    Some(left_parts.cmp(&right_parts))
}

fn parse_semver_core(version: &str) -> Option<(u64, u64, u64)> {
    let core = version
        .strip_prefix('v')
        .unwrap_or(version)
        .split(['-', '+'])
        .next()?;
    let mut parts = core.split('.');
    let major = parts.next()?.parse().ok()?;
    let minor = parts.next()?.parse().ok()?;
    let patch = parts.next()?.parse().ok()?;
    Some((major, minor, patch))
}

fn read_plugin_version(plugin_root: &Path) -> Option<String> {
    let raw = fs::read_to_string(plugin_root.join("LoomleBridge.uplugin")).ok()?;
    let value: serde_json::Value = serde_json::from_str(strip_utf8_bom(&raw)).ok()?;
    value
        .get("VersionName")
        .and_then(|value| value.as_str())
        .map(str::to_owned)
}

fn copy_tree_replace(source: &Path, destination: &Path) -> Result<(), String> {
    if !source.is_dir() {
        return Err(format!("install source not found: {}", source.display()));
    }
    if destination.exists() || destination.is_symlink() {
        let metadata = fs::symlink_metadata(destination)
            .map_err(|error| format!("failed to inspect {}: {error}", destination.display()))?;
        if metadata.is_dir() && !metadata.file_type().is_symlink() {
            fs::remove_dir_all(destination)
                .map_err(|error| format!("failed to remove {}: {error}", destination.display()))?;
        } else {
            fs::remove_file(destination)
                .map_err(|error| format!("failed to remove {}: {error}", destination.display()))?;
        }
    }
    fs::create_dir_all(
        destination
            .parent()
            .ok_or_else(|| format!("invalid destination: {}", destination.display()))?,
    )
    .map_err(|error| format!("failed to create {}: {error}", destination.display()))?;
    copy_dir_recursive(source, destination)
}

fn clean_project_plugin_build_outputs(plugin_root: &Path) -> Result<bool, String> {
    let binaries_dir = plugin_root.join("Binaries");
    if !binaries_dir.exists() {
        return Ok(false);
    }
    fs::remove_dir_all(&binaries_dir)
        .map_err(|error| format!("failed to remove {}: {error}", binaries_dir.display()))?;
    Ok(true)
}

fn current_unreal_binary_platform_dir() -> Option<&'static str> {
    if cfg!(target_os = "macos") {
        Some("Mac")
    } else if cfg!(windows) {
        Some("Win64")
    } else {
        None
    }
}

fn read_modules_manifest_build_id(path: &Path) -> Option<String> {
    let raw = fs::read_to_string(path).ok()?;
    let value: serde_json::Value = serde_json::from_str(strip_utf8_bom(&raw)).ok()?;
    value
        .get("BuildId")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
}

fn patch_modules_manifest_build_id(path: &Path, build_id: &str) -> Result<bool, String> {
    let raw = fs::read_to_string(path)
        .map_err(|error| format!("failed to read {}: {error}", path.display()))?;
    let mut value: serde_json::Value = serde_json::from_str(strip_utf8_bom(&raw))
        .map_err(|error| format!("failed to parse {}: {error}", path.display()))?;
    if value
        .get("BuildId")
        .and_then(|value| value.as_str())
        .is_some_and(|existing| existing == build_id)
    {
        return Ok(false);
    }
    let Some(object) = value.as_object_mut() else {
        return Err(format!(
            "modules manifest must be a JSON object: {}",
            path.display()
        ));
    };
    object.insert(
        "BuildId".to_string(),
        serde_json::Value::String(build_id.to_string()),
    );
    fs::write(
        path,
        serde_json::to_string_pretty(&value)
            .map_err(|error| format!("failed to encode {}: {error}", path.display()))?
            + "\n",
    )
    .map_err(|error| format!("failed to write {}: {error}", path.display()))?;
    Ok(true)
}

fn patch_plugin_modules_build_id_from_engine_manifest(
    plugin_modules_path: &Path,
    engine_modules_path: &Path,
) -> Result<bool, String> {
    let Some(engine_build_id) = read_modules_manifest_build_id(engine_modules_path) else {
        return Err(format!(
            "engine modules manifest is missing BuildId: {}",
            engine_modules_path.display()
        ));
    };
    patch_modules_manifest_build_id(plugin_modules_path, &engine_build_id)
}

fn read_project_engine_association(project_root: &Path) -> Option<String> {
    let uproject = find_project_uproject(project_root)?;
    let raw = fs::read_to_string(uproject).ok()?;
    let value: serde_json::Value = serde_json::from_str(strip_utf8_bom(&raw)).ok()?;
    value
        .get("EngineAssociation")
        .and_then(|value| value.as_str())
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
}

fn engine_root_env_var_for_association(association: &str) -> Option<String> {
    let suffix = if cfg!(target_os = "macos") {
        "MAC"
    } else if cfg!(windows) {
        "WINDOWS"
    } else {
        return None;
    };
    let normalized = association.replace('.', "_").replace('-', "_");
    Some(format!("UE_{}_ROOT_{}", normalized, suffix))
}

fn candidate_engine_roots_for_project(project_root: &Path) -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    if let Some(path) = env::var_os("LOOMLE_UNREAL_ENGINE_ROOT") {
        candidates.push(PathBuf::from(path));
    }

    let association = read_project_engine_association(project_root);
    if let Some(association) = association.as_deref() {
        if let Some(env_var) = engine_root_env_var_for_association(association) {
            if let Some(path) = env::var_os(env_var) {
                candidates.push(PathBuf::from(path));
            }
        }
        let association_path = PathBuf::from(association);
        if association_path.is_absolute() {
            candidates.push(association_path);
        }
        if cfg!(target_os = "macos") {
            candidates.push(PathBuf::from(format!(
                "/Users/Shared/Epic Games/UE_{}",
                association
            )));
        } else if cfg!(windows) {
            candidates.push(PathBuf::from(format!(
                "C:\\Program Files\\Epic Games\\UE_{}",
                association
            )));
        }
    }

    if cfg!(target_os = "macos") {
        if let Some(path) = env::var_os("UE_5_7_ROOT_MAC") {
            candidates.push(PathBuf::from(path));
        }
        candidates.push(PathBuf::from("/Users/Shared/Epic Games/UE_5.7"));
    } else if cfg!(windows) {
        if let Some(path) = env::var_os("UE_5_7_ROOT_WINDOWS") {
            candidates.push(PathBuf::from(path));
        }
        candidates.push(PathBuf::from("C:\\Program Files\\Epic Games\\UE_5.7"));
    }

    let mut seen = HashSet::new();
    candidates
        .into_iter()
        .filter(|path| seen.insert(path.display().to_string()))
        .collect()
}

fn resolve_engine_modules_manifest_for_project(
    project_root: &Path,
    platform_dir: &str,
) -> Option<PathBuf> {
    candidate_engine_roots_for_project(project_root)
        .into_iter()
        .map(|root| {
            root.join("Engine")
                .join("Binaries")
                .join(platform_dir)
                .join("UnrealEditor.modules")
        })
        .find(|path| path.is_file())
}

fn reconcile_project_plugin_modules_build_id(
    project_root: &Path,
    plugin_root: &Path,
) -> Result<Option<bool>, String> {
    let Some(platform_dir) = current_unreal_binary_platform_dir() else {
        return Ok(None);
    };
    let plugin_modules_path = plugin_root
        .join("Binaries")
        .join(platform_dir)
        .join("UnrealEditor.modules");
    if !plugin_modules_path.is_file() {
        return Ok(None);
    }
    let Some(engine_modules_path) =
        resolve_engine_modules_manifest_for_project(project_root, platform_dir)
    else {
        return Ok(None);
    };
    patch_plugin_modules_build_id_from_engine_manifest(&plugin_modules_path, &engine_modules_path)
        .map(Some)
}

fn cleanup_old_binaries() {
    if !cfg!(windows) {
        return;
    }
    let Ok(exe) = env::current_exe() else { return };
    let old = exe.with_extension("exe.old");
    let _ = fs::remove_file(&old);
}

fn copy_file_replace(source: &Path, destination: &Path) -> Result<(), String> {
    if !source.is_file() {
        return Err(format!("install file not found: {}", source.display()));
    }
    if destination.exists() || destination.is_symlink() {
        #[cfg(windows)]
        {
            let old = destination.with_extension("exe.old");
            let _ = fs::remove_file(&old);
            fs::rename(destination, &old)
                .map_err(|error| format!("failed to rename {}: {error}", destination.display()))?;
        }
        #[cfg(not(windows))]
        fs::remove_file(destination)
            .map_err(|error| format!("failed to remove {}: {error}", destination.display()))?;
    }
    fs::create_dir_all(
        destination
            .parent()
            .ok_or_else(|| format!("invalid destination: {}", destination.display()))?,
    )
    .map_err(|error| {
        format!(
            "failed to create {}: {error}",
            destination
                .parent()
                .unwrap_or_else(|| Path::new("."))
                .display()
        )
    })?;
    fs::copy(source, destination).map_err(|error| {
        format!(
            "failed to copy {} to {}: {error}",
            source.display(),
            destination.display()
        )
    })?;
    make_executable_if_unix(destination)?;
    Ok(())
}

fn copy_dir_recursive(source: &Path, destination: &Path) -> Result<(), String> {
    fs::create_dir_all(destination)
        .map_err(|error| format!("failed to create {}: {error}", destination.display()))?;
    for entry in fs::read_dir(source)
        .map_err(|error| format!("failed to read {}: {error}", source.display()))?
    {
        let entry =
            entry.map_err(|error| format!("failed to read {}: {error}", source.display()))?;
        let source_path = entry.path();
        let destination_path = destination.join(entry.file_name());
        let file_type = entry
            .file_type()
            .map_err(|error| format!("failed to inspect {}: {error}", source_path.display()))?;
        if file_type.is_dir() {
            copy_dir_recursive(&source_path, &destination_path)?;
        } else if file_type.is_file() {
            fs::copy(&source_path, &destination_path).map_err(|error| {
                format!(
                    "failed to copy {} to {}: {error}",
                    source_path.display(),
                    destination_path.display()
                )
            })?;
        }
    }
    Ok(())
}

#[derive(Debug, Clone)]
struct ProjectSupportSyncOutcome {
    project_root: PathBuf,
    plugin_path: PathBuf,
    changed: bool,
    previous_version: Option<String>,
    installed_version: String,
    requires_editor_restart: bool,
    skipped_reason: Option<String>,
    message: Option<String>,
}

impl ProjectSupportSyncOutcome {
    fn to_project_install_json(&self) -> serde_json::Value {
        serde_json::json!({
            "status": if self.skipped_reason.is_some() {
                "skipped"
            } else if self.changed {
                "updated"
            } else {
                "unchanged"
            },
            "projectRoot": self.project_root.display().to_string(),
            "pluginPath": self.plugin_path.display().to_string(),
            "changed": self.changed,
            "previousVersion": self.previous_version,
            "installedVersion": self.installed_version,
            "requiresEditorRestart": self.requires_editor_restart,
            "skippedReason": self.skipped_reason,
            "message": self.message.clone().unwrap_or_else(|| if self.changed {
                "LOOMLE project support installed. Restart Unreal Editor to activate LoomleBridge."
                    .to_string()
            } else {
                "LOOMLE project support is already installed at the active version."
                    .to_string()
            })
        })
    }
}

#[derive(Debug, Default)]
struct RegisteredProjectSyncSummary {
    updated: usize,
    unchanged: usize,
    skipped_online: usize,
    failed: usize,
}

fn plugin_cache_path_for_version(version: &str) -> PathBuf {
    loomle_root()
        .join("versions")
        .join(version)
        .join("plugin-cache")
        .join("LoomleBridge")
}

fn project_registry_dir() -> PathBuf {
    loomle_root().join("state").join("projects")
}

fn project_registry_path(project_root: &Path) -> PathBuf {
    project_registry_dir().join(format!("{}.json", stable_project_id(project_root)))
}

fn find_project_uproject(project_root: &Path) -> Option<PathBuf> {
    let entries = fs::read_dir(project_root).ok()?;
    entries.flatten().map(|entry| entry.path()).find(|path| {
        path.extension()
            .and_then(|ext| ext.to_str())
            .is_some_and(|ext| ext.eq_ignore_ascii_case("uproject"))
    })
}

fn project_name_from_root(project_root: &Path) -> String {
    project_root
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("Unreal Project")
        .to_string()
}

fn write_project_registration(
    project_root: &Path,
    plugin_version: &str,
    source: &str,
) -> Result<(), String> {
    let project_id = stable_project_id(project_root);
    let path = project_registry_path(project_root);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }

    let previous = fs::read_to_string(&path)
        .ok()
        .and_then(|raw| serde_json::from_str::<serde_json::Value>(strip_utf8_bom(&raw)).ok());
    let registered_at = previous
        .as_ref()
        .and_then(|value| value.get("registeredAt").cloned())
        .unwrap_or_else(|| serde_json::json!(unix_timestamp_secs().to_string()));
    let now = unix_timestamp_secs().to_string();
    let uproject = find_project_uproject(project_root);
    let value = serde_json::json!({
        "schemaVersion": 1,
        "projectId": project_id,
        "name": project_name_from_root(project_root),
        "projectRoot": project_root.display().to_string(),
        "uproject": uproject.map(|path| path.display().to_string()),
        "pluginPath": project_root.join("Plugins").join("LoomleBridge").display().to_string(),
        "pluginInstallScope": "project",
        "pluginManagedBy": "native",
        "pluginVersion": plugin_version,
        "platform": current_platform_name(),
        "registeredAt": registered_at,
        "lastSeenAt": now,
        "source": source,
    });
    fs::write(
        &path,
        serde_json::to_string_pretty(&value)
            .map_err(|error| format!("failed to encode project registration: {error}"))?
            + "\n",
    )
    .map_err(|error| format!("failed to write {}: {error}", path.display()))
}

fn read_registered_project_record(project_root: &Path) -> Option<ProjectRecord> {
    let path = project_registry_path(project_root);
    let raw = fs::read_to_string(path).ok()?;
    serde_json::from_str::<ProjectRecord>(&raw).ok()
}

fn project_record_external_plugin_reason(record: &ProjectRecord) -> Option<String> {
    let managed_by = record.plugin_managed_by.as_deref().unwrap_or("");
    if managed_by == "fab" {
        return Some("FAB_MANAGED_PLUGIN".to_string());
    }
    if matches!(managed_by, "engine" | "external") {
        return Some("EXTERNAL_MANAGED_PLUGIN".to_string());
    }
    if record
        .plugin_install_scope
        .as_deref()
        .is_some_and(|scope| scope != "project")
    {
        return Some("EXTERNAL_MANAGED_PLUGIN".to_string());
    }
    let Some(plugin_path) = record.plugin_path.as_ref() else {
        return None;
    };
    let project_plugin_path = record.project_root.join("Plugins").join("LoomleBridge");
    if !same_path(plugin_path, &project_plugin_path) {
        return Some("EXTERNAL_MANAGED_PLUGIN".to_string());
    }
    None
}

fn sync_project_support_to_version(
    project_root: &Path,
    active_version: &str,
    force: bool,
) -> Result<ProjectSupportSyncOutcome, String> {
    let plugin_source = plugin_cache_path_for_version(active_version);
    if !plugin_source.is_dir() {
        return Err(format!(
            "LOOMLE plugin cache missing: {}",
            plugin_source.display()
        ));
    }

    let _lock = acquire_project_install_lock(project_root)?;
    let plugin_destination = project_root.join("Plugins").join("LoomleBridge");
    if let Some(record) = read_registered_project_record(project_root) {
        if let Some(reason) = project_record_external_plugin_reason(&record) {
            return Ok(ProjectSupportSyncOutcome {
                project_root: project_root.to_path_buf(),
                plugin_path: record
                    .plugin_path
                    .unwrap_or_else(|| plugin_destination.clone()),
                changed: false,
                previous_version: record.plugin_version,
                installed_version: active_version.to_string(),
                requires_editor_restart: false,
                skipped_reason: Some(reason.clone()),
                message: Some(if reason == "FAB_MANAGED_PLUGIN" {
                    "This project uses a Fab-managed LoomleBridge. Native loomle will not overwrite it; use loomle mcp and project.attach while the project is open.".to_string()
                } else {
                    "This project uses an externally managed LoomleBridge. Native loomle will not overwrite it; use loomle mcp and project.attach while the project is open.".to_string()
                }),
            });
        }
    }
    let previous_version = read_plugin_version(&plugin_destination);
    if previous_version.as_deref() == Some(active_version) && !force {
        let modules_reconcile =
            reconcile_project_plugin_modules_build_id(project_root, &plugin_destination)?;
        let (reconciled_modules, cleaned_build_outputs) = match modules_reconcile {
            Some(changed) => (changed, false),
            None => (
                false,
                clean_project_plugin_build_outputs(&plugin_destination)?,
            ),
        };
        write_project_registration(project_root, active_version, "project.sync")?;
        return Ok(ProjectSupportSyncOutcome {
            project_root: project_root.to_path_buf(),
            plugin_path: plugin_destination,
            changed: reconciled_modules || cleaned_build_outputs,
            previous_version,
            installed_version: active_version.to_string(),
            requires_editor_restart: reconciled_modules || cleaned_build_outputs,
            skipped_reason: None,
            message: if reconciled_modules {
                Some(
                    "Updated LoomleBridge module manifest BuildId to match the current Unreal Editor build."
                        .to_string(),
                )
            } else {
                cleaned_build_outputs.then(|| {
                    "Removed stale LoomleBridge build outputs so Unreal can rebuild plugin binaries for the current editor build.".to_string()
                })
            },
        });
    }

    let _ = clean_project_plugin_build_outputs(&plugin_destination)?;
    copy_tree_replace(&plugin_source, &plugin_destination)?;
    let _ = reconcile_project_plugin_modules_build_id(project_root, &plugin_destination)?;
    ensure_editor_performance_setting(project_root)?;
    write_project_registration(project_root, active_version, "project.sync")?;

    Ok(ProjectSupportSyncOutcome {
        project_root: project_root.to_path_buf(),
        plugin_path: plugin_destination,
        changed: true,
        previous_version,
        installed_version: active_version.to_string(),
        requires_editor_restart: true,
        skipped_reason: None,
        message: None,
    })
}

fn sync_registered_project_support() -> RegisteredProjectSyncSummary {
    let active_version = match read_global_active_version() {
        Ok(version) => version,
        Err(message) => {
            eprintln!("[loomle-update] project sync skipped: {message}");
            return RegisteredProjectSyncSummary {
                failed: 1,
                ..RegisteredProjectSyncSummary::default()
            };
        }
    };
    let projects = discover_runtime_projects(ProjectStatusFilter::All);
    let mut summary = RegisteredProjectSyncSummary::default();
    if projects.is_empty() {
        eprintln!("[loomle-update] no registered Unreal projects found");
        return summary;
    }

    for project in projects {
        if project.status == "online" {
            summary.skipped_online += 1;
            eprintln!(
                "[loomle-update] skipped online project {} ({})",
                project.name,
                project.project_root.display()
            );
            continue;
        }

        let project_root = match validate_project_root(&project.project_root) {
            Ok(project_root) => project_root,
            Err(message) => {
                summary.failed += 1;
                eprintln!(
                    "[loomle-update] failed to sync project {} ({}): {}",
                    project.name,
                    project.project_root.display(),
                    message
                );
                continue;
            }
        };

        match sync_project_support_to_version(&project_root, &active_version, false) {
            Ok(outcome) if outcome.changed => {
                summary.updated += 1;
                eprintln!(
                    "[loomle-update] updated project {} to {}",
                    project_root.display(),
                    active_version
                );
            }
            Ok(_) => {
                summary.unchanged += 1;
                eprintln!(
                    "[loomle-update] project {} is already at {}",
                    project_root.display(),
                    active_version
                );
            }
            Err(message) => {
                summary.failed += 1;
                eprintln!(
                    "[loomle-update] failed to sync project {}: {}",
                    project_root.display(),
                    message
                );
            }
        }
    }

    eprintln!(
        "[loomle-update] project sync summary: updated={}, unchanged={}, skippedOnline={}, failed={}",
        summary.updated, summary.unchanged, summary.skipped_online, summary.failed
    );
    if summary.skipped_online > 0 {
        eprintln!(
            "[loomle-update] close Unreal Editor and run `loomle update` again to sync skipped online projects"
        );
    }
    summary
}

fn ensure_editor_performance_setting(project_root: &Path) -> Result<(), String> {
    const SECTION: &str = "[/Script/UnrealEd.EditorPerformanceSettings]";
    const SETTING: &str = "bThrottleCPUWhenNotForeground=False";
    let settings_path = project_root
        .join("Config")
        .join("DefaultEditorSettings.ini");
    let existing = fs::read_to_string(&settings_path).unwrap_or_default();
    if existing
        .lines()
        .any(|line| line.trim().eq_ignore_ascii_case(SETTING))
    {
        return Ok(());
    }
    if let Some(parent) = settings_path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let mut next = existing;
    if !next.is_empty() && !next.ends_with('\n') {
        next.push('\n');
    }
    if !next.is_empty() {
        next.push('\n');
    }
    next.push_str(SECTION);
    next.push('\n');
    next.push_str(SETTING);
    next.push('\n');
    fs::write(&settings_path, next)
        .map_err(|error| format!("failed to write {}: {error}", settings_path.display()))
}

struct ProjectInstallLock {
    path: PathBuf,
}

impl Drop for ProjectInstallLock {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.path);
    }
}

fn acquire_project_install_lock(project_root: &Path) -> Result<ProjectInstallLock, String> {
    let locks_dir = loomle_root().join("locks");
    fs::create_dir_all(&locks_dir).map_err(|error| {
        format!(
            "failed to create lock directory {}: {error}",
            locks_dir.display()
        )
    })?;
    let lock_path = locks_dir.join(format!(
        "project-install-{}.lock",
        stable_project_id(project_root)
    ));
    match OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&lock_path)
    {
        Ok(_) => Ok(ProjectInstallLock { path: lock_path }),
        Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => Err(format!(
            "another LOOMLE project.install is already running for {}",
            project_root.display()
        )),
        Err(error) => Err(format!(
            "failed to acquire install lock {}: {error}",
            lock_path.display()
        )),
    }
}

fn same_path(left: &Path, right: &Path) -> bool {
    let left = left.canonicalize().unwrap_or_else(|_| left.to_path_buf());
    let right = right.canonicalize().unwrap_or_else(|_| right.to_path_buf());
    left == right
}

fn update_global_install(options: UpdateOptions) -> Result<(), String> {
    let install_root = loomle_root();
    let lock_path = install_root.join("locks").join("update.lock");
    let _lock = acquire_file_lock(&lock_path, "another loomle update is already running")?;

    if let Some(version) = options.version.as_deref() {
        if switch_to_installed_version(&install_root, version)? {
            eprintln!("[loomle-update] switched to locally installed version {version}");
            let _ = sync_registered_project_support();
            return Ok(());
        }
        eprintln!("[loomle-update] version {version} is not installed locally; downloading");
    }

    let tmp_dir = make_temp_update_dir()?;
    let installer_path = if cfg!(windows) {
        tmp_dir.join("install.ps1")
    } else {
        tmp_dir.join("install.sh")
    };
    let installer_url = installer_url_for_update();

    eprintln!("[loomle-update] downloading installer {installer_url}");
    download_to_file(&installer_url, &installer_path)?;
    if !cfg!(windows) {
        make_executable_if_unix(&installer_path)?;
    }

    let status = run_installer(&installer_path, &install_root, options)?;
    let _ = fs::remove_dir_all(&tmp_dir);
    if !status.success() {
        return Err(format!("installer failed with status {status}"));
    }
    let _ = sync_registered_project_support();
    Ok(())
}

fn switch_to_installed_version(install_root: &Path, version: &str) -> Result<bool, String> {
    let normalized_version = normalize_version(version);
    let client_name = current_platform_client_binary_name();
    let version_root = install_root.join("versions").join(&normalized_version);
    let version_client = version_root.join(&client_name);
    let plugin_cache = version_root.join("plugin-cache").join("LoomleBridge");
    if !version_client.is_file() {
        return Ok(false);
    }
    if !plugin_cache.is_dir() {
        return Err(format!(
            "installed version {normalized_version} is incomplete: missing {}",
            plugin_cache.display()
        ));
    }

    let launcher_path = install_root.join("bin").join(&client_name);
    copy_file_replace(&version_client, &launcher_path)?;
    write_active_install_state(
        install_root,
        &normalized_version,
        &current_platform_name(),
        &launcher_path,
        &version_client,
    )?;
    Ok(true)
}

fn normalize_version(version: &str) -> String {
    version.strip_prefix('v').unwrap_or(version).to_string()
}

fn current_platform_name() -> String {
    if cfg!(windows) {
        "windows".to_string()
    } else if cfg!(target_os = "macos") {
        "darwin".to_string()
    } else {
        "linux".to_string()
    }
}

fn current_platform_client_binary_name() -> String {
    if cfg!(windows) {
        "loomle.exe".to_string()
    } else {
        "loomle".to_string()
    }
}

fn write_active_install_state(
    install_root: &Path,
    version: &str,
    platform: &str,
    launcher_path: &Path,
    active_client_path: &Path,
) -> Result<(), String> {
    let active_state_path = install_root.join("install").join("active.json");
    if let Some(parent) = active_state_path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let value = serde_json::json!({
        "schemaVersion": 2,
        "installedVersion": version,
        "activeVersion": version,
        "platform": platform,
        "installRoot": install_root.display().to_string(),
        "launcherPath": launcher_path.display().to_string(),
        "activeClientPath": active_client_path.display().to_string(),
        "versionsRoot": install_root.join("versions").display().to_string(),
        "pluginCacheRoot": install_root.join("versions").join(version).join("plugin-cache").display().to_string(),
    });
    fs::write(
        &active_state_path,
        serde_json::to_string_pretty(&value)
            .map_err(|error| format!("failed to encode active install state: {error}"))?
            + "\n",
    )
    .map_err(|error| format!("failed to write {}: {error}", active_state_path.display()))
}

fn installer_url_for_update() -> String {
    if let Some(url) = env::var_os("LOOMLE_INSTALLER_URL") {
        return url.to_string_lossy().to_string();
    }
    if cfg!(windows) {
        "https://loomle.ai/install.ps1".to_string()
    } else {
        "https://loomle.ai/install.sh".to_string()
    }
}

fn run_installer(
    installer_path: &Path,
    install_root: &Path,
    options: UpdateOptions,
) -> Result<ExitStatus, String> {
    let args = build_installer_args(options, install_root, cfg!(windows));

    if cfg!(windows) {
        Command::new("powershell")
            .args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-File"])
            .arg(installer_path)
            .args(args)
            .status()
            .map_err(|error| format!("failed to start PowerShell installer: {error}"))
    } else {
        Command::new("bash")
            .arg(installer_path)
            .args(args)
            .status()
            .map_err(|error| format!("failed to start install.sh: {error}"))
    }
}

fn build_installer_args(
    options: UpdateOptions,
    install_root: &Path,
    preserve_launcher: bool,
) -> Vec<String> {
    let mut args = Vec::new();
    if let Some(version) = options.version {
        args.push("--version".to_string());
        args.push(version);
    }
    if let Some(manifest_url) = options.manifest_url {
        args.push("--manifest-url".to_string());
        args.push(manifest_url);
    }
    if let Some(asset_url) = options.asset_url {
        args.push("--asset-url".to_string());
        args.push(asset_url);
    }
    args.push("--install-root".to_string());
    args.push(install_root.display().to_string());
    if preserve_launcher {
        args.push("--preserve-launcher".to_string());
    }

    args
}

fn make_temp_update_dir() -> Result<PathBuf, String> {
    let base = env::temp_dir().join(format!("loomle-update-{}", std::process::id()));
    if base.exists() {
        fs::remove_dir_all(&base)
            .map_err(|error| format!("failed to reset temp dir {}: {error}", base.display()))?;
    }
    fs::create_dir_all(&base)
        .map_err(|error| format!("failed to create temp dir {}: {error}", base.display()))?;
    Ok(base)
}

fn download_to_file(url: &str, destination: &Path) -> Result<(), String> {
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let status = Command::new("curl")
        .args([
            "-fsSL",
            "--retry",
            "5",
            "--retry-delay",
            "2",
            "--retry-all-errors",
            "--connect-timeout",
            "15",
            "--max-time",
            "180",
            url,
            "-o",
        ])
        .arg(destination)
        .status()
        .map_err(|error| format!("failed to start curl: {error}"))?;
    if !status.success() {
        return Err(format!("curl failed for {url} with status {status}"));
    }
    Ok(())
}

fn make_executable_if_unix(path: &Path) -> Result<(), String> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(path)
            .map_err(|error| format!("failed to stat {}: {error}", path.display()))?
            .permissions();
        permissions.set_mode(permissions.mode() | 0o755);
        fs::set_permissions(path, permissions)
            .map_err(|error| format!("failed to chmod {}: {error}", path.display()))?;
    }
    Ok(())
}

#[derive(Debug)]
struct FileLock {
    path: PathBuf,
}

impl Drop for FileLock {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.path);
    }
}

const FILE_LOCK_STALE_AFTER: Duration = Duration::from_secs(30 * 60);

#[derive(Debug, serde::Deserialize, serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct FileLockMetadata {
    pid: u32,
    started_at_unix_secs: u64,
    command: String,
}

fn acquire_file_lock(path: &Path, busy_message: &str) -> Result<FileLock, String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| {
            format!(
                "failed to create lock directory {}: {error}",
                parent.display()
            )
        })?;
    }

    for _ in 0..2 {
        match create_file_lock(path) {
            Ok(lock) => return Ok(lock),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {
                if remove_stale_file_lock(path)? {
                    continue;
                }
                return Err(active_file_lock_message(path, busy_message));
            }
            Err(error) => {
                return Err(format!(
                    "failed to acquire lock {}: {error}",
                    path.display()
                ));
            }
        }
    }

    Err(active_file_lock_message(path, busy_message))
}

fn create_file_lock(path: &Path) -> std::io::Result<FileLock> {
    let metadata = FileLockMetadata {
        pid: std::process::id(),
        started_at_unix_secs: unix_timestamp_secs(),
        command: env::args().next().unwrap_or_else(|| "loomle".to_string()),
    };
    let mut file = OpenOptions::new().write(true).create_new(true).open(path)?;
    if let Err(error) = serde_json::to_writer_pretty(&mut file, &metadata) {
        let _ = fs::remove_file(path);
        return Err(std::io::Error::new(std::io::ErrorKind::Other, error));
    }
    Ok(FileLock {
        path: path.to_path_buf(),
    })
}

fn remove_stale_file_lock(path: &Path) -> Result<bool, String> {
    let metadata = read_file_lock_metadata(path);
    if let Some(metadata) = metadata.as_ref() {
        if process_is_running(metadata.pid) {
            return Ok(false);
        }
    } else if !file_lock_is_older_than(path, FILE_LOCK_STALE_AFTER)? {
        return Ok(false);
    }

    match fs::remove_file(path) {
        Ok(()) => Ok(true),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(true),
        Err(error) => Err(format!(
            "failed to remove stale lock {}: {error}",
            path.display()
        )),
    }
}

fn active_file_lock_message(path: &Path, busy_message: &str) -> String {
    if let Some(metadata) = read_file_lock_metadata(path) {
        format!(
            "{busy_message}; lock={} pid={} startedAt={}",
            path.display(),
            metadata.pid,
            metadata.started_at_unix_secs
        )
    } else {
        format!("{busy_message}; lock={}", path.display())
    }
}

fn read_file_lock_metadata(path: &Path) -> Option<FileLockMetadata> {
    let contents = fs::read_to_string(path).ok()?;
    serde_json::from_str(strip_utf8_bom(&contents)).ok()
}

fn file_lock_is_older_than(path: &Path, age: Duration) -> Result<bool, String> {
    let modified = fs::metadata(path)
        .and_then(|metadata| metadata.modified())
        .map_err(|error| format!("failed to stat lock {}: {error}", path.display()))?;
    Ok(SystemTime::now()
        .duration_since(modified)
        .unwrap_or_default()
        >= age)
}

#[cfg(unix)]
fn process_is_running(pid: u32) -> bool {
    if pid == 0 {
        return false;
    }
    Command::new("kill")
        .arg("-0")
        .arg(pid.to_string())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|status| status.success())
        .unwrap_or(false)
}

#[cfg(windows)]
fn process_is_running(pid: u32) -> bool {
    if pid == 0 {
        return false;
    }
    Command::new("powershell")
        .args([
            "-NoProfile",
            "-Command",
            &format!("if (Get-Process -Id {pid} -ErrorAction SilentlyContinue) {{ exit 0 }} else {{ exit 1 }}"),
        ])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|status| status.success())
        .unwrap_or(false)
}

fn loomle_root() -> PathBuf {
    if let Some(root) = env::var_os("LOOMLE_INSTALL_ROOT") {
        return PathBuf::from(root);
    }
    home_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join(".loomle")
}

fn stable_project_id(project_root: &Path) -> String {
    let mut normalized = project_root.to_string_lossy().replace('\\', "/");
    while normalized.ends_with('/') {
        normalized.pop();
    }
    normalized.make_ascii_lowercase();
    format!("{:016x}", stable_fnv1a64(normalized.as_bytes()))
}

fn stable_fnv1a64(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf29ce484222325u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x100000001b3u64);
    }
    hash
}

#[derive(Debug, Clone)]
enum Cli {
    Help,
    Mcp { project_root: Option<PathBuf> },
    Doctor,
    Update(UpdateOptions),
}

#[derive(Debug, Clone, Default)]
struct UpdateOptions {
    version: Option<String>,
    manifest_url: Option<String>,
    asset_url: Option<String>,
}

impl Cli {
    fn parse<I, T>(args: I) -> Result<Self, String>
    where
        I: IntoIterator<Item = T>,
        T: Into<OsString>,
    {
        let mut iter = args.into_iter().map(Into::into);
        let _program = iter.next();

        let mut command = iter.next();
        if command.is_none() {
            return Ok(Self::Help);
        }

        let command_text = command
            .as_ref()
            .map(|arg| arg.to_string_lossy().to_string())
            .unwrap_or_default();

        let mut project_root = None;

        if command_text == "doctor" {
            ensure_no_extra_args(iter)?;
            return Ok(Self::Doctor);
        }
        if command_text == "update" {
            return Ok(Self::Update(parse_update_options(iter)?));
        }
        if command_text == "mcp" {
            command = iter.next();
        }

        if command_text == "--help" || command_text == "-h" {
            return Ok(Self::Help);
        }
        if command_text == "--version" {
            println!("{}", env!("CARGO_PKG_VERSION"));
            std::process::exit(0);
        }

        // Backward compatibility during the per-project client to global migration:
        // `loomle --project-root <ProjectRoot>` still starts MCP mode.
        let mut pending = command;

        while let Some(arg) = pending.take().or_else(|| iter.next()) {
            let text = arg.to_string_lossy().to_string();
            match text.as_str() {
                "--project-root" => {
                    let path = iter
                        .next()
                        .ok_or_else(|| String::from("missing value for --project-root"))?;
                    project_root = Some(PathBuf::from(path));
                }
                "--help" | "-h" => return Err(String::from("help requested")),
                "--version" => {
                    println!("{}", env!("CARGO_PKG_VERSION"));
                    std::process::exit(0);
                }
                other => return Err(format!("unknown argument: {other}")),
            }
        }

        Ok(Self::Mcp { project_root })
    }
}

fn ensure_no_extra_args<I, T>(mut iter: I) -> Result<(), String>
where
    I: Iterator<Item = T>,
    T: Into<OsString>,
{
    if let Some(arg) = iter.next() {
        return Err(format!(
            "unknown argument: {}",
            arg.into().to_string_lossy()
        ));
    }
    Ok(())
}

fn parse_update_options<I, T>(mut iter: I) -> Result<UpdateOptions, String>
where
    I: Iterator<Item = T>,
    T: Into<OsString>,
{
    let mut options = UpdateOptions::default();
    while let Some(arg) = iter.next() {
        let text = arg.into().to_string_lossy().to_string();
        match text.as_str() {
            "--version" => {
                let value = iter
                    .next()
                    .ok_or_else(|| String::from("missing value for --version"))?;
                options.version = Some(value.into().to_string_lossy().to_string());
            }
            "--manifest-url" => {
                let value = iter
                    .next()
                    .ok_or_else(|| String::from("missing value for --manifest-url"))?;
                options.manifest_url = Some(value.into().to_string_lossy().to_string());
            }
            "--asset-url" => {
                let value = iter
                    .next()
                    .ok_or_else(|| String::from("missing value for --asset-url"))?;
                options.asset_url = Some(value.into().to_string_lossy().to_string());
            }
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    Ok(options)
}

fn usage_text() -> &'static str {
    "LOOMLE global command\n\
\n\
Usage:\n\
  loomle mcp [--project-root <ProjectRoot>]\n\
  loomle doctor\n\
  loomle update [--version <Version>]\n\
  loomle --version\n\
  loomle --help\n\
\n\
Commands:\n\
  mcp       Start the stdio MCP server for Codex, Claude, or another host.\n\
  doctor    Print install paths and MCP host configuration hints.\n\
  update    Update to the latest release. With --version, switch to an installed\n\
            local version first; if it is not installed, download that version.\n\
            Registered offline Unreal projects are synced from the active plugin cache.\n\
\n\
Examples:\n\
  loomle update\n\
  loomle update --version 0.5.7\n\
  loomle mcp --project-root /path/to/UnrealProject\n"
}

fn print_usage_stdout() {
    print!("{}", usage_text());
}

fn print_usage_stderr() {
    eprint!("{}", usage_text());
}

#[cfg(test)]
mod tests {
    use super::{
        acquire_file_lock, active_install_state_from_json, all_declared_tools, asset_create_schema,
        asset_edit_schema, asset_inspect_schema, blueprint_graph_inspect_schema,
        blueprint_graph_layout_schema, blueprint_node_edit_schema, blueprint_node_inspect_schema,
        blueprint_palette_schema, build_blueprint_graph_layout_plan, build_installer_args,
        call_schema_inspect, compare_semver, current_platform_client_binary_name,
        current_unreal_binary_platform_dir, infer_attached_project_root,
        material_graph_edit_schema, material_graph_inspect_schema, material_graph_layout_schema,
        material_node_edit_schema, material_palette_schema, parse_active_install_state_json,
        parse_blueprint_graph_layout_request, patch_plugin_modules_build_id_from_engine_manifest,
        pcg_compile_schema, pcg_graph_inspect_schema, pcg_graph_layout_schema,
        pcg_node_inspect_schema, pcg_palette_schema, pcg_parameter_edit_schema,
        play_participant_wait_conditions_met, play_schema,
        play_wait_participant_conditions_from_args, public_tool_name_to_internal,
        read_cached_latest_version, read_file_lock_metadata, read_modules_manifest_build_id,
        read_plugin_version, runtime_declared_tools, shape_blueprint_graph_inspect_result,
        shape_pcg_compile_result, shape_pcg_graph_inspect_result, shape_pcg_node_inspect_result,
        shape_widget_tree_inspect_payload, switch_to_installed_version,
        sync_project_support_to_version, sync_registered_project_support,
        translate_blueprint_graph_edit_args, translate_blueprint_graph_inspect_args,
        translate_blueprint_node_edit_args, translate_blueprint_node_inspect_args,
        translate_blueprint_palette_args, translate_material_graph_edit_args,
        translate_material_graph_inspect_args, translate_material_graph_layout_args,
        translate_material_node_edit_args, translate_material_palette_args,
        translate_pcg_compile_args, translate_pcg_graph_inspect_args,
        translate_pcg_graph_layout_args, translate_pcg_node_inspect_args,
        translate_pcg_parameter_edit_args, translate_widget_edit_args,
        translate_widget_inspect_args, translate_widget_tree_edit_args,
        validate_blueprint_graph_inspect_args, validate_blueprint_graph_inspect_targets,
        validate_pcg_graph_inspect_args, widget_edit_schema, widget_inspect_schema,
        widget_palette_schema, widget_tree_edit_schema, widget_tree_inspect_schema,
        write_public_blueprint_graph_address, Cli, FileLockMetadata, RuntimeProject, UpdateOptions,
    };
    use rmcp::model::JsonObject;
    use std::ffi::OsString;
    use std::fs;
    use std::path::PathBuf;
    use std::sync::{Mutex, OnceLock};

    fn loomle_install_root_env_lock() -> std::sync::MutexGuard<'static, ()> {
        static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
        LOCK.get_or_init(|| Mutex::new(()))
            .lock()
            .expect("LOOMLE_INSTALL_ROOT test env lock")
    }

    #[test]
    fn parse_default_shows_help() {
        let cli = Cli::parse(vec![OsString::from("loomle")]).expect("cli");
        match cli {
            Cli::Help => {}
            _ => panic!("expected help"),
        }
    }

    #[test]
    fn parse_help() {
        let cli =
            Cli::parse(vec![OsString::from("loomle"), OsString::from("--help")]).expect("cli");
        match cli {
            Cli::Help => {}
            _ => panic!("expected help"),
        }
    }

    #[test]
    fn parse_mcp() {
        let cli = Cli::parse(vec![OsString::from("loomle"), OsString::from("mcp")]).expect("cli");
        match cli {
            Cli::Mcp { project_root } => assert!(project_root.is_none()),
            _ => panic!("expected mcp"),
        }
    }

    #[test]
    fn parse_project_root() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("mcp"),
            OsString::from("--project-root"),
            OsString::from("/tmp/project"),
        ])
        .expect("cli");
        match cli {
            Cli::Mcp { project_root } => {
                assert_eq!(project_root, Some(PathBuf::from("/tmp/project")))
            }
            _ => panic!("expected mcp"),
        }
    }

    #[test]
    fn parse_legacy_project_root_as_mcp() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("--project-root"),
            OsString::from("/tmp/project"),
        ])
        .expect("cli");
        match cli {
            Cli::Mcp { project_root } => {
                assert_eq!(project_root, Some(PathBuf::from("/tmp/project")))
            }
            _ => panic!("expected mcp"),
        }
    }

    #[test]
    fn parse_doctor() {
        let cli =
            Cli::parse(vec![OsString::from("loomle"), OsString::from("doctor")]).expect("cli");
        match cli {
            Cli::Doctor => {}
            _ => panic!("expected doctor"),
        }
    }

    #[test]
    fn parse_update() {
        let cli =
            Cli::parse(vec![OsString::from("loomle"), OsString::from("update")]).expect("cli");
        match cli {
            Cli::Update(_) => {}
            _ => panic!("expected update"),
        }
    }

    #[test]
    fn parse_update_version() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("update"),
            OsString::from("--version"),
            OsString::from("0.5.7"),
        ])
        .expect("cli");
        match cli {
            Cli::Update(options) => assert_eq!(options.version.as_deref(), Some("0.5.7")),
            _ => panic!("expected update"),
        }
    }

    #[test]
    fn update_installer_args_preserve_launcher() {
        let install_root = PathBuf::from(r"C:\Users\example\.loomle");
        let args = build_installer_args(UpdateOptions::default(), &install_root, true);

        assert!(args.iter().any(|arg| arg == "--preserve-launcher"));
        assert!(args.iter().any(|arg| arg == "--install-root"));
        assert!(args.iter().any(|arg| arg == r"C:\Users\example\.loomle"));
    }

    #[test]
    fn windows_installer_supports_preserve_launcher() {
        let script_path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("install.ps1");
        let script = fs::read_to_string(&script_path).expect("install.ps1");

        assert!(
            script.contains("$PreserveLauncher"),
            "{}",
            script_path.display()
        );
        assert!(
            script.contains("--preserve-launcher"),
            "{}",
            script_path.display()
        );
        assert!(
            script.contains("if (-not $PreserveLauncher)"),
            "{}",
            script_path.display()
        );
    }

    fn layout_node(id: &str, x: i64, y: i64, exec_children: &[&str]) -> serde_json::Value {
        let linked_to = exec_children
            .iter()
            .map(|child| serde_json::json!({ "nodeGuid": child, "pin": "Execute" }))
            .collect::<Vec<_>>();
        serde_json::json!({
            "id": id,
            "guid": id,
            "position": { "x": x, "y": y },
            "pins": [
                {
                    "name": "Then",
                    "direction": "output",
                    "category": "exec",
                    "layout": {
                        "offset": { "x": 240, "y": 31 },
                        "anchor": { "x": x + 240, "y": y + 31 },
                        "source": "estimate"
                    },
                    "linkedTo": linked_to
                },
                {
                    "name": "Execute",
                    "direction": "input",
                    "category": "exec",
                    "layout": {
                        "offset": { "x": 0, "y": 31 },
                        "anchor": { "x": x, "y": y + 31 },
                        "source": "estimate"
                    },
                    "linkedTo": []
                },
                {
                    "name": "Value",
                    "direction": "output",
                    "category": "int",
                    "linkedTo": [
                        { "nodeGuid": "dataOnly", "pin": "Value" }
                    ]
                }
            ]
        })
    }

    fn layout_inspect_payload(nodes: Vec<serde_json::Value>) -> serde_json::Value {
        serde_json::json!({
            "semanticSnapshot": {
                "nodes": nodes
            }
        })
    }

    #[test]
    fn blueprint_graph_layout_formats_root_exec_tree() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert("root".into(), serde_json::json!({ "id": "a" }));
        args.insert("origin".into(), serde_json::json!({ "x": 100, "y": 200 }));
        args.insert("spacing".into(), serde_json::json!({ "x": 300, "y": 150 }));
        args.insert("dryRun".into(), serde_json::json!(true));

        let request = parse_blueprint_graph_layout_request(&args).expect("request");
        let plan = build_blueprint_graph_layout_plan(
            &request,
            &layout_inspect_payload(vec![
                layout_node("a", 0, 0, &["b"]),
                layout_node("b", 10, 10, &[]),
                layout_node("c", 20, 20, &[]),
            ]),
        )
        .expect("plan");

        assert_eq!(plan.resolved_node_count, 2);
        assert_eq!(plan.moves.len(), 2);
        assert_eq!(plan.moves[0].node_id, "a");
        assert_eq!((plan.moves[0].to_x, plan.moves[0].to_y), (100, 200));
        assert_eq!(plan.moves[1].node_id, "b");
        assert_eq!((plan.moves[1].to_x, plan.moves[1].to_y), (400, 200));
        assert_eq!(
            plan.to_move_commands(),
            vec![
                serde_json::json!({
                    "kind": "moveNode",
                    "node": { "id": "a" },
                    "position": { "x": 100, "y": 200 }
                }),
                serde_json::json!({
                    "kind": "moveNode",
                    "node": { "id": "b" },
                    "position": { "x": 400, "y": 200 }
                })
            ]
        );
    }

    #[test]
    fn blueprint_graph_layout_writes_public_graph_name_for_edit_call() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert("root".into(), serde_json::json!({ "id": "a" }));

        let request = parse_blueprint_graph_layout_request(&args).expect("request");
        let mut edit_args = JsonObject::new();
        write_public_blueprint_graph_address(&mut edit_args, request.graph_address)
            .expect("public graph address");

        assert_eq!(
            edit_args.get("graph"),
            Some(&serde_json::json!({ "name": "EventGraph" }))
        );
        assert!(!edit_args.contains_key("graphName"));
        assert!(!edit_args.contains_key("graphRef"));
    }

    #[test]
    fn blueprint_graph_layout_writes_public_graph_id_for_edit_call() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "id": "graph-1" }));
        args.insert("root".into(), serde_json::json!({ "id": "a" }));

        let request = parse_blueprint_graph_layout_request(&args).expect("request");
        let mut edit_args = JsonObject::new();
        write_public_blueprint_graph_address(&mut edit_args, request.graph_address)
            .expect("public graph address");

        assert_eq!(
            edit_args.get("graph"),
            Some(&serde_json::json!({ "id": "graph-1" }))
        );
        assert!(!edit_args.contains_key("graphName"));
        assert!(!edit_args.contains_key("graphRef"));
    }

    #[test]
    fn blueprint_graph_layout_follows_exec_outputs_only() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graphName".into(), serde_json::json!("EventGraph"));
        args.insert("root".into(), serde_json::json!({ "id": "root" }));
        args.insert("spacing".into(), serde_json::json!({ "x": 300, "y": 150 }));

        let request = parse_blueprint_graph_layout_request(&args).expect("request");
        let plan = build_blueprint_graph_layout_plan(
            &request,
            &layout_inspect_payload(vec![
                layout_node("root", 100, 200, &["then"]),
                layout_node("then", 500, 600, &[]),
                layout_node("dataOnly", 700, 800, &[]),
            ]),
        )
        .expect("plan");

        assert_eq!(plan.resolved_node_count, 2);
        assert_eq!(plan.moves.len(), 1);
        assert_eq!(plan.moves[0].node_id, "then");
        assert_eq!((plan.moves[0].to_x, plan.moves[0].to_y), (400, 200));
        assert!(!plan
            .moves
            .iter()
            .any(|movement| movement.node_id == "dataOnly"));
    }

    #[test]
    fn blueprint_graph_layout_schema_is_self_describing() {
        let schema = blueprint_graph_layout_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("root"));
        assert!(properties.contains_key("spacing"));
        assert!(properties.contains_key("origin"));
        assert!(!properties.contains_key("operation"));
        assert!(!properties.contains_key("direction"));
        assert!(!properties.contains_key("style"));
        assert!(!properties.contains_key("scope"));
        assert!(!properties.contains_key("graphName"));
        let required = schema
            .get("required")
            .and_then(|value| value.as_array())
            .expect("required")
            .iter()
            .filter_map(|value| value.as_str())
            .collect::<std::collections::HashSet<_>>();
        assert!(required.contains("graph"));
        assert!(required.contains("root"));
        assert!(!required.contains("operation"));
        let schema_text = serde_json::to_string(&schema).expect("schema json");
        assert!(schema_text.contains("Layout follows Blueprint exec output pins"));
        assert!(schema_text.contains("blueprint_graph_inspect"));
        assert!(schema_text.contains("Defaults to {x:360,y:180}"));
    }

    #[test]
    fn blueprint_graph_layout_rejects_retired_scope_shape() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graphName".into(), serde_json::json!("EventGraph"));
        args.insert(
            "scope".into(),
            serde_json::json!({"mode":"selection","nodes":[{"id":"a"}]}),
        );

        let error = parse_blueprint_graph_layout_request(&args).expect_err("expected error");
        let text = serde_json::to_string(&error).expect("error json");
        assert!(text.contains("no longer accepts scope"));
    }

    #[test]
    fn semver_compare_handles_v_prefix() {
        assert_eq!(
            compare_semver("v0.5.8", "0.5.7"),
            Some(std::cmp::Ordering::Greater)
        );
        assert_eq!(
            compare_semver("0.5.7", "0.5.8"),
            Some(std::cmp::Ordering::Less)
        );
    }

    #[test]
    fn update_steps_prompt_agents_to_close_unreal_editor() {
        let steps = super::update_steps_for_agents();
        let steps = steps.as_array().expect("steps array");
        assert!(steps.iter().any(|step| step
            .as_str()
            .is_some_and(|step| step.contains("Close Unreal Editor"))));
        assert!(steps.iter().any(|step| step
            .as_str()
            .is_some_and(|step| step.contains("loomle update"))));
    }

    #[test]
    fn update_version_switches_to_local_install_when_present() {
        let root = std::env::temp_dir().join(format!(
            "loomle-test-switch-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let client_name = current_platform_client_binary_name();
        let version_root = root.join("versions").join("0.5.7");
        let version_client = version_root.join(&client_name);
        let plugin_cache = version_root.join("plugin-cache").join("LoomleBridge");
        fs::create_dir_all(&plugin_cache).expect("plugin cache");
        fs::write(plugin_cache.join("LoomleBridge.uplugin"), "{}\n").expect("uplugin");
        fs::write(&version_client, "test-binary\n").expect("version client");

        let switched = switch_to_installed_version(&root, "v0.5.7").expect("switch");
        assert!(switched);
        assert_eq!(
            fs::read_to_string(root.join("bin").join(&client_name)).expect("launcher"),
            "test-binary\n"
        );
        let active = fs::read_to_string(root.join("install").join("active.json")).expect("active");
        assert!(active.contains("\"activeVersion\": \"0.5.7\""));
        let active_bytes =
            fs::read(root.join("install").join("active.json")).expect("active bytes");
        assert_ne!(&active_bytes[..3], &[0xEF, 0xBB, 0xBF]);
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn active_install_state_reader_accepts_utf8_bom() {
        let path = PathBuf::from("active.json");
        let raw = "\u{feff}{\"activeVersion\":\"0.5.36\",\"launcherPath\":\"/tmp/loomle\",\"activeClientPath\":\"/tmp/loomle-0.5.36\"}";

        let value = parse_active_install_state_json(raw, &path).expect("parse");
        let state = active_install_state_from_json(value, &path).expect("state");

        assert_eq!(state.active_version, "0.5.36");
        assert_eq!(state.launcher_path, PathBuf::from("/tmp/loomle"));
        assert_eq!(
            state.active_client_path,
            PathBuf::from("/tmp/loomle-0.5.36")
        );
    }

    #[test]
    fn update_cache_reader_accepts_utf8_bom() {
        let root = std::env::temp_dir().join(format!(
            "loomle-test-update-cache-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        fs::create_dir_all(&root).expect("cache dir");
        let path = root.join("update-check.json");
        fs::write(
            &path,
            format!(
                "\u{feff}{{\"checkedAt\":{},\"latestVersion\":\"0.5.36\"}}\n",
                super::unix_timestamp_secs()
            ),
        )
        .expect("cache");

        assert_eq!(
            read_cached_latest_version(&path, std::time::Duration::from_secs(60)).as_deref(),
            Some("0.5.36")
        );
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn plugin_version_reader_accepts_utf8_bom() {
        let root = std::env::temp_dir().join(format!(
            "loomle-test-uplugin-bom-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        fs::create_dir_all(&root).expect("plugin dir");
        fs::write(
            root.join("LoomleBridge.uplugin"),
            "\u{feff}{\"VersionName\":\"0.5.36\"}\n",
        )
        .expect("uplugin");

        assert_eq!(read_plugin_version(&root).as_deref(), Some("0.5.36"));
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn modules_manifest_build_id_patch_preserves_modules() {
        let root = std::env::temp_dir().join(format!(
            "loomle-test-modules-build-id-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let engine_manifest = root
            .join("Engine")
            .join("Binaries")
            .join("Mac")
            .join("UnrealEditor.modules");
        let plugin_manifest = root
            .join("Project")
            .join("Plugins")
            .join("LoomleBridge")
            .join("Binaries")
            .join("Mac")
            .join("UnrealEditor.modules");
        fs::create_dir_all(engine_manifest.parent().expect("engine parent")).expect("engine dir");
        fs::create_dir_all(plugin_manifest.parent().expect("plugin parent")).expect("plugin dir");
        fs::write(
            &engine_manifest,
            r#"{"BuildId":"engine-build","Modules":{"Core":"UnrealEditor-Core.dylib"}}"#,
        )
        .expect("engine manifest");
        fs::write(
            &plugin_manifest,
            r#"{"BuildId":"old-build","Modules":{"LoomleBridge":"UnrealEditor-LoomleBridge.dylib"}}"#,
        )
        .expect("plugin manifest");

        assert!(patch_plugin_modules_build_id_from_engine_manifest(
            &plugin_manifest,
            &engine_manifest
        )
        .expect("patch"));
        let patched: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(&plugin_manifest).expect("patched manifest"))
                .expect("patched json");
        assert_eq!(patched["BuildId"], "engine-build");
        assert_eq!(
            patched["Modules"]["LoomleBridge"],
            "UnrealEditor-LoomleBridge.dylib"
        );
        assert!(!patch_plugin_modules_build_id_from_engine_manifest(
            &plugin_manifest,
            &engine_manifest
        )
        .expect("second patch"));
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn file_lock_metadata_reader_accepts_utf8_bom() {
        let root = std::env::temp_dir().join(format!(
            "loomle-test-lock-bom-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        fs::create_dir_all(&root).expect("lock dir");
        let path = root.join("update.lock");
        fs::write(
            &path,
            "\u{feff}{\"pid\":123,\"startedAtUnixSecs\":456,\"command\":\"loomle update\"}",
        )
        .expect("lock");

        let metadata = read_file_lock_metadata(&path).expect("metadata");
        assert_eq!(metadata.pid, 123);
        assert_eq!(metadata.started_at_unix_secs, 456);
        assert_eq!(metadata.command, "loomle update");
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn project_support_sync_updates_plugin_from_active_cache() {
        let _env_lock = loomle_install_root_env_lock();
        let root = std::env::temp_dir().join(format!(
            "loomle-test-project-sync-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let previous_root = std::env::var_os("LOOMLE_INSTALL_ROOT");
        std::env::set_var("LOOMLE_INSTALL_ROOT", &root);

        let plugin_cache = root
            .join("versions")
            .join("0.5.8")
            .join("plugin-cache")
            .join("LoomleBridge");
        fs::create_dir_all(&plugin_cache).expect("plugin cache");
        fs::write(
            plugin_cache.join("LoomleBridge.uplugin"),
            r#"{"VersionName":"0.5.8"}"#,
        )
        .expect("cached uplugin");

        let project_root = root.join("Projects").join("Game");
        let installed_plugin = project_root.join("Plugins").join("LoomleBridge");
        fs::create_dir_all(&installed_plugin).expect("installed plugin");
        fs::write(project_root.join("Game.uproject"), "{}\n").expect("uproject");
        fs::write(
            installed_plugin.join("LoomleBridge.uplugin"),
            r#"{"VersionName":"0.5.7"}"#,
        )
        .expect("old uplugin");

        let outcome = sync_project_support_to_version(&project_root, "0.5.8", false).expect("sync");
        assert!(outcome.changed);
        assert_eq!(outcome.previous_version.as_deref(), Some("0.5.7"));
        assert_eq!(
            read_plugin_version(&installed_plugin).as_deref(),
            Some("0.5.8")
        );
        let registration = fs::read_to_string(super::project_registry_path(&project_root))
            .expect("project registration");
        assert!(registration.contains("\"projectId\""));
        assert!(registration.contains("\"pluginVersion\": \"0.5.8\""));
        assert!(fs::read_to_string(
            project_root
                .join("Config")
                .join("DefaultEditorSettings.ini")
        )
        .expect("settings")
        .contains("bThrottleCPUWhenNotForeground=False"));

        let unchanged =
            sync_project_support_to_version(&project_root, "0.5.8", false).expect("sync");
        assert!(!unchanged.changed);
        assert!(!unchanged.requires_editor_restart);

        let stale_binaries = installed_plugin.join("Binaries").join("Mac");
        fs::create_dir_all(&stale_binaries).expect("stale binaries dir");
        fs::write(
            stale_binaries.join("UnrealEditor.modules"),
            r#"{"BuildId":"old","Modules":{"LoomleBridge":"UnrealEditor-LoomleBridge.dylib"}}"#,
        )
        .expect("stale modules manifest");
        let cleaned = sync_project_support_to_version(&project_root, "0.5.8", false).expect("sync");
        assert!(cleaned.changed);
        assert!(cleaned.requires_editor_restart);
        let stale_manifest = stale_binaries.join("UnrealEditor.modules");
        if stale_manifest.exists() {
            assert_ne!(
                read_modules_manifest_build_id(&stale_manifest).as_deref(),
                Some("old")
            );
        } else {
            assert!(!installed_plugin.join("Binaries").exists());
        }

        let unchanged_after_clean =
            sync_project_support_to_version(&project_root, "0.5.8", false).expect("sync");
        assert!(!unchanged_after_clean.changed);
        assert!(!unchanged_after_clean.requires_editor_restart);

        if let Some(previous_root) = previous_root {
            std::env::set_var("LOOMLE_INSTALL_ROOT", previous_root);
        } else {
            std::env::remove_var("LOOMLE_INSTALL_ROOT");
        }
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn project_support_sync_patches_installed_build_modules_manifest() {
        let Some(platform_dir) = current_unreal_binary_platform_dir() else {
            return;
        };
        let _env_lock = loomle_install_root_env_lock();
        let root = std::env::temp_dir().join(format!(
            "loomle-test-project-modules-sync-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let previous_root = std::env::var_os("LOOMLE_INSTALL_ROOT");
        let previous_engine_root = std::env::var_os("LOOMLE_UNREAL_ENGINE_ROOT");
        std::env::set_var("LOOMLE_INSTALL_ROOT", &root);
        let engine_root = root.join("UE");
        std::env::set_var("LOOMLE_UNREAL_ENGINE_ROOT", &engine_root);

        let engine_manifest = engine_root
            .join("Engine")
            .join("Binaries")
            .join(platform_dir)
            .join("UnrealEditor.modules");
        fs::create_dir_all(engine_manifest.parent().expect("engine parent")).expect("engine dir");
        fs::write(
            &engine_manifest,
            r#"{"BuildId":"current-editor-build","Modules":{"Core":"UnrealEditor-Core.dylib"}}"#,
        )
        .expect("engine manifest");

        let plugin_cache = root
            .join("versions")
            .join("0.5.8")
            .join("plugin-cache")
            .join("LoomleBridge");
        let cached_manifest = plugin_cache
            .join("Binaries")
            .join(platform_dir)
            .join("UnrealEditor.modules");
        fs::create_dir_all(cached_manifest.parent().expect("cached parent")).expect("plugin cache");
        fs::write(
            plugin_cache.join("LoomleBridge.uplugin"),
            r#"{"VersionName":"0.5.8"}"#,
        )
        .expect("cached uplugin");
        fs::write(
            &cached_manifest,
            r#"{"BuildId":"stale-release-build","Modules":{"LoomleBridge":"UnrealEditor-LoomleBridge.dylib"}}"#,
        )
        .expect("cached modules manifest");

        let project_root = root.join("Projects").join("Game");
        fs::create_dir_all(&project_root).expect("project");
        fs::write(
            project_root.join("Game.uproject"),
            r#"{"EngineAssociation":"5.7"}"#,
        )
        .expect("uproject");

        let outcome = sync_project_support_to_version(&project_root, "0.5.8", false).expect("sync");
        assert!(outcome.changed);
        let installed_manifest = project_root
            .join("Plugins")
            .join("LoomleBridge")
            .join("Binaries")
            .join(platform_dir)
            .join("UnrealEditor.modules");
        assert_eq!(
            read_modules_manifest_build_id(&installed_manifest).as_deref(),
            Some("current-editor-build")
        );

        if let Some(previous_root) = previous_root {
            std::env::set_var("LOOMLE_INSTALL_ROOT", previous_root);
        } else {
            std::env::remove_var("LOOMLE_INSTALL_ROOT");
        }
        if let Some(previous_engine_root) = previous_engine_root {
            std::env::set_var("LOOMLE_UNREAL_ENGINE_ROOT", previous_engine_root);
        } else {
            std::env::remove_var("LOOMLE_UNREAL_ENGINE_ROOT");
        }
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn project_support_sync_skips_external_managed_plugin() {
        let _env_lock = loomle_install_root_env_lock();
        let root = std::env::temp_dir().join(format!(
            "loomle-test-external-plugin-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let previous_root = std::env::var_os("LOOMLE_INSTALL_ROOT");
        std::env::set_var("LOOMLE_INSTALL_ROOT", &root);

        let plugin_cache = root
            .join("versions")
            .join("0.5.8")
            .join("plugin-cache")
            .join("LoomleBridge");
        fs::create_dir_all(&plugin_cache).expect("plugin cache");
        fs::write(
            plugin_cache.join("LoomleBridge.uplugin"),
            r#"{"VersionName":"0.5.8"}"#,
        )
        .expect("cached uplugin");

        let project_root = root.join("Projects").join("FabGame");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("FabGame.uproject"), "{}\n").expect("uproject");
        let fab_plugin = root.join("FabLibrary").join("LoomleBridge");
        fs::create_dir_all(&fab_plugin).expect("fab plugin");
        fs::write(
            fab_plugin.join("LoomleBridge.uplugin"),
            r#"{"VersionName":"0.5.7"}"#,
        )
        .expect("fab uplugin");

        fs::create_dir_all(super::project_registry_dir()).expect("registry dir");
        fs::write(
            super::project_registry_path(&project_root),
            serde_json::json!({
                "name": "FabGame",
                "projectRoot": project_root.display().to_string(),
                "pluginPath": fab_plugin.display().to_string(),
                "pluginInstallScope": "engine",
                "pluginManagedBy": "fab",
                "pluginVersion": "0.5.7"
            })
            .to_string(),
        )
        .expect("fab project record");

        let outcome = sync_project_support_to_version(&project_root, "0.5.8", true).expect("sync");
        assert!(!outcome.changed);
        assert_eq!(
            outcome.skipped_reason.as_deref(),
            Some("FAB_MANAGED_PLUGIN")
        );
        assert!(!project_root.join("Plugins").join("LoomleBridge").exists());
        assert_eq!(read_plugin_version(&fab_plugin).as_deref(), Some("0.5.7"));

        if let Some(previous_root) = previous_root {
            std::env::set_var("LOOMLE_INSTALL_ROOT", previous_root);
        } else {
            std::env::remove_var("LOOMLE_INSTALL_ROOT");
        }
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn registered_project_sync_updates_offline_and_skips_online_projects() {
        let _env_lock = loomle_install_root_env_lock();
        let root = std::env::temp_dir().join(format!(
            "loomle-test-registered-sync-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let previous_root = std::env::var_os("LOOMLE_INSTALL_ROOT");
        std::env::set_var("LOOMLE_INSTALL_ROOT", &root);

        fs::create_dir_all(root.join("install")).expect("install");
        fs::write(
            root.join("install").join("active.json"),
            r#"{"activeVersion":"0.5.8","launcherPath":"/tmp/loomle","activeClientPath":"/tmp/loomle"}"#,
        )
        .expect("active");
        let plugin_cache = root
            .join("versions")
            .join("0.5.8")
            .join("plugin-cache")
            .join("LoomleBridge");
        fs::create_dir_all(&plugin_cache).expect("plugin cache");
        fs::write(
            plugin_cache.join("LoomleBridge.uplugin"),
            r#"{"VersionName":"0.5.8"}"#,
        )
        .expect("cached uplugin");

        let offline_project = root.join("Projects").join("OfflineGame");
        fs::create_dir_all(offline_project.join("Plugins").join("LoomleBridge"))
            .expect("offline plugin");
        fs::write(offline_project.join("OfflineGame.uproject"), "{}\n").expect("uproject");
        fs::write(
            offline_project
                .join("Plugins")
                .join("LoomleBridge")
                .join("LoomleBridge.uplugin"),
            r#"{"VersionName":"0.5.7"}"#,
        )
        .expect("old plugin");

        let online_project = root.join("Projects").join("OnlineGame");
        let online_endpoint = online_project.join("Intermediate").join("loomle.sock");
        fs::create_dir_all(online_endpoint.parent().expect("endpoint parent")).expect("endpoint");
        fs::write(&online_endpoint, "").expect("endpoint marker");
        fs::create_dir_all(online_project.join("Plugins").join("LoomleBridge"))
            .expect("online plugin");
        fs::write(online_project.join("OnlineGame.uproject"), "{}\n").expect("uproject");
        fs::write(
            online_project
                .join("Plugins")
                .join("LoomleBridge")
                .join("LoomleBridge.uplugin"),
            r#"{"VersionName":"0.5.7"}"#,
        )
        .expect("old plugin");

        let projects = root.join("state").join("projects");
        let runtimes = root.join("state").join("runtimes");
        fs::create_dir_all(&projects).expect("projects");
        fs::create_dir_all(&runtimes).expect("runtimes");
        fs::write(
            projects.join(format!(
                "{}.json",
                super::stable_project_id(&offline_project)
            )),
            serde_json::json!({
                "name": "OfflineGame",
                "projectRoot": offline_project,
                "pluginVersion": "0.5.7"
            })
            .to_string(),
        )
        .expect("offline record");
        fs::write(
            projects.join(format!(
                "{}.json",
                super::stable_project_id(&online_project)
            )),
            serde_json::json!({
                "name": "OnlineGame",
                "projectRoot": online_project,
                "pluginVersion": "0.5.7"
            })
            .to_string(),
        )
        .expect("online project record");
        fs::write(
            runtimes.join(format!(
                "{}.json",
                super::stable_project_id(&online_project)
            )),
            serde_json::json!({
                "name": "OnlineGame",
                "projectRoot": online_project,
                "endpoint": online_endpoint,
                "pluginVersion": "0.5.7"
            })
            .to_string(),
        )
        .expect("online record");

        let summary = sync_registered_project_support();
        assert_eq!(summary.updated, 1);
        assert_eq!(summary.unchanged, 0);
        assert_eq!(summary.skipped_online, 1);
        assert_eq!(summary.failed, 0);
        assert_eq!(
            read_plugin_version(&offline_project.join("Plugins").join("LoomleBridge")).as_deref(),
            Some("0.5.8")
        );
        assert_eq!(
            read_plugin_version(&online_project.join("Plugins").join("LoomleBridge")).as_deref(),
            Some("0.5.7")
        );

        if let Some(previous_root) = previous_root {
            std::env::set_var("LOOMLE_INSTALL_ROOT", previous_root);
        } else {
            std::env::remove_var("LOOMLE_INSTALL_ROOT");
        }
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn file_lock_is_removed_on_drop() {
        let root = std::env::temp_dir().join(format!(
            "loomle-test-lock-drop-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let lock_path = root.join("locks").join("update.lock");
        {
            let _lock = acquire_file_lock(&lock_path, "busy").expect("lock");
            assert!(lock_path.exists());
        }
        assert!(!lock_path.exists());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn file_lock_recovers_stale_dead_pid() {
        let root = std::env::temp_dir().join(format!(
            "loomle-test-lock-stale-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let lock_path = root.join("locks").join("update.lock");
        fs::create_dir_all(lock_path.parent().expect("parent")).expect("lock dir");
        fs::write(
            &lock_path,
            serde_json::to_string(&FileLockMetadata {
                pid: u32::MAX,
                started_at_unix_secs: 1,
                command: "loomle update".to_string(),
            })
            .expect("metadata"),
        )
        .expect("write stale lock");

        let _lock = acquire_file_lock(&lock_path, "busy").expect("stale lock should recover");
        let metadata = read_file_lock_metadata(&lock_path).expect("metadata");
        assert_eq!(metadata.pid, std::process::id());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn file_lock_blocks_active_pid() {
        let root = std::env::temp_dir().join(format!(
            "loomle-test-lock-active-{}-{}",
            std::process::id(),
            super::unix_timestamp_secs()
        ));
        let lock_path = root.join("locks").join("update.lock");
        let _lock = acquire_file_lock(&lock_path, "another loomle update is already running")
            .expect("first lock");

        let error = acquire_file_lock(&lock_path, "another loomle update is already running")
            .expect_err("second lock should fail");
        assert!(error.contains("another loomle update is already running"));
        assert!(error.contains("pid="));
        assert!(error.contains(lock_path.to_string_lossy().as_ref()));
        let _ = fs::remove_dir_all(root);
    }

    fn online_project(project_root: &str) -> RuntimeProject {
        let root = PathBuf::from(project_root);
        RuntimeProject {
            project_id: project_root.to_string(),
            name: root
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or(project_root)
                .to_string(),
            project_root: root.clone(),
            uproject: None,
            endpoint: root.join("Intermediate").join("loomle.sock"),
            status: "online".to_string(),
            attachable: true,
            plugin_installed: true,
            plugin_path: Some(root.join("Plugins").join("LoomleBridge")),
            plugin_install_scope: Some("project".to_string()),
            plugin_managed_by: Some("native".to_string()),
            plugin_version: Some("0.5.4".to_string()),
            protocol_version: Some(1),
            last_seen_at: None,
            reason: None,
        }
    }

    #[test]
    fn infer_attached_project_prefers_explicit_project() {
        let explicit = Some(PathBuf::from("/Projects/Explicit"));
        let inferred = infer_attached_project_root(
            explicit.clone(),
            Some(PathBuf::from("/Projects/StackBot/Content")),
            &[online_project("/Projects/StackBot")],
        );
        assert_eq!(inferred, explicit);
    }

    #[test]
    fn infer_attached_project_uses_cwd_match() {
        let inferred = infer_attached_project_root(
            None,
            Some(PathBuf::from("/Projects/StackBot/Plugins/LoomleBridge")),
            &[
                online_project("/Projects/OtherGame"),
                online_project("/Projects/StackBot"),
            ],
        );
        assert_eq!(inferred, Some(PathBuf::from("/Projects/StackBot")));
    }

    #[test]
    fn infer_attached_project_falls_back_to_single_online_project() {
        let inferred = infer_attached_project_root(
            None,
            Some(PathBuf::from("/Users/example")),
            &[online_project("/Projects/StackBot")],
        );
        assert_eq!(inferred, Some(PathBuf::from("/Projects/StackBot")));
    }

    #[test]
    fn infer_attached_project_requires_manual_choice_when_ambiguous() {
        let inferred = infer_attached_project_root(
            None,
            Some(PathBuf::from("/Users/example")),
            &[
                online_project("/Projects/StackBot"),
                online_project("/Projects/OtherGame"),
            ],
        );
        assert_eq!(inferred, None);
    }

    #[test]
    fn blueprint_graph_edit_translates_graph_id_to_graph_ref() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "id": "graph-guid" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "removeNode",
                    "node": { "id": "node-guid" }
                }
            ]),
        );

        let translated = translate_blueprint_graph_edit_args(&args).expect("translated args");
        assert_eq!(translated.get("graphName"), None);
        assert_eq!(
            translated
                .get("graphRef")
                .and_then(|value| value.get("graphId"))
                .and_then(|value| value.as_str()),
            Some("graph-guid")
        );
    }

    #[test]
    fn blueprint_graph_edit_rejects_legacy_ops() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "ops".into(),
            serde_json::json!([
                {
                    "op": "removeNode",
                    "args": { "nodeId": "node-guid" }
                }
            ]),
        );

        assert!(translate_blueprint_graph_edit_args(&args).is_err());
    }

    #[test]
    fn blueprint_graph_edit_translates_add_from_palette_command() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "addFromPalette",
                    "alias": "branch",
                        "entry": {
                            "id": "palette:abc123",
                            "contextSensitive": false,
                            "context": {
                                "selectedObjects": [
                                    { "kind": "component_property", "name": "BottomWorldNavButton" }
                                ]
                            }
                        },
                        "position": { "x": 320, "y": 160 },
                        "eventName": "OnClicked_BottomWorldNav"
                    }
            ]),
        );

        let translated = translate_blueprint_graph_edit_args(&args).expect("translated args");
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(ops.len(), 1);
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("addFromPalette")
        );
        assert_eq!(
            ops[0].get("clientRef").and_then(|value| value.as_str()),
            Some("branch")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("entryId"))
                .and_then(|value| value.as_str()),
            Some("palette:abc123")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("entry"))
                .and_then(|value| value.get("id"))
                .and_then(|value| value.as_str()),
            Some("palette:abc123")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("contextSensitive"))
                .and_then(|value| value.as_bool()),
            Some(false)
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("context"))
                .and_then(|value| value.get("selectedObjects"))
                .and_then(|value| value.as_array())
                .and_then(|items| items.first())
                .and_then(|value| value.get("name"))
                .and_then(|value| value.as_str()),
            Some("BottomWorldNavButton")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("eventName"))
                .and_then(|value| value.as_str()),
            Some("OnClicked_BottomWorldNav")
        );
    }

    #[test]
    fn blueprint_graph_edit_translates_exec_chain_commands() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "insertExec",
                    "from": { "node": { "id": "node-a" }, "pin": "then" },
                    "node": { "alias": "new-step" },
                    "to": { "node": { "id": "node-b" }, "pin": "execute" }
                },
                {
                    "kind": "bypassExec",
                    "node": { "id": "new-step-id" }
                }
            ]),
        );

        let translated = translate_blueprint_graph_edit_args(&args).expect("translated args");
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(ops.len(), 2);
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("insertExec")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("inputPin"))
                .and_then(|value| value.as_str()),
            Some("execute")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("outputPin"))
                .and_then(|value| value.as_str()),
            Some("then")
        );
        assert_eq!(
            ops[1].get("op").and_then(|value| value.as_str()),
            Some("bypassExec")
        );
        assert_eq!(
            ops[1]
                .get("args")
                .and_then(|value| value.get("node"))
                .and_then(|value| value.get("nodeId"))
                .and_then(|value| value.as_str()),
            Some("new-step-id")
        );
    }

    #[test]
    fn blueprint_graph_palette_schema_and_args_are_public_shape() {
        let schema = blueprint_palette_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("graph"));
        assert!(properties.contains_key("context"));
        assert!(!properties.contains_key("graphName"));
        assert_eq!(
            properties
                .get("limit")
                .and_then(|value| value.get("maximum"))
                .and_then(|value| value.as_i64()),
            Some(500)
        );

        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert("limit".into(), serde_json::json!(500));
        args.insert(
            "fromPins".into(),
            serde_json::json!([{ "node": { "id": "node-1" }, "pin": "Then" }]),
        );
        args.insert(
            "context".into(),
            serde_json::json!({
                "selectedObjects": [
                    { "kind": "component_property", "name": "BottomWorldNavButton" }
                ]
            }),
        );
        let translated = translate_blueprint_palette_args(&args).expect("translated args");
        assert_eq!(
            translated.get("graphName").and_then(|value| value.as_str()),
            Some("EventGraph")
        );
        assert_eq!(
            translated.get("fromPins"),
            Some(&serde_json::json!([{ "nodeId": "node-1", "pin": "Then" }]))
        );
        assert_eq!(
            translated.get("context"),
            Some(&serde_json::json!({
                "selectedObjects": [
                    { "kind": "component_property", "name": "BottomWorldNavButton" }
                ]
            }))
        );

        args.insert("limit".into(), serde_json::json!(501));
        let error = translate_blueprint_palette_args(&args).expect_err("limit rejected");
        assert_eq!(
            error
                .structured_content
                .and_then(|value| value.get("code").cloned())
                .and_then(|value| value.as_str().map(str::to_string)),
            Some("INVALID_ARGUMENT".to_string())
        );
    }

    #[test]
    fn blueprint_graph_edit_schema_uses_lightweight_command_envelope() {
        let schema = super::blueprint_graph_edit_schema();
        let properties = schema
            .get("properties")
            .and_then(|properties| properties.as_object())
            .expect("properties");
        assert!(properties.contains_key("graph"));
        assert!(!properties.contains_key("graphName"));
        assert!(!properties.contains_key("continueOnError"));
        assert!(!properties.contains_key("returnDiff"));
        assert!(!properties.contains_key("returnDiagnostics"));
        assert_eq!(
            schema
                .get("required")
                .and_then(|value| value.as_array())
                .map(|values| values
                    .iter()
                    .filter_map(|value| value.as_str())
                    .collect::<std::collections::BTreeSet<_>>()),
            Some(["assetPath", "commands", "graph"].into_iter().collect())
        );
        let command_properties = schema
            .get("properties")
            .and_then(|properties| properties.get("commands"))
            .and_then(|commands| commands.get("items"))
            .and_then(|items| items.get("properties"))
            .and_then(|properties| properties.as_object())
            .expect("command properties");

        assert!(command_properties.contains_key("kind"));
        assert!(command_properties.contains_key("alias"));
        assert!(!command_properties.contains_key("entry"));
        assert!(!command_properties.contains_key("from"));
        assert!(!command_properties.contains_key("target"));
    }

    #[test]
    fn blueprint_graph_edit_rejects_public_legacy_graph_fields() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graphName".into(), serde_json::json!("EventGraph"));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "removeNode",
                    "node": { "id": "node-guid" }
                }
            ]),
        );

        assert!(translate_blueprint_graph_edit_args(&args).is_err());
    }

    #[test]
    fn blueprint_graph_edit_preserves_structured_pin_default_object() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "setPinDefault",
                    "target": {
                        "node": { "alias": "spawn" },
                        "pin": "Class"
                    },
                    "value": {
                        "object": "/Game/BP_Coin.BP_Coin_C"
                    }
                }
            ]),
        );

        let translated = translate_blueprint_graph_edit_args(&args).expect("translated args");
        let op_args = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .and_then(|ops| ops.first())
            .and_then(|op| op.get("args"))
            .expect("op args");
        assert_eq!(
            op_args.get("object").and_then(|value| value.as_str()),
            Some("/Game/BP_Coin.BP_Coin_C")
        );
        assert!(op_args.get("value").is_none());
    }

    #[test]
    fn blueprint_graph_edit_reconstruct_node_translates_to_op() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "reconstructNode",
                    "node": { "id": "node-guid" },
                    "preserveLinks": true
                }
            ]),
        );

        let translated = translate_blueprint_graph_edit_args(&args).expect("translated args");
        let op = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .and_then(|ops| ops.first())
            .expect("op");
        assert_eq!(
            op.get("op").and_then(|value| value.as_str()),
            Some("reconstructNode")
        );
        assert_eq!(
            op.get("args")
                .and_then(|value| value.get("nodeId"))
                .and_then(|value| value.as_str()),
            Some("node-guid")
        );
    }

    #[test]
    fn blueprint_graph_inspect_summary_returns_roots_and_chain_summaries() {
        let mut result = serde_json::json!({
            "semanticSnapshot": {
                "nodes": [
                    {
                        "id": "event",
                        "guid": "event",
                        "className": "K2Node_Event",
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_Event",
                        "title": "Event BeginPlay",
                        "pins": [
                            {
                                "name": "Then",
                                "direction": "output",
                                "category": "exec",
                                "linkedTo": [{ "nodeGuid": "print", "pin": "execute" }]
                            }
                        ]
                    },
                    {
                        "id": "print",
                        "guid": "print",
                        "className": "K2Node_CallFunction",
                        "classPath": "/Script/BlueprintGraph.K2Node_CallFunction",
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_CallFunction",
                        "path": "/Game/BP.BP:EventGraph.Node",
                        "title": "Print String",
                        "nodeTitleFull": "Print String Full",
                        "position": { "x": 10, "y": 20 },
                        "layout": { "x": 10, "y": 20, "source": "model" },
                        "pins": [
                            {
                                "name": "execute",
                                "direction": "input",
                                "category": "exec",
                                "linkedTo": [{ "nodeGuid": "event", "pin": "Then" }]
                            },
                            {
                                "name": "InString",
                                "direction": "input",
                                "category": "string",
                                "defaultValue": "Hello"
                            }
                        ]
                    }
                ],
                "edges": []
            },
            "meta": {}
        })
        .as_object()
        .cloned()
        .expect("object");
        let args = JsonObject::new();

        shape_blueprint_graph_inspect_result(&mut result, &args);

        assert!(result.get("semanticSnapshot").is_none());
        assert_eq!(
            result.get("view").and_then(|value| value.as_str()),
            Some("summary")
        );
        let root = result
            .get("roots")
            .and_then(|value| value.as_array())
            .and_then(|nodes| nodes.first())
            .and_then(|value| value.as_object())
            .expect("root");
        assert_eq!(
            root.get("id").and_then(|value| value.as_str()),
            Some("event")
        );
        assert!(!root.contains_key("pins"));
        assert!(result
            .get("nodes")
            .and_then(|value| value.as_object())
            .is_some_and(|nodes| nodes.contains_key("event") && nodes.contains_key("print")));
        let chain = result
            .get("chains")
            .and_then(|value| value.as_array())
            .and_then(|chains| chains.first())
            .and_then(|value| value.as_object())
            .expect("chain");
        assert_eq!(
            chain.get("nodeCount").and_then(|value| value.as_u64()),
            Some(2)
        );
    }

    #[test]
    fn blueprint_graph_inspect_exec_flow_traces_from_root_node() {
        let mut result = serde_json::json!({
            "semanticSnapshot": {
                "nodes": [
                    {
                        "id": "event",
                        "guid": "event",
                        "className": "K2Node_Event",
                        "pins": [
                            {
                                "name": "Then",
                                "direction": "output",
                                "category": "exec",
                                "linkedTo": [{ "nodeGuid": "print", "pin": "execute" }]
                            }
                        ]
                    },
                    {
                        "id": "print",
                        "guid": "print",
                        "className": "K2Node_CallFunction",
                        "title": "Print String",
                        "hasNodeEditCapabilities": true,
                        "inspectWith": "blueprint_node_inspect",
                        "pins": [
                            {
                                "name": "execute",
                                "direction": "input",
                                "category": "exec",
                                "linkedTo": [{ "nodeGuid": "event", "pin": "Then" }]
                            }
                        ]
                    }
                ],
                "edges": []
            },
            "meta": {}
        })
        .as_object()
        .cloned()
        .expect("object");
        let mut args = JsonObject::new();
        args.insert("view".into(), serde_json::json!("exec_flow"));
        args.insert("rootNode".into(), serde_json::json!({"id":"event"}));

        shape_blueprint_graph_inspect_result(&mut result, &args);

        let nodes = result
            .get("nodes")
            .and_then(|value| value.as_array())
            .expect("flow nodes");
        assert_eq!(nodes.len(), 2);
        let print = nodes
            .iter()
            .filter_map(|value| value.as_object())
            .find(|node| node.get("id").and_then(|value| value.as_str()) == Some("print"))
            .expect("print node");
        assert_eq!(
            print.get("inspectWith").and_then(|value| value.as_str()),
            Some("blueprint_node_inspect")
        );
        assert!(print.get("pins").is_none());
        assert_eq!(
            result
                .get("links")
                .and_then(|value| value.as_array())
                .map(Vec::len),
            Some(1)
        );
        assert_eq!(
            result.get("direction").and_then(|value| value.as_str()),
            Some("downstream")
        );
    }

    #[test]
    fn blueprint_graph_inspect_data_flow_traces_pin_sources() {
        let mut result = serde_json::json!({
            "semanticSnapshot": {
                "nodes": [
                    {
                        "id": "make",
                        "guid": "make",
                        "className": "K2Node_CallFunction",
                        "title": "Make String",
                        "pins": [
                            {
                                "name": "ReturnValue",
                                "direction": "output",
                                "category": "string",
                                "linkedTo": [{ "nodeGuid": "print", "pin": "InString" }]
                            }
                        ]
                    },
                    {
                        "id": "print",
                        "guid": "print",
                        "className": "K2Node_CallFunction",
                        "title": "Print String",
                        "pins": [
                            {
                                "name": "InString",
                                "direction": "input",
                                "category": "string",
                                "linkedTo": [{ "nodeGuid": "make", "pin": "ReturnValue" }]
                            }
                        ]
                    }
                ],
                "edges": []
            },
            "meta": {}
        })
        .as_object()
        .cloned()
        .expect("object");
        let mut args = JsonObject::new();
        args.insert("view".into(), serde_json::json!("data_flow"));
        args.insert(
            "rootPin".into(),
            serde_json::json!({"node":{"id":"print"},"pin":"InString"}),
        );

        shape_blueprint_graph_inspect_result(&mut result, &args);

        let links = result
            .get("links")
            .and_then(|value| value.as_array())
            .expect("data links");
        assert_eq!(links.len(), 1);
        assert_eq!(
            links[0].get("fromNodeId").and_then(|value| value.as_str()),
            Some("make")
        );
        assert_eq!(
            result.get("view").and_then(|value| value.as_str()),
            Some("data_flow")
        );
    }

    #[test]
    fn blueprint_graph_inspect_flow_targets_return_actionable_errors() {
        let result = serde_json::json!({
            "semanticSnapshot": {
                "nodes": [
                    {
                        "id": "event",
                        "guid": "event",
                        "className": "K2Node_Event",
                        "pins": [
                            {"name": "Then", "direction": "output", "category": "exec"}
                        ]
                    }
                ],
                "edges": []
            }
        })
        .as_object()
        .cloned()
        .expect("object");

        let mut exec_args = JsonObject::new();
        exec_args.insert("view".into(), serde_json::json!("exec_flow"));
        exec_args.insert("rootNode".into(), serde_json::json!({"id":"missing"}));
        let exec_error =
            validate_blueprint_graph_inspect_targets(&result, &exec_args).expect("exec error");
        assert_eq!(
            exec_error
                .structured_content
                .and_then(|value| value.get("code").cloned())
                .and_then(|value| value.as_str().map(str::to_string)),
            Some("NODE_NOT_FOUND".to_string())
        );

        let mut data_args = JsonObject::new();
        data_args.insert("view".into(), serde_json::json!("data_flow"));
        data_args.insert(
            "rootPin".into(),
            serde_json::json!({"node":{"id":"event"},"pin":"MissingPin"}),
        );
        let data_error =
            validate_blueprint_graph_inspect_targets(&result, &data_args).expect("pin error");
        assert_eq!(
            data_error
                .structured_content
                .and_then(|value| value.get("code").cloned())
                .and_then(|value| value.as_str().map(str::to_string)),
            Some("PIN_NOT_FOUND".to_string())
        );
    }

    #[test]
    fn blueprint_graph_inspect_schema_declares_flow_views_and_roots() {
        let schema = blueprint_graph_inspect_schema();
        let view_enum = schema
            .get("properties")
            .and_then(|value| value.get("view"))
            .and_then(|value| value.get("enum"))
            .and_then(|value| value.as_array())
            .expect("view enum");
        let values = view_enum
            .iter()
            .filter_map(|value| value.as_str())
            .collect::<std::collections::HashSet<_>>();
        assert!(values.contains("summary"));
        assert!(values.contains("exec_flow"));
        assert!(values.contains("data_flow"));
        assert_eq!(values.len(), 3);
        assert!(schema
            .get("properties")
            .and_then(|value| value.as_object())
            .is_some_and(|properties| properties.contains_key("rootNode")
                && properties.contains_key("rootPin")
                && properties.contains_key("traversal")
                && !properties.contains_key("filter")
                && !properties.contains_key("page")
                && !properties.contains_key("graphName")
                && !properties.contains_key("nodeIds")
                && !properties.contains_key("nodeClasses")
                && !properties.contains_key("limit")
                && !properties.contains_key("cursor")
                && !properties.contains_key("detail")
                && !properties.contains_key("includePinDefaults")));
    }

    #[test]
    fn blueprint_graph_inspect_translates_to_full_connected_runtime_snapshot() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({"name":"EventGraph"}));
        args.insert("view".into(), serde_json::json!("summary"));

        let translated = translate_blueprint_graph_inspect_args(&args).expect("translated args");
        assert_eq!(
            translated.get("graphName").and_then(|value| value.as_str()),
            Some("EventGraph")
        );
        assert_eq!(
            translated.get("limit").and_then(|value| value.as_i64()),
            Some(10000)
        );
        assert_eq!(
            translated
                .get("includeConnections")
                .and_then(|value| value.as_bool()),
            Some(true)
        );
        assert_eq!(
            translated
                .get("includePinDefaults")
                .and_then(|value| value.as_bool()),
            None
        );
    }

    #[test]
    fn blueprint_graph_inspect_rejects_legacy_top_level_query_args() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({"name":"EventGraph"}));
        args.insert("graphName".into(), serde_json::json!("EventGraph"));

        let error = validate_blueprint_graph_inspect_args(&args).expect_err("legacy arg rejected");
        let payload = error.structured_content.expect("structured content");
        assert_eq!(
            payload.get("code").and_then(|value| value.as_str()),
            Some("INVALID_ARGUMENT")
        );
    }

    #[test]
    fn blueprint_graph_inspect_requires_roots_for_flow_views() {
        let mut exec_args = JsonObject::new();
        exec_args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        exec_args.insert("graph".into(), serde_json::json!({"name":"EventGraph"}));
        exec_args.insert("view".into(), serde_json::json!("exec_flow"));
        assert!(validate_blueprint_graph_inspect_args(&exec_args).is_err());

        let mut data_args = JsonObject::new();
        data_args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        data_args.insert("graph".into(), serde_json::json!({"name":"EventGraph"}));
        data_args.insert("view".into(), serde_json::json!("data_flow"));
        assert!(validate_blueprint_graph_inspect_args(&data_args).is_err());

        exec_args.insert("rootNode".into(), serde_json::json!({"id":"event"}));
        data_args.insert(
            "rootPin".into(),
            serde_json::json!({"node":{"id":"print"},"pin":"InString"}),
        );
        assert!(validate_blueprint_graph_inspect_args(&exec_args).is_ok());
        assert!(validate_blueprint_graph_inspect_args(&data_args).is_ok());

        exec_args.insert("traversal".into(), serde_json::json!({"maxDepth": 129}));
        let error =
            validate_blueprint_graph_inspect_args(&exec_args).expect_err("maxDepth rejected");
        assert_eq!(
            error
                .structured_content
                .and_then(|value| value.get("code").cloned())
                .and_then(|value| value.as_str().map(str::to_string)),
            Some("INVALID_ARGUMENT".to_string())
        );
    }

    #[test]
    fn blueprint_node_inspect_schema_is_single_layer() {
        let schema = blueprint_node_inspect_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("assetPath"));
        assert!(properties.contains_key("graph"));
        assert!(properties.contains_key("node"));
        let schema_text = serde_json::to_string(&schema).expect("schema text");
        assert!(!schema_text.contains("schema_inspect"));
    }

    #[test]
    fn blueprint_node_edit_schema_points_to_schema_inspect() {
        let schema = blueprint_node_edit_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties
            .get("operation")
            .and_then(|value| value.get("description"))
            .and_then(|value| value.as_str())
            .is_some_and(|description| description.contains("schema_inspect")));
        assert!(properties
            .get("args")
            .and_then(|value| value.get("description"))
            .and_then(|value| value.as_str())
            .is_some_and(|description| description.contains("schema_inspect")));
        assert!(!properties.contains_key("returnDiff"));
        assert!(!properties.contains_key("returnDiagnostics"));
        assert!(properties.contains_key("expectedRevision"));
    }

    #[test]
    fn blueprint_node_inspect_translates_graph_and_node_ref() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({"name":"EventGraph"}));
        args.insert("node".into(), serde_json::json!({"id":"node-1"}));

        let translated = translate_blueprint_node_inspect_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/BP_Test")
        );
        assert_eq!(
            translated.get("graphName").and_then(|value| value.as_str()),
            Some("EventGraph")
        );
        assert_eq!(
            translated.get("nodeId").and_then(|value| value.as_str()),
            Some("node-1")
        );
    }

    #[test]
    fn blueprint_node_edit_translates_operation_args_and_controls() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({"id": "graph-guid"}));
        args.insert("node".into(), serde_json::json!({"id": "node-guid"}));
        args.insert("operation".into(), serde_json::json!("addPin"));
        args.insert(
            "args".into(),
            serde_json::json!({"role": "case", "name": "Paused"}),
        );
        args.insert("dryRun".into(), serde_json::json!(true));
        args.insert("returnDiff".into(), serde_json::json!(true));
        args.insert("returnDiagnostics".into(), serde_json::json!(true));

        let translated = translate_blueprint_node_edit_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/BP_Test")
        );
        assert_eq!(
            translated.get("nodeId").and_then(|value| value.as_str()),
            Some("node-guid")
        );
        assert_eq!(
            translated.get("operation").and_then(|value| value.as_str()),
            Some("addPin")
        );
        assert_eq!(
            translated.get("dryRun").and_then(|value| value.as_bool()),
            Some(true)
        );
        assert!(translated.get("graphRef").is_some());
        assert!(translated.get("returnDiff").is_none());
        assert!(translated.get("returnDiagnostics").is_none());
    }

    #[test]
    fn material_graph_inspect_accepts_child_graph_ref_as_graph() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "asset",
                "assetPath": "/Game/MF_Test"
            }),
        );
        args.insert("includeConnections".into(), serde_json::json!(true));

        let translated = translate_material_graph_inspect_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/MF_Test")
        );
        assert_eq!(
            translated
                .get("includeConnections")
                .and_then(|value| value.as_bool()),
            Some(true)
        );
    }

    #[test]
    fn material_graph_inspect_rejects_inline_graph_ref() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "inline",
                "assetPath": "/Game/MF_Test",
                "nodeGuid": "node-guid"
            }),
        );

        assert!(translate_material_graph_inspect_args(&args).is_err());
    }

    #[test]
    fn material_graph_inspect_schema_has_openai_compatible_top_level() {
        let schema = material_graph_inspect_schema();
        assert_eq!(
            schema.get("type").and_then(|value| value.as_str()),
            Some("object")
        );
        for keyword in ["oneOf", "anyOf", "allOf", "enum", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "material_graph_inspect schema should not expose top-level {keyword}"
            );
        }
    }

    #[test]
    fn asset_inspect_schema_declares_supported_asset_kinds() {
        let schema = asset_inspect_schema();
        let kind_values = schema
            .get("properties")
            .and_then(|properties| properties.get("kind"))
            .and_then(|kind| kind.get("enum"))
            .and_then(|value| value.as_array())
            .expect("kind enum")
            .iter()
            .filter_map(|value| value.as_str())
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "blueprint",
            "enum",
            "userDefinedStruct",
            "material",
            "materialFunction",
            "pcgGraph",
            "widgetBlueprint",
        ] {
            assert!(
                kind_values.contains(expected),
                "missing asset kind {expected}"
            );
        }

        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("view"));
        assert!(properties.contains_key("filter"));
        assert!(properties.contains_key("includeConnections"));
    }

    #[test]
    fn asset_create_schema_declares_supported_asset_kinds() {
        let schema = asset_create_schema();
        let kind_values = schema
            .get("properties")
            .and_then(|properties| properties.get("kind"))
            .and_then(|kind| kind.get("enum"))
            .and_then(|value| value.as_array())
            .expect("kind enum")
            .iter()
            .filter_map(|value| value.as_str())
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "blueprint",
            "enum",
            "material",
            "materialFunction",
            "pcgGraph",
            "widgetBlueprint",
        ] {
            assert!(
                kind_values.contains(expected),
                "missing asset kind {expected}"
            );
        }
    }

    #[test]
    fn asset_edit_schema_centers_metadata_editing() {
        let schema = asset_edit_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");

        assert!(properties.contains_key("metadata"));
        assert!(properties.contains_key("removeKeys"));
        assert!(properties.contains_key("clearMetadata"));

        let operations = properties
            .get("operation")
            .and_then(|value| value.get("enum"))
            .and_then(|value| value.as_array())
            .expect("operation enum")
            .iter()
            .filter_map(|value| value.as_str())
            .collect::<std::collections::HashSet<_>>();
        assert!(operations.contains("updateMetadata"));
        assert!(operations.contains("updateEntries"));
        assert!(operations.contains("addField"));
        assert!(operations.contains("changeFieldType"));

        let required = schema
            .get("required")
            .and_then(|value| value.as_array())
            .expect("required")
            .iter()
            .filter_map(|value| value.as_str())
            .collect::<std::collections::HashSet<_>>();
        assert!(!required.contains("kind"));
    }

    #[test]
    fn pcg_graph_inspect_accepts_child_graph_ref_as_graph() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "asset",
                "assetPath": "/Game/PCG_Child"
            }),
        );
        args.insert("view".into(), serde_json::json!("links"));

        let translated = translate_pcg_graph_inspect_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/PCG_Child")
        );
        assert_eq!(
            translated
                .get("includeConnections")
                .and_then(|value| value.as_bool()),
            Some(true)
        );
    }

    #[test]
    fn pcg_graph_inspect_rejects_inline_graph_ref() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "inline",
                "assetPath": "/Game/PCG_Parent",
                "nodeGuid": "node-guid"
            }),
        );

        assert!(translate_pcg_graph_inspect_args(&args).is_err());
    }

    #[test]
    fn pcg_graph_inspect_schema_has_openai_compatible_top_level() {
        let schema = pcg_graph_inspect_schema();
        assert_eq!(
            schema.get("type").and_then(|value| value.as_str()),
            Some("object")
        );
        for keyword in ["oneOf", "anyOf", "allOf", "enum", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "pcg_graph_inspect schema should not expose top-level {keyword}"
            );
        }
    }

    #[test]
    fn pcg_graph_inspect_schema_declares_view_filter_and_page() {
        let schema = pcg_graph_inspect_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("view"));
        assert!(properties.contains_key("filter"));
        assert!(properties.contains_key("page"));
        assert!(!properties.contains_key("nodeIds"));
        assert!(!properties.contains_key("nodeClasses"));
    }

    #[test]
    fn pcg_graph_inspect_translates_view_filter_and_rejects_legacy_args() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/PCG_Test"));
        args.insert("view".into(), serde_json::json!("defaults"));
        args.insert(
            "filter".into(),
            serde_json::json!({
                "nodeIds": ["node-1"],
                "text": "spawn"
            }),
        );

        let translated = translate_pcg_graph_inspect_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/PCG_Test")
        );
        assert_eq!(
            translated
                .get("includeConnections")
                .and_then(|value| value.as_bool()),
            Some(true)
        );
        assert_eq!(
            translated
                .get("nodeIds")
                .and_then(|value| value.as_array())
                .map(|items| items.len()),
            Some(1)
        );

        args.insert("nodeIds".into(), serde_json::json!(["node-1"]));
        let error = validate_pcg_graph_inspect_args(&args).expect_err("legacy arg rejected");
        let payload = error.structured_content.expect("structured content");
        assert_eq!(
            payload.get("code").and_then(|value| value.as_str()),
            Some("INVALID_ARGUMENT")
        );
    }

    #[test]
    fn pcg_graph_inspect_overview_prunes_pins_and_edges() {
        let mut result = serde_json::json!({
            "semanticSnapshot": {
                "nodes": [
                    {
                        "id": "node-1",
                        "nodeClassPath": "/Script/PCG.TestSettings",
                        "title": "Spawn Points",
                        "pins": [
                            {
                                "name": "Input",
                                "direction": "input",
                                "defaultValue": "42",
                                "default": { "value": "42" },
                                "links": [{ "toNodeId": "node-2", "toPin": "Output" }]
                            }
                        ],
                        "settings": {"properties": []}
                    }
                ],
                "edges": [{ "fromNodeId": "node-2", "toNodeId": "node-1" }]
            },
            "meta": {}
        })
        .as_object()
        .cloned()
        .expect("object");
        let mut args = JsonObject::new();
        args.insert("view".into(), serde_json::json!("overview"));

        shape_pcg_graph_inspect_result(&mut result, &args);
        let node = result
            .get("semanticSnapshot")
            .and_then(|value| value.get("nodes"))
            .and_then(|value| value.as_array())
            .and_then(|nodes| nodes.first())
            .and_then(|value| value.as_object())
            .expect("node");
        assert!(!node.contains_key("pins"));
        assert_eq!(
            result
                .get("semanticSnapshot")
                .and_then(|value| value.get("edges"))
                .and_then(|value| value.as_array())
                .map(|edges| edges.len()),
            Some(0)
        );
    }

    #[test]
    fn pcg_graph_inspect_defaults_keeps_pin_defaults_and_links() {
        let mut result = serde_json::json!({
            "semanticSnapshot": {
                "nodes": [
                    {
                        "id": "node-1",
                        "nodeClassPath": "/Script/PCG.TestSettings",
                        "title": "Spawn Points",
                        "pins": [
                            {
                                "name": "Input",
                                "direction": "input",
                                "defaultValue": "42",
                                "default": { "value": "42" },
                                "links": [{ "toNodeId": "node-2", "toPin": "Output" }]
                            }
                        ]
                    }
                ],
                "edges": [{ "fromNodeId": "node-2", "toNodeId": "node-1" }]
            },
            "meta": {}
        })
        .as_object()
        .cloned()
        .expect("object");
        let mut args = JsonObject::new();
        args.insert("view".into(), serde_json::json!("defaults"));

        shape_pcg_graph_inspect_result(&mut result, &args);
        let pin = result
            .get("semanticSnapshot")
            .and_then(|value| value.get("nodes"))
            .and_then(|value| value.as_array())
            .and_then(|nodes| nodes.first())
            .and_then(|value| value.get("pins"))
            .and_then(|value| value.as_array())
            .and_then(|pins| pins.first())
            .and_then(|value| value.as_object())
            .expect("pin");
        assert!(pin.contains_key("defaultValue"));
        assert!(pin.contains_key("default"));
        assert!(pin.contains_key("links"));
    }

    #[test]
    fn pcg_node_inspect_schema_declares_node_and_class_modes() {
        let schema = pcg_node_inspect_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("assetPath"));
        assert!(properties.contains_key("graph"));
        assert!(properties.contains_key("node"));
        assert!(properties.contains_key("nodeClass"));
        assert!(properties.contains_key("settingsClass"));
    }

    #[test]
    fn pcg_node_inspect_translates_instance_and_class_modes() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/PCG_Test"));
        args.insert("node".into(), serde_json::json!({"id": "node-1"}));

        let translated = translate_pcg_node_inspect_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/PCG_Test")
        );
        assert_eq!(
            translated.get("nodeId").and_then(|value| value.as_str()),
            Some("node-1")
        );

        let mut class_args = JsonObject::new();
        class_args.insert(
            "nodeClass".into(),
            serde_json::json!("/Script/PCG.PCGStaticMeshSpawnerSettings"),
        );
        let class_translated =
            translate_pcg_node_inspect_args(&class_args).expect("class translated args");
        assert_eq!(
            class_translated
                .get("nodeClass")
                .and_then(|value| value.as_str()),
            Some("/Script/PCG.PCGStaticMeshSpawnerSettings")
        );
        assert!(!class_translated.contains_key("assetPath"));
    }

    #[test]
    fn pcg_node_inspect_shapes_instance_properties_for_edit() {
        let payload = serde_json::json!({
            "mode": "instance",
            "assetPath": "/Game/PCG_Test",
            "nodeId": "node-1",
            "node": {
                "id": "node-1",
                "settings": {
                    "properties": [
                        { "name": "Density", "defaultValue": "1.0" }
                    ]
                },
                "pins": [
                    { "name": "Input", "direction": "input" }
                ]
            }
        });

        let shaped = shape_pcg_node_inspect_result(payload);
        assert_eq!(
            shaped.get("mode").and_then(|value| value.as_str()),
            Some("instance")
        );
        assert_eq!(
            shaped
                .get("properties")
                .and_then(|value| value.as_array())
                .map(|items| items.len()),
            Some(1)
        );
        assert_eq!(
            shaped
                .get("pins")
                .and_then(|value| value.as_array())
                .map(|items| items.len()),
            Some(1)
        );
    }

    #[test]
    fn pcg_compile_schema_accepts_asset_or_graph() {
        let schema = pcg_compile_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("assetPath"));
        assert!(properties.contains_key("graph"));
    }

    #[test]
    fn pcg_compile_translates_graph_asset_path() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "asset",
                "assetPath": "/Game/PCG_Test"
            }),
        );

        let translated = translate_pcg_compile_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/PCG_Test")
        );
    }

    #[test]
    fn pcg_compile_shapes_verify_result() {
        let payload = serde_json::json!({
            "assetPath": "/Game/PCG_Test",
            "status": "ok",
            "summary": "PCG verification succeeded.",
            "diagnostics": [],
            "compileReport": {
                "compiled": true,
                "compilationChanged": false
            },
            "queryReport": {
                "revision": "pcg:1234"
            }
        });

        let shaped = shape_pcg_compile_result(payload);
        assert_eq!(
            shaped.get("status").and_then(|value| value.as_str()),
            Some("ok")
        );
        assert_eq!(
            shaped.get("valid").and_then(|value| value.as_bool()),
            Some(true)
        );
        assert_eq!(
            shaped.get("compiled").and_then(|value| value.as_bool()),
            Some(true)
        );
    }

    #[test]
    fn pcg_palette_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("pcg_palette"));
    }

    #[test]
    fn material_palette_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("material_palette"));
    }

    #[test]
    fn widget_palette_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("widget_palette"));
    }

    #[test]
    fn widget_tree_edit_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("widget_tree_edit"));
    }

    #[test]
    fn widget_tree_inspect_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("widget_tree_inspect"));
    }

    #[test]
    fn widget_inspect_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("widget_inspect"));
    }

    #[test]
    fn widget_compile_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("widget_compile"));
    }

    #[test]
    fn widget_event_create_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("widget_event_create"));
    }

    #[test]
    fn public_widget_surface_is_declared_without_legacy_tools() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "widget_palette",
            "widget_tree_inspect",
            "widget_tree_edit",
            "widget_edit",
            "widget_event_create",
            "widget_inspect",
            "widget_compile",
        ] {
            assert!(tool_names.contains(expected), "missing tool {expected}");
        }

        for retired in [
            "widget.query",
            "widget.mutate",
            "widget.verify",
            "widget.describe",
        ] {
            assert!(
                !tool_names.contains(retired),
                "retired widget tool should not be declared: {retired}"
            );
        }
    }

    #[test]
    fn material_graph_edit_tool_replaces_public_mutate_tool() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("material_graph_edit"));
        assert!(tool_names.contains("material_graph_layout"));
        assert!(tool_names.contains("material_graph_inspect"));
        assert!(tool_names.contains("material_compile"));
        assert!(tool_names.contains("material_node_inspect"));
        assert!(tool_names.contains("material_node_edit"));
        assert!(!tool_names.contains("material.query"));
        assert!(!tool_names.contains("material.mutate"));
        assert!(!tool_names.contains("material.verify"));
        assert!(!tool_names.contains("material.describe"));
    }

    #[test]
    fn pcg_graph_inspect_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("pcg_graph_inspect"));
    }

    #[test]
    fn pcg_node_inspect_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("pcg_node_inspect"));
    }

    #[test]
    fn pcg_compile_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("pcg_compile"));
    }

    #[test]
    fn pcg_graph_edit_tool_is_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("pcg_graph_edit"));
        assert!(tool_names.contains("pcg_graph_layout"));
    }

    #[test]
    fn pcg_parameter_tools_are_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("pcg_parameter_inspect"));
        assert!(tool_names.contains("pcg_parameter_edit"));
    }

    #[test]
    fn pcg_parameter_edit_schema_points_to_schema_inspect() {
        let schema = pcg_parameter_edit_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties
            .get("operation")
            .and_then(|value| value.get("description"))
            .and_then(|value| value.as_str())
            .is_some_and(|description| description.contains("schema_inspect")));
        assert!(properties
            .get("args")
            .and_then(|value| value.get("description"))
            .and_then(|value| value.as_str())
            .is_some_and(|description| description.contains("schema_inspect")));
    }

    #[test]
    fn pcg_parameter_edit_translates_operation_args_and_controls() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/PCG_Test"));
        args.insert("operation".into(), serde_json::json!("create"));
        args.insert(
            "args".into(),
            serde_json::json!({"name": "DensityScale", "type": "Double"}),
        );
        args.insert("dryRun".into(), serde_json::json!(true));

        let translated = translate_pcg_parameter_edit_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/PCG_Test")
        );
        assert_eq!(
            translated.get("operation").and_then(|value| value.as_str()),
            Some("create")
        );
        assert_eq!(
            translated.get("dryRun").and_then(|value| value.as_bool()),
            Some(true)
        );
        assert!(translated
            .get("args")
            .is_some_and(|value| value.is_object()));
    }

    #[test]
    fn public_pcg_surface_is_declared_without_legacy_tools() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "pcg_graph_inspect",
            "pcg_node_inspect",
            "pcg_parameter_inspect",
            "pcg_parameter_edit",
            "pcg_palette",
            "pcg_graph_edit",
            "pcg_compile",
        ] {
            assert!(tool_names.contains(expected), "missing tool {expected}");
        }

        for retired in [
            "pcg.list",
            "pcg.query",
            "pcg.mutate",
            "pcg.verify",
            "pcg.describe",
        ] {
            assert!(
                !tool_names.contains(retired),
                "retired PCG tool should not be declared: {retired}"
            );
        }
    }

    #[test]
    fn pcg_palette_schema_is_top_level_simple() {
        let schema = pcg_palette_schema();
        assert_eq!(
            schema.get("type").and_then(|value| value.as_str()),
            Some("object")
        );
        for keyword in ["oneOf", "anyOf", "allOf", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "pcg_palette schema should not expose top-level {keyword}"
            );
        }
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("query"));
        assert!(properties.contains_key("elementTypes"));
    }

    #[test]
    fn material_palette_schema_is_top_level_simple() {
        let schema = material_palette_schema();
        assert_eq!(
            schema.get("type").and_then(|value| value.as_str()),
            Some("object")
        );
        for keyword in ["oneOf", "anyOf", "allOf", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "material_palette schema should not expose top-level {keyword}"
            );
        }
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("query"));
        assert!(properties.contains_key("elementTypes"));
    }

    #[test]
    fn widget_palette_schema_is_top_level_simple() {
        let schema = widget_palette_schema();
        assert_eq!(
            schema.get("type").and_then(|value| value.as_str()),
            Some("object")
        );
        for keyword in ["oneOf", "anyOf", "allOf", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "widget_palette schema should not expose top-level {keyword}"
            );
        }
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("query"));
        assert!(properties.contains_key("elementTypes"));
    }

    #[test]
    fn widget_tree_edit_schema_points_to_schema_inspect() {
        let schema = widget_tree_edit_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        let commands_description = properties
            .get("commands")
            .and_then(|value| value.get("description"))
            .and_then(|value| value.as_str())
            .expect("commands description");
        assert!(commands_description.contains("schema_inspect"));
        assert!(commands_description.contains("widget_palette"));
        assert!(properties.contains_key("dryRun"));
        assert!(properties.contains_key("expectedRevision"));
        assert!(!properties.contains_key("continueOnError"));
        assert!(!properties.contains_key("returnDiff"));
        assert!(!properties.contains_key("returnDiagnostics"));
        assert!(!properties.contains_key("idempotencyKey"));
    }

    #[test]
    fn widget_edit_schema_points_to_schema_inspect() {
        let schema = widget_edit_schema();
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        let commands_description = properties
            .get("commands")
            .and_then(|value| value.get("description"))
            .and_then(|value| value.as_str())
            .expect("commands description");
        assert!(commands_description.contains("schema_inspect"));
        assert!(commands_description.contains("widget_inspect"));
        assert!(properties.contains_key("dryRun"));
        assert!(properties.contains_key("expectedRevision"));
    }

    #[test]
    fn widget_tree_edit_manifest_output_schema_declares_mutation_envelope() {
        let manifest: serde_json::Value =
            serde_json::from_str(include_str!("../../mcp/manifest/manifest.json"))
                .expect("manifest json");
        let tool = manifest
            .get("tools")
            .and_then(|value| value.as_array())
            .and_then(|tools| {
                tools.iter().find(|tool| {
                    tool.get("name").and_then(|value| value.as_str()) == Some("widget_tree_edit")
                })
            })
            .expect("widget_tree_edit tool");
        let output_properties = tool
            .get("outputSchema")
            .and_then(|value| value.get("oneOf"))
            .and_then(|value| value.as_array())
            .and_then(|one_of| one_of.first())
            .and_then(|value| value.get("properties"))
            .and_then(|value| value.as_object())
            .expect("output properties");
        for field in [
            "applied",
            "valid",
            "dryRun",
            "resolvedRefs",
            "planned",
            "diagnostics",
            "diff",
            "opResults",
            "previousRevision",
            "newRevision",
        ] {
            assert!(output_properties.contains_key(field), "missing {field}");
        }
    }

    #[test]
    fn widget_tree_inspect_schema_is_single_layer() {
        let schema = widget_tree_inspect_schema();
        for keyword in ["oneOf", "anyOf", "allOf", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "widget_tree_inspect schema should not expose top-level {keyword}"
            );
        }
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("assetPath"));
        assert!(properties.contains_key("view"));
        assert!(properties.contains_key("filter"));
        assert!(!properties.contains_key("includeSlotProperties"));
        let views = properties
            .get("view")
            .and_then(|value| value.get("enum"))
            .and_then(|value| value.as_array())
            .expect("view enum");
        assert_eq!(
            views
                .iter()
                .filter_map(|value| value.as_str())
                .collect::<Vec<_>>(),
            vec!["outline", "layout"]
        );
        let schema_text = serde_json::to_string(&schema).expect("schema json");
        assert!(!schema_text.contains("schema_inspect"));
    }

    #[test]
    fn widget_inspect_schema_is_single_layer() {
        let schema = widget_inspect_schema();
        for keyword in ["oneOf", "anyOf", "allOf", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "widget_inspect schema should not expose top-level {keyword}"
            );
        }
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("widgetClass"));
        assert!(properties.contains_key("assetPath"));
        assert!(properties.contains_key("widget"));
        let schema_text = serde_json::to_string(&schema).expect("schema json");
        assert!(!schema_text.contains("schema_inspect"));
    }

    #[test]
    fn widget_inspect_manifest_output_schema_declares_property_values() {
        let manifest: serde_json::Value =
            serde_json::from_str(include_str!("../../mcp/manifest/manifest.json"))
                .expect("manifest json");
        let tool = manifest
            .get("tools")
            .and_then(|value| value.as_array())
            .and_then(|tools| {
                tools.iter().find(|tool| {
                    tool.get("name").and_then(|value| value.as_str()) == Some("widget_inspect")
                })
            })
            .expect("widget_inspect tool");
        let output_schema = tool.get("outputSchema").expect("output schema");
        assert!(output_schema
            .get("$defs")
            .and_then(|value| value.get("widgetProperty"))
            .is_some());
        let success_properties = output_schema
            .get("oneOf")
            .and_then(|value| value.as_array())
            .and_then(|one_of| one_of.first())
            .and_then(|value| value.get("properties"))
            .and_then(|value| value.as_object())
            .expect("success properties");
        for field in [
            "widgetClass",
            "properties",
            "slotClass",
            "slotProperties",
            "currentValues",
            "slotCurrentValues",
        ] {
            assert!(success_properties.contains_key(field), "missing {field}");
        }
    }

    #[test]
    fn widget_event_create_manifest_output_schema_declares_native_event_shape() {
        let manifest: serde_json::Value =
            serde_json::from_str(include_str!("../../mcp/manifest/manifest.json"))
                .expect("manifest json");
        let tool = manifest
            .get("tools")
            .and_then(|value| value.as_array())
            .and_then(|tools| {
                tools.iter().find(|tool| {
                    tool.get("name").and_then(|value| value.as_str()) == Some("widget_event_create")
                })
            })
            .expect("widget_event_create tool");
        let output_schema = tool.get("outputSchema").expect("output schema");
        let success_properties = output_schema
            .get("oneOf")
            .and_then(|value| value.as_array())
            .and_then(|one_of| one_of.first())
            .and_then(|value| value.get("properties"))
            .and_then(|value| value.as_object())
            .expect("success properties");
        assert_eq!(
            success_properties
                .get("widget")
                .and_then(|value| value.get("properties"))
                .and_then(|value| value.get("name"))
                .and_then(|value| value.get("type"))
                .and_then(|value| value.as_str()),
            Some("string")
        );
        assert!(success_properties.contains_key("node"));
        assert!(success_properties.contains_key("graphRef"));
    }

    #[test]
    fn widget_inspect_translates_widget_ref_to_legacy_describe_args() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/UI/WBP_Menu"));
        args.insert("widget".into(), serde_json::json!({"name": "TitleText"}));

        let translated = translate_widget_inspect_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/UI/WBP_Menu")
        );
        assert_eq!(
            translated
                .get("widgetName")
                .and_then(|value| value.as_str()),
            Some("TitleText")
        );
    }

    #[test]
    fn widget_tree_inspect_outline_prunes_slot_properties() {
        let payload = serde_json::json!({
            "assetPath": "/Game/UI/WBP_Menu",
            "revision": "abc",
            "rootWidget": {
                "name": "RootCanvas",
                "widgetClass": "/Script/UMG.CanvasPanel",
                "slotClass": "/Script/UMG.CanvasPanelSlot",
                "slot": {"LayoutData": "..."},
                "children": [{
                    "name": "TitleText",
                    "widgetClass": "/Script/UMG.TextBlock",
                    "slotClass": "/Script/UMG.CanvasPanelSlot",
                    "slot": {"ZOrder": "2"},
                    "children": []
                }]
            }
        });
        let mut args = JsonObject::new();
        args.insert("view".into(), serde_json::json!("outline"));

        let shaped = shape_widget_tree_inspect_payload(payload, &args);
        let root = shaped
            .get("rootWidget")
            .and_then(|value| value.as_object())
            .expect("root");
        assert!(!root.contains_key("slot"));
        assert!(!root.contains_key("slotClass"));
        let child = root
            .get("children")
            .and_then(|value| value.as_array())
            .and_then(|children| children.first())
            .and_then(|value| value.as_object())
            .expect("child");
        assert!(!child.contains_key("slot"));
        assert!(!child.contains_key("slotClass"));
    }

    #[test]
    fn widget_tree_inspect_filter_keeps_root_and_returns_matches() {
        let payload = serde_json::json!({
            "assetPath": "/Game/UI/WBP_Menu",
            "revision": "abc",
            "rootWidget": {
                "name": "RootCanvas",
                "widgetClass": "/Script/UMG.CanvasPanel",
                "children": [{
                    "name": "TitleText",
                    "widgetClass": "/Script/UMG.TextBlock",
                    "slot": {"ZOrder": "2"},
                    "children": []
                }]
            }
        });
        let mut args = JsonObject::new();
        args.insert("view".into(), serde_json::json!("layout"));
        args.insert("filter".into(), serde_json::json!({"names": ["TitleText"]}));

        let shaped = shape_widget_tree_inspect_payload(payload, &args);
        assert!(shaped.get("rootWidget").is_some());
        let matches = shaped
            .get("matches")
            .and_then(|value| value.as_array())
            .expect("matches");
        assert_eq!(matches.len(), 1);
        assert_eq!(
            matches[0].get("name").and_then(|value| value.as_str()),
            Some("TitleText")
        );
    }

    #[test]
    fn widget_tree_edit_translates_add_from_palette() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/UI/WBP_Menu"));
        args.insert(
            "commands".into(),
            serde_json::json!([{
                "kind": "addFromPalette",
                "entry": {
                    "id": "widget_palette:text",
                    "kind": "native",
                    "payload": {"widgetClass": "/Script/UMG.TextBlock"},
                    "executable": true
                },
                "name": "TitleText",
                "parent": {"name": "RootCanvas"},
                "isVariable": true
            }]),
        );

        let translated = translate_widget_tree_edit_args(&args).expect("translated args");
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(ops.len(), 1);
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("addWidget")
        );
        let op_args = ops[0]
            .get("args")
            .and_then(|value| value.as_object())
            .expect("op args");
        assert_eq!(
            op_args.get("widgetClass").and_then(|value| value.as_str()),
            Some("/Script/UMG.TextBlock")
        );
        assert_eq!(
            op_args.get("parentName").and_then(|value| value.as_str()),
            Some("RootCanvas")
        );
        assert_eq!(
            op_args.get("isVariable").and_then(|value| value.as_bool()),
            Some(true)
        );
    }

    #[test]
    fn widget_tree_edit_translates_set_is_variable() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/UI/WBP_Menu"));
        args.insert(
            "commands".into(),
            serde_json::json!([{
                "kind": "setIsVariable",
                "target": {"name": "WorldCardGrid"},
                "value": true
            }]),
        );

        let translated = translate_widget_tree_edit_args(&args).expect("translated args");
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(ops.len(), 1);
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("setIsVariable")
        );
        let op_args = ops[0]
            .get("args")
            .and_then(|value| value.as_object())
            .expect("op args");
        assert_eq!(
            op_args.get("name").and_then(|value| value.as_str()),
            Some("WorldCardGrid")
        );
        assert_eq!(
            op_args.get("value").and_then(|value| value.as_bool()),
            Some(true)
        );
    }

    #[test]
    fn widget_tree_edit_translates_rename_widget() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/UI/WBP_Menu"));
        args.insert(
            "commands".into(),
            serde_json::json!([{
                "kind": "renameWidget",
                "target": {"name": "WorldSelect_Card0_Button"},
                "name": "CardButton"
            }]),
        );

        let translated = translate_widget_tree_edit_args(&args).expect("translated args");
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(ops.len(), 1);
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("renameWidget")
        );
        let op_args = ops[0]
            .get("args")
            .and_then(|value| value.as_object())
            .expect("op args");
        assert_eq!(
            op_args.get("name").and_then(|value| value.as_str()),
            Some("WorldSelect_Card0_Button")
        );
        assert_eq!(
            op_args.get("newName").and_then(|value| value.as_str()),
            Some("CardButton")
        );
    }

    #[test]
    fn widget_edit_translates_property_commands() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/UI/WBP_Menu"));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "setProperty",
                    "widget": {"name": "TitleText"},
                    "property": "Text",
                    "value": "Hello"
                },
                {
                    "kind": "setSlotProperty",
                    "widget": {"name": "TitleText"},
                    "property": "ZOrder",
                    "value": "2"
                }
            ]),
        );
        args.insert("dryRun".into(), serde_json::json!(true));

        let translated = translate_widget_edit_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/UI/WBP_Menu")
        );
        assert_eq!(
            translated.get("dryRun").and_then(|value| value.as_bool()),
            Some(true)
        );
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("setProperty")
        );
        assert_eq!(
            ops[1].get("op").and_then(|value| value.as_str()),
            Some("setSlotProperty")
        );
    }

    #[test]
    fn material_palette_accepts_asset_graph_ref() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({"kind": "asset", "assetPath": "/Game/M_Test"}),
        );
        args.insert("query".into(), serde_json::json!("Multiply"));

        let translated = translate_material_palette_args(&args).expect("translated args");
        assert_eq!(
            translated.get("assetPath").and_then(|value| value.as_str()),
            Some("/Game/M_Test")
        );
        assert_eq!(
            translated.get("query").and_then(|value| value.as_str()),
            Some("Multiply")
        );
    }

    #[test]
    fn material_graph_edit_translates_add_from_palette_and_connect() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/M_Test"));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "addFromPalette",
                    "entry": {
                        "id": "material_palette:multiply",
                        "kind": "expression",
                        "payload": {"nodeClassPath": "/Script/Engine.MaterialExpressionMultiply"}
                    },
                    "alias": "multiply",
                    "position": {"x": 240, "y": 120}
                },
                {
                    "kind": "connect",
                    "from": {"node": {"alias": "multiply"}, "pin": "Result"},
                    "to": {"node": {"id": "__material_root__"}, "pin": "Base Color"}
                }
            ]),
        );

        let translated = translate_material_graph_edit_args(&args).expect("translated args");
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("addNode.byClass")
        );
        assert_eq!(
            ops[0].get("nodeClassPath").and_then(|value| value.as_str()),
            Some("/Script/Engine.MaterialExpressionMultiply")
        );
        assert_eq!(
            ops[1].get("op").and_then(|value| value.as_str()),
            Some("connectPins")
        );
    }

    #[test]
    fn material_graph_edit_schema_points_to_schema_inspect() {
        let schema = material_graph_edit_schema();
        let commands_description = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .and_then(|properties| properties.get("commands"))
            .and_then(|commands| commands.get("description"))
            .and_then(|value| value.as_str())
            .unwrap_or_default();
        assert!(commands_description.contains("schema_inspect"));
        assert!(commands_description.contains("material_palette"));
    }

    #[test]
    fn material_node_edit_translates_to_set_property_op() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/M_Test"));
        args.insert("node".into(), serde_json::json!({"id": "node-1"}));
        args.insert("property".into(), serde_json::json!("ParameterName"));
        args.insert("value".into(), serde_json::json!("DensityScale"));

        let translated = translate_material_node_edit_args(&args).expect("translated args");
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("setProperty")
        );
        assert_eq!(
            ops[0].get("nodeId").and_then(|value| value.as_str()),
            Some("node-1")
        );
        assert_eq!(
            ops[0].get("property").and_then(|value| value.as_str()),
            Some("ParameterName")
        );
        assert_eq!(
            ops[0].get("value").and_then(|value| value.as_str()),
            Some("DensityScale")
        );
    }

    #[test]
    fn material_node_edit_schema_is_single_layer() {
        let schema = material_node_edit_schema();
        for keyword in ["oneOf", "anyOf", "allOf", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "material_node_edit schema should not expose top-level {keyword}"
            );
        }
        let properties = schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        assert!(properties.contains_key("node"));
        assert!(properties.contains_key("property"));
        assert!(properties.contains_key("value"));
        let schema_text = serde_json::to_string(&schema).expect("schema json");
        assert!(!schema_text.contains("schema_inspect"));
    }

    #[test]
    fn material_graph_layout_translates_selection_only() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/M_Test"));
        args.insert(
            "scope".into(),
            serde_json::json!({"mode": "selection", "nodes": [{"id": "node-1"}, {"id": "node-2"}]}),
        );

        let translated = translate_material_graph_layout_args(&args).expect("translated args");
        let ops = translated
            .get("ops")
            .and_then(|value| value.as_array())
            .expect("ops");
        assert_eq!(
            ops[0].get("op").and_then(|value| value.as_str()),
            Some("layoutGraph")
        );
        assert_eq!(
            ops[0].get("scope").and_then(|value| value.as_str()),
            Some("selection")
        );
        assert_eq!(
            ops[0]
                .get("nodeIds")
                .and_then(|value| value.as_array())
                .map(Vec::len),
            Some(2)
        );
    }

    #[test]
    fn graph_layout_schemas_do_not_expose_implicit_scopes() {
        for schema in [material_graph_layout_schema(), pcg_graph_layout_schema()] {
            let properties = schema
                .get("properties")
                .and_then(|value| value.as_object())
                .expect("properties");
            assert!(!properties.contains_key("operation"));
            assert!(!properties.contains_key("direction"));
            assert!(!properties.contains_key("style"));
            let schema_text = serde_json::to_string(&schema).expect("schema json");
            assert!(schema_text.contains("selection"));
            assert!(schema_text.contains("graph.inspect"));
            assert!(!schema_text.contains("touched"));
            assert!(!schema_text.contains("\"all\""));
            assert!(!schema_text.contains("\"tree\""));
        }
    }

    #[test]
    fn pcg_graph_layout_rejects_non_selection_scope() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/PCG_Test"));
        args.insert("scope".into(), serde_json::json!({"mode": "all"}));

        let error = translate_pcg_graph_layout_args(&args).expect_err("expected error");
        let text = serde_json::to_string(&error).expect("error json");
        assert!(text.contains("Supported modes: selection"));
    }

    #[test]
    fn diagnostic_and_log_tools_are_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("play"));
        assert!(tool_names.contains("diagnostic_tail"));
        assert!(tool_names.contains("log_tail"));
        assert!(tool_names.contains("blueprint_graph_layout"));
        assert!(!tool_names.contains("log.subscribe"));
        assert!(!tool_names.contains("diag.tail"));
    }

    #[test]
    fn public_blueprint_surface_is_declared_without_retired_tools() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "asset_create",
            "asset_inspect",
            "asset_edit",
            "blueprint_inspect",
            "blueprint_class_inspect",
            "blueprint_class_edit",
            "blueprint_member_inspect",
            "blueprint_member_edit",
            "blueprint_graph_list",
            "blueprint_graph_inspect",
            "blueprint_graph_edit",
            "blueprint_graph_layout",
            "blueprint_node_inspect",
            "blueprint_node_edit",
            "blueprint_graph_palette",
            "blueprint_compile",
        ] {
            assert!(tool_names.contains(expected), "missing tool {expected}");
        }

        for retired in [
            "blueprint.edit",
            "blueprint.enum.inspect",
            "blueprint.enum.edit",
            "blueprint.graph.refactor",
            "blueprint.graph.generate",
            "blueprint.palette",
        ] {
            assert!(
                !tool_names.contains(retired),
                "retired tool should not be declared: {retired}"
            );
        }
    }

    #[test]
    fn schema_inspect_tool_is_declared() {
        let tool_names = all_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("status"));
        assert!(tool_names.contains("schema_inspect"));
        assert!(!tool_names.contains("loomle"));
        assert!(!tool_names.contains("setup.status"));
        assert!(!tool_names.contains("setup.configure"));
    }

    #[test]
    fn public_tool_names_map_to_internal_bridge_names() {
        for (public, internal) in [
            ("schema_inspect", "schema.inspect"),
            ("blueprint_graph_list", "blueprint.graph.list"),
            ("blueprint_graph_inspect", "blueprint.graph.inspect"),
            ("blueprint_node_inspect", "blueprint.node.inspect"),
            ("material_palette", "material.palette"),
            ("pcg_palette", "pcg.palette"),
            ("widget_palette", "widget.palette"),
            ("widget_inspect", "widget.inspect"),
        ] {
            assert_eq!(public_tool_name_to_internal(public), internal);
        }
    }

    #[test]
    fn public_tool_name_mapping_restores_dotted_internal_routes() {
        let manifest: serde_json::Value =
            serde_json::from_str(super::TOOL_MANIFEST_JSON).expect("manifest json");
        for tool in manifest
            .get("tools")
            .and_then(|value| value.as_array())
            .expect("manifest tools")
        {
            let availability = tool
                .get("availability")
                .and_then(|value| value.as_array())
                .expect("availability");
            if !availability
                .iter()
                .any(|value| value.as_str() == Some("native"))
            {
                continue;
            }
            let public_name = tool
                .get("name")
                .and_then(|value| value.as_str())
                .expect("tool name");
            let internal_name = public_name.replace('_', ".");
            assert_eq!(
                public_tool_name_to_internal(public_name),
                internal_name,
                "{public_name} should route to the dotted internal route"
            );
        }
    }

    #[test]
    fn native_tools_list_comes_from_manifest() {
        let declared_tools = all_declared_tools();
        let declared_names = declared_tools
            .iter()
            .map(|tool| tool.name.as_ref().to_string())
            .collect::<std::collections::HashSet<_>>();
        let manifest: serde_json::Value =
            serde_json::from_str(super::TOOL_MANIFEST_JSON).expect("manifest json");
        let manifest_names = manifest
            .get("tools")
            .and_then(|value| value.as_array())
            .expect("manifest tools")
            .iter()
            .filter(|tool| {
                tool.get("availability")
                    .and_then(|value| value.as_array())
                    .is_some_and(|availability| {
                        availability
                            .iter()
                            .any(|value| value.as_str() == Some("native"))
                    })
            })
            .filter_map(|tool| tool.get("name").and_then(|value| value.as_str()))
            .map(str::to_string)
            .collect::<std::collections::HashSet<_>>();

        assert_eq!(declared_names, manifest_names);
        assert!(declared_tools
            .iter()
            .all(|tool| tool.output_schema.is_none()));
        let manifest_tool = |name: &str| {
            manifest
                .get("tools")
                .and_then(|value| value.as_array())
                .expect("manifest tools")
                .iter()
                .find(|tool| tool.get("name").and_then(|value| value.as_str()) == Some(name))
                .expect("manifest tool")
        };
        let status_tool = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "status")
            .expect("status");
        let status_tool_output = status_tool.name.as_ref();
        let status_tool_output = manifest_tool(status_tool_output)
            .get("outputSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|value| value.as_object())
            .expect("status output properties");
        assert!(status_tool_output.contains_key("mcp"));
        assert!(status_tool_output.contains_key("project"));
        assert!(status_tool_output.contains_key("runtime"));
        assert!(status_tool_output.contains_key("issues"));
        assert!(!status_tool_output.contains_key("plugin"));
        assert!(!status_tool_output.contains_key("hosts"));

        let blueprint = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "blueprint_inspect")
            .expect("blueprint_inspect");
        let blueprint_output_properties = blueprint.name.as_ref();
        let blueprint_output_properties = manifest_tool(blueprint_output_properties)
            .get("outputSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|value| value.as_object())
            .expect("blueprint_inspect output properties");
        assert!(blueprint_output_properties.contains_key("routes"));
        assert!(blueprint_output_properties.contains_key("summary"));
        assert!(blueprint_output_properties.contains_key("variables"));
        assert!(blueprint_output_properties.contains_key("functions"));

        let blueprint_class = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "blueprint_class_inspect")
            .expect("blueprint_class_inspect");
        let blueprint_class_output_properties = blueprint_class.name.as_ref();
        let blueprint_class_output_properties = manifest_tool(blueprint_class_output_properties)
            .get("outputSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|value| value.as_object())
            .expect("blueprint_class_inspect output properties");
        assert!(blueprint_class_output_properties.contains_key("class"));
        assert!(blueprint_class_output_properties.contains_key("settings"));
        assert!(blueprint_class_output_properties.contains_key("implementedInterfaces"));
        assert!(blueprint_class_output_properties.contains_key("interfaceFunctions"));
        assert!(blueprint_class_output_properties.contains_key("classDefaults"));
        assert!(blueprint_class_output_properties.contains_key("metadata"));

        let blueprint_member = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "blueprint_member_inspect")
            .expect("blueprint_member_inspect");
        let blueprint_member_output_properties = blueprint_member.name.as_ref();
        let blueprint_member_output_properties = manifest_tool(blueprint_member_output_properties)
            .get("outputSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|value| value.as_object())
            .expect("blueprint_member_inspect output properties");
        assert!(blueprint_member_output_properties.contains_key("assetPath"));
        assert!(blueprint_member_output_properties.contains_key("memberKind"));
        assert!(blueprint_member_output_properties.contains_key("items"));

        let blueprint_node_inspect = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "blueprint_node_inspect")
            .expect("blueprint_node_inspect");
        let blueprint_node_inspect_output = blueprint_node_inspect.name.as_ref();
        let blueprint_node_inspect_output = manifest_tool(blueprint_node_inspect_output)
            .get("outputSchema")
            .and_then(|schema| schema.get("oneOf"))
            .and_then(|value| value.as_array())
            .and_then(|items| items.first())
            .and_then(|schema| schema.get("properties"))
            .and_then(|value| value.as_object())
            .expect("blueprint_node_inspect output properties");
        assert!(blueprint_node_inspect_output.contains_key("node"));
        assert!(blueprint_node_inspect_output.contains_key("editState"));
        assert!(blueprint_node_inspect_output.contains_key("editCapabilities"));
        assert!(!blueprint_node_inspect_output.contains_key("pins"));
        assert!(!blueprint_node_inspect_output.contains_key("state"));
        assert!(!blueprint_node_inspect_output.contains_key("graphName"));

        let blueprint_member_edit = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "blueprint_member_edit")
            .expect("blueprint_member_edit");
        let blueprint_member_edit_input = blueprint_member_edit
            .input_schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("blueprint_member_edit input properties");
        let member_kind_enum = blueprint_member_edit_input
            .get("memberKind")
            .and_then(|value| value.get("enum"))
            .and_then(|value| value.as_array())
            .expect("memberKind enum");
        assert!(!member_kind_enum
            .iter()
            .any(|value| value.as_str() == Some("customEvent")));
        assert!(!blueprint_member_edit_input.contains_key("returnDiff"));
        assert!(!blueprint_member_edit_input.contains_key("returnDiagnostics"));
        assert!(blueprint_member_edit_input.contains_key("expectedRevision"));
        let blueprint_member_edit_output_properties = blueprint_member_edit.name.as_ref();
        let blueprint_member_edit_output_properties =
            manifest_tool(blueprint_member_edit_output_properties)
                .get("outputSchema")
                .and_then(|schema| schema.get("properties"))
                .and_then(|value| value.as_object())
                .expect("blueprint_member_edit output properties");
        assert!(blueprint_member_edit_output_properties.contains_key("applied"));
        assert!(blueprint_member_edit_output_properties.contains_key("valid"));
        assert!(blueprint_member_edit_output_properties.contains_key("resolvedRefs"));
        assert!(blueprint_member_edit_output_properties.contains_key("planned"));
        assert!(blueprint_member_edit_output_properties.contains_key("diagnostics"));
        assert!(blueprint_member_edit_output_properties.contains_key("diff"));
        assert!(blueprint_member_edit_output_properties.contains_key("previousRevision"));
        assert!(blueprint_member_edit_output_properties.contains_key("newRevision"));

        let blueprint_class_edit = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "blueprint_class_edit")
            .expect("blueprint_class_edit");
        let blueprint_class_edit_ops = blueprint_class_edit
            .input_schema
            .get("properties")
            .and_then(|value| value.get("operation"))
            .and_then(|value| value.get("enum"))
            .and_then(|value| value.as_array())
            .expect("blueprint_class_edit operation enum");
        assert_eq!(
            blueprint_class_edit_ops,
            &[
                serde_json::json!("setParent"),
                serde_json::json!("setSettings"),
                serde_json::json!("setDefault"),
                serde_json::json!("addInterface"),
                serde_json::json!("removeInterface"),
            ]
        );
        let blueprint_class_edit_input = blueprint_class_edit
            .input_schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("blueprint_class_edit input properties");
        assert!(!blueprint_class_edit_input.contains_key("returnDiff"));
        assert!(!blueprint_class_edit_input.contains_key("returnDiagnostics"));
        assert!(blueprint_class_edit_input.contains_key("expectedRevision"));
        let blueprint_class_edit_output_properties = blueprint_class_edit.name.as_ref();
        let blueprint_class_edit_output_properties =
            manifest_tool(blueprint_class_edit_output_properties)
                .get("outputSchema")
                .and_then(|schema| schema.get("properties"))
                .and_then(|value| value.as_object())
                .expect("blueprint_class_edit output properties");
        assert!(blueprint_class_edit_output_properties.contains_key("applied"));
        assert!(blueprint_class_edit_output_properties.contains_key("valid"));
        assert!(blueprint_class_edit_output_properties.contains_key("resolvedRefs"));
        assert!(blueprint_class_edit_output_properties.contains_key("planned"));
        assert!(blueprint_class_edit_output_properties.contains_key("diagnostics"));
        assert!(blueprint_class_edit_output_properties.contains_key("diff"));
        assert!(blueprint_class_edit_output_properties.contains_key("previousRevision"));
        assert!(blueprint_class_edit_output_properties.contains_key("newRevision"));

        let context = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "context")
            .expect("context");
        let context_properties = context
            .input_schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("context properties");
        assert!(context_properties.is_empty());
        let context_output_properties = context.name.as_ref();
        let context_output_properties = manifest_tool(context_output_properties)
            .get("outputSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|value| value.as_object())
            .expect("context output properties");
        assert!(context_output_properties.contains_key("activeAsset"));
        assert!(context_output_properties.contains_key("activeEditor"));
        assert!(context_output_properties.contains_key("activeGraph"));
        assert!(context_output_properties.contains_key("selection"));
        assert!(!context_output_properties.contains_key("context"));

        let graph_inspect = declared_tools
            .iter()
            .find(|tool| tool.name.as_ref() == "blueprint_graph_inspect")
            .expect("blueprint_graph_inspect");
        let properties = graph_inspect
            .input_schema
            .get("properties")
            .and_then(|value| value.as_object())
            .expect("properties");
        let view_enum = properties
            .get("view")
            .and_then(|value| value.get("enum"))
            .and_then(|value| value.as_array())
            .expect("view enum")
            .iter()
            .filter_map(|value| value.as_str())
            .collect::<Vec<_>>();
        assert_eq!(view_enum, vec!["summary", "exec_flow", "data_flow"]);
        assert!(!properties.contains_key("filter"));
        assert!(!properties.contains_key("page"));
        assert!(!serde_json::to_string(&graph_inspect.input_schema)
            .expect("thin input json")
            .contains("$defs"));

        let output_schema = manifest_tool(graph_inspect.name.as_ref())
            .get("outputSchema")
            .expect("blueprint_graph_inspect output schema");
        let output_views = output_schema
            .get("oneOf")
            .and_then(|value| value.as_array())
            .expect("output oneOf")
            .iter()
            .filter_map(|value| value.get("properties"))
            .filter_map(|properties| properties.get("view"))
            .filter_map(|view| view.get("const"))
            .filter_map(|value| value.as_str())
            .collect::<Vec<_>>();
        assert_eq!(output_views, vec!["summary", "exec_flow", "data_flow"]);
        let error_codes = output_schema
            .get("oneOf")
            .and_then(|value| value.as_array())
            .expect("output oneOf")
            .iter()
            .find(|entry| {
                entry.get("title").and_then(|value| value.as_str())
                    == Some("Blueprint Graph Inspect Error")
            })
            .and_then(|entry| entry.get("properties"))
            .and_then(|properties| properties.get("code"))
            .and_then(|code| code.get("enum"))
            .and_then(|value| value.as_array())
            .expect("error code enum")
            .iter()
            .filter_map(|value| value.as_str())
            .collect::<std::collections::HashSet<_>>();
        assert!(error_codes.contains("NODE_NOT_FOUND"));
        assert!(error_codes.contains("PIN_NOT_FOUND"));
    }

    #[test]
    fn schema_inspect_lists_blueprint_graph_edit_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_graph_edit"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        let operations = payload
            .get("operations")
            .and_then(|value| value.as_array())
            .expect("operations");
        let names = operations
            .iter()
            .filter_map(|entry| entry.get("name").and_then(|value| value.as_str()))
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "addFromPalette",
            "connect",
            "disconnect",
            "insertExec",
            "bypassExec",
            "breakLinks",
            "setPinDefault",
            "removeNode",
            "moveNode",
        ] {
            assert!(names.contains(expected), "missing operation {expected}");
        }
        for retired in [
            "layoutGraph",
            "compile",
            "addGraph",
            "addFunctionGraph",
            "addMacroGraph",
            "renameGraph",
            "deleteGraph",
        ] {
            assert!(
                !names.contains(retired),
                "retired operation {retired} leaked"
            );
        }
    }

    #[test]
    fn schema_inspect_lists_blueprint_node_edit_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_node_edit"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        let operations = payload
            .get("operations")
            .and_then(|value| value.as_array())
            .expect("operations");
        let names = operations
            .iter()
            .filter_map(|entry| entry.get("name").and_then(|value| value.as_str()))
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "addPin",
            "removePin",
            "insertPin",
            "renamePin",
            "movePin",
            "restorePins",
            "setDelegateFunction",
        ] {
            assert!(names.contains(expected), "missing operation {expected}");
        }
    }

    #[test]
    fn schema_inspect_returns_blueprint_node_edit_operation_schema() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_node_edit"));
        args.insert("operation".into(), serde_json::json!("addPin"));
        args.insert(
            "include".into(),
            serde_json::json!(["summary", "operation", "notes"]),
        );

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload.get("operation").and_then(|value| value.as_str()),
            Some("addPin")
        );
        let schema_text = serde_json::to_string(payload.get("operationSchema").expect("schema"))
            .expect("schema json");
        assert!(schema_text.contains("case"));
        assert!(schema_text.contains("argument"));
        let notes_text =
            serde_json::to_string(payload.get("notes").expect("notes")).expect("notes json");
        assert!(notes_text.contains("blueprint_node_inspect"));
    }

    #[test]
    fn schema_inspect_returns_add_from_palette_schema() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_graph_edit"));
        args.insert("operation".into(), serde_json::json!("addFromPalette"));
        args.insert(
            "include".into(),
            serde_json::json!(["summary", "operation", "examples", "errors", "notes"]),
        );

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload.get("category").and_then(|value| value.as_str()),
            Some("core")
        );
        assert_eq!(
            payload
                .get("operationSchema")
                .and_then(|schema| schema.get("properties"))
                .and_then(|properties| properties.get("kind"))
                .and_then(|kind| kind.get("const"))
                .and_then(|value| value.as_str()),
            Some("addFromPalette")
        );
        assert_eq!(
            payload
                .get("errors")
                .and_then(|value| value.as_array())
                .and_then(|errors| errors.first())
                .and_then(|value| value.as_str()),
            Some("PALETTE_ENTRY_NOT_EXECUTABLE")
        );
    }

    #[test]
    fn schema_inspect_lists_blueprint_member_edit_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_member_edit"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        let operations = payload
            .get("operations")
            .and_then(|value| value.as_array())
            .expect("operations");
        let names = operations
            .iter()
            .filter_map(|entry| entry.get("name").and_then(|value| value.as_str()))
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "variable.create",
            "function.create",
            "event.addInput",
            "component.create",
        ] {
            assert!(names.contains(expected), "missing operation {expected}");
        }
    }

    #[test]
    fn schema_inspect_lists_pcg_graph_edit_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("pcg"));
        args.insert("tool".into(), serde_json::json!("pcg_graph_edit"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        let operations = payload
            .get("operations")
            .and_then(|value| value.as_array())
            .expect("operations");
        let names = operations
            .iter()
            .filter_map(|entry| entry.get("name").and_then(|value| value.as_str()))
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "addFromPalette",
            "removeNode",
            "moveNode",
            "connect",
            "disconnect",
            "setPinDefault",
            "setNodeProperty",
        ] {
            assert!(names.contains(expected), "missing operation {expected}");
        }
    }

    #[test]
    fn schema_inspect_lists_material_graph_edit_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("material"));
        args.insert("tool".into(), serde_json::json!("material_graph_edit"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        let operations = payload
            .get("operations")
            .and_then(|value| value.as_array())
            .expect("operations");
        let names = operations
            .iter()
            .filter_map(|entry| entry.get("name").and_then(|value| value.as_str()))
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "addFromPalette",
            "removeNode",
            "moveNode",
            "connect",
            "disconnect",
            "breakPinLinks",
        ] {
            assert!(names.contains(expected), "missing operation {expected}");
        }
        assert!(!names.contains("layoutGraph"));
        assert!(!names.contains("compile"));
    }

    #[test]
    fn schema_inspect_lists_widget_tree_edit_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("widget"));
        args.insert("tool".into(), serde_json::json!("widget_tree_edit"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        let operations = payload
            .get("operations")
            .and_then(|value| value.as_array())
            .expect("operations");
        let names = operations
            .iter()
            .filter_map(|entry| entry.get("name").and_then(|value| value.as_str()))
            .collect::<std::collections::HashSet<_>>();

        for expected in [
            "addFromPalette",
            "removeWidget",
            "renameWidget",
            "reparentWidget",
            "setIsVariable",
        ] {
            assert!(names.contains(expected), "missing operation {expected}");
        }
        assert!(!names.contains("setProperty"));
    }

    #[test]
    fn schema_inspect_lists_widget_edit_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("widget"));
        args.insert("tool".into(), serde_json::json!("widget_edit"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        let operations = payload
            .get("operations")
            .and_then(|value| value.as_array())
            .expect("operations");
        let names = operations
            .iter()
            .filter_map(|entry| entry.get("name").and_then(|value| value.as_str()))
            .collect::<std::collections::HashSet<_>>();

        assert!(names.contains("setProperty"));
        assert!(names.contains("setSlotProperty"));
    }

    #[test]
    fn schema_inspect_returns_widget_add_from_palette_schema() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("widget"));
        args.insert("tool".into(), serde_json::json!("widget_tree_edit"));
        args.insert("operation".into(), serde_json::json!("addFromPalette"));
        args.insert(
            "include".into(),
            serde_json::json!(["summary", "operation", "notes"]),
        );

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload.get("operation").and_then(|value| value.as_str()),
            Some("addFromPalette")
        );
        assert!(payload
            .get("operationSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|properties| properties.get("entry"))
            .is_some());
        let notes = payload
            .get("notes")
            .and_then(|value| value.as_array())
            .expect("notes");
        assert!(notes.iter().any(|note| note
            .as_str()
            .is_some_and(|text| text.contains("full selected entry"))));
    }

    #[test]
    fn schema_inspect_returns_material_add_from_palette_schema() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("material"));
        args.insert("tool".into(), serde_json::json!("material_graph_edit"));
        args.insert("operation".into(), serde_json::json!("addFromPalette"));
        args.insert(
            "include".into(),
            serde_json::json!(["summary", "operation"]),
        );

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload.get("operation").and_then(|value| value.as_str()),
            Some("addFromPalette")
        );
        assert!(payload
            .get("operationSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|properties| properties.get("entry"))
            .is_some());
    }

    #[test]
    fn schema_inspect_lists_pcg_parameter_edit_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("pcg"));
        args.insert("tool".into(), serde_json::json!("pcg_parameter_edit"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        let operations = payload
            .get("operations")
            .and_then(|value| value.as_array())
            .expect("operations");
        let names = operations
            .iter()
            .filter_map(|entry| entry.get("name").and_then(|value| value.as_str()))
            .collect::<std::collections::HashSet<_>>();

        for expected in ["create", "update", "rename", "delete", "setDefault"] {
            assert!(names.contains(expected), "missing operation {expected}");
        }
    }

    #[test]
    fn schema_inspect_returns_pcg_parameter_edit_operation_schema() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("pcg"));
        args.insert("tool".into(), serde_json::json!("pcg_parameter_edit"));
        args.insert("operation".into(), serde_json::json!("create"));
        args.insert(
            "include".into(),
            serde_json::json!(["summary", "operation"]),
        );

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload.get("operation").and_then(|value| value.as_str()),
            Some("create")
        );
        assert!(payload
            .get("operationSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|properties| properties.get("type"))
            .is_some());
    }

    #[test]
    fn schema_inspect_returns_member_edit_operation_schema() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_member_edit"));
        args.insert("operation".into(), serde_json::json!("variable.create"));
        args.insert(
            "include".into(),
            serde_json::json!(["summary", "operation", "examples", "errors", "notes"]),
        );

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(false));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload.get("category").and_then(|value| value.as_str()),
            Some("variable")
        );
        assert_eq!(
            payload
                .get("operationSchema")
                .and_then(|schema| schema.get("properties"))
                .and_then(|properties| properties.get("memberKind"))
                .and_then(|member_kind| member_kind.get("const"))
                .and_then(|value| value.as_str()),
            Some("variable")
        );
        assert_eq!(
            payload
                .get("operationSchema")
                .and_then(|schema| schema.get("properties"))
                .and_then(|properties| properties.get("operation"))
                .and_then(|operation| operation.get("const"))
                .and_then(|value| value.as_str()),
            Some("create")
        );
    }

    #[test]
    fn schema_inspect_rejects_tools_outside_domain() {
        for tool in ["asset_create", "asset_inspect", "asset_edit"] {
            let mut args = JsonObject::new();
            args.insert("domain".into(), serde_json::json!("blueprint"));
            args.insert("tool".into(), serde_json::json!(tool));

            let result = call_schema_inspect(&args);
            assert_eq!(
                result.is_error,
                Some(true),
                "{tool} should not use schema_inspect"
            );
            let payload = result.structured_content.expect("structured content");
            assert_eq!(
                payload.get("code").and_then(|value| value.as_str()),
                Some("UNKNOWN_TOOL")
            );
            let available_tools = payload
                .get("availableTools")
                .and_then(|value| value.as_array())
                .expect("availableTools");
            let names = available_tools
                .iter()
                .filter_map(|value| value.as_str())
                .collect::<std::collections::HashSet<_>>();
            assert!(names.contains("blueprint_graph_edit"));
            assert!(names.contains("blueprint_member_edit"));
            assert!(names.contains("blueprint_node_edit"));
            assert!(names.contains("blueprint_inspect"));
        }
    }

    #[test]
    fn schema_inspect_returns_tool_output_schema() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_graph_inspect"));
        args.insert("include".into(), serde_json::json!(["output"]));

        let result = call_schema_inspect(&args);
        assert_ne!(result.is_error, Some(true));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload
                .get("hasOutputSchema")
                .and_then(|value| value.as_bool()),
            Some(true)
        );
        assert!(payload.get("outputSchema").is_some());
        assert!(payload
            .get("operations")
            .and_then(|value| value.as_array())
            .is_some_and(|operations| operations.is_empty()));
    }

    #[test]
    fn schema_inspect_returns_full_tool_input_schema() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_graph_inspect"));
        args.insert("include".into(), serde_json::json!(["input"]));

        let result = call_schema_inspect(&args);
        assert_ne!(result.is_error, Some(true));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload
                .get("hasInputSchema")
                .and_then(|value| value.as_bool()),
            Some(true)
        );
        let input_properties = payload
            .get("inputSchema")
            .and_then(|schema| schema.get("properties"))
            .and_then(|value| value.as_object())
            .expect("input properties");
        assert!(input_properties.contains_key("assetPath"));
        assert!(input_properties.contains_key("graph"));
        assert!(input_properties.contains_key("rootNode"));
        assert!(input_properties.contains_key("rootPin"));
        assert!(input_properties.contains_key("traversal"));
    }

    #[test]
    fn schema_inspect_can_include_operation_schema_and_tool_output() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_graph_edit"));
        args.insert("operation".into(), serde_json::json!("addFromPalette"));
        args.insert("include".into(), serde_json::json!(["operation", "output"]));

        let result = call_schema_inspect(&args);
        assert_ne!(result.is_error, Some(true));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload.get("operation").and_then(|value| value.as_str()),
            Some("addFromPalette")
        );
        assert!(payload.get("operationSchema").is_some());
        assert!(payload.get("outputSchema").is_some());
        assert_eq!(
            payload
                .get("hasOutputSchema")
                .and_then(|value| value.as_bool()),
            Some(true)
        );
    }

    #[test]
    fn schema_inspect_rejects_unknown_operation_with_available_operations() {
        let mut args = JsonObject::new();
        args.insert("domain".into(), serde_json::json!("blueprint"));
        args.insert("tool".into(), serde_json::json!("blueprint_graph_edit"));
        args.insert("operation".into(), serde_json::json!("connectPin"));

        let result = call_schema_inspect(&args);
        assert_eq!(result.is_error, Some(true));
        let payload = result.structured_content.expect("structured content");
        assert_eq!(
            payload.get("code").and_then(|value| value.as_str()),
            Some("UNKNOWN_OPERATION")
        );
        assert!(payload
            .get("availableOperations")
            .and_then(|value| value.as_array())
            .is_some_and(|operations| operations.iter().any(|op| op == "connect")));
    }

    #[test]
    fn play_schema_has_openai_compatible_top_level() {
        let schema = play_schema();
        assert_eq!(
            schema.get("type").and_then(|value| value.as_str()),
            Some("object")
        );
        for keyword in ["oneOf", "anyOf", "allOf", "enum", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "play schema should not expose top-level {keyword}"
            );
        }
    }

    #[test]
    fn play_wait_participant_conditions_match_role_count_and_id() {
        let payload = serde_json::json!({
            "participants": [
                {"id": "server", "role": "server", "ready": true},
                {"id": "client:0", "role": "client", "ready": true},
                {"id": "client:1", "role": "client", "ready": false}
            ]
        });

        let ready_client =
            vec![serde_json::json!({"role": "client", "count": 1, "state": "ready"})];
        assert!(play_participant_wait_conditions_met(
            &payload,
            &ready_client
        ));

        let two_ready_clients =
            vec![serde_json::json!({"role": "client", "count": 2, "state": "ready"})];
        assert!(!play_participant_wait_conditions_met(
            &payload,
            &two_ready_clients
        ));

        let server_by_id = vec![serde_json::json!({"participant": "server", "state": "ready"})];
        assert!(play_participant_wait_conditions_met(
            &payload,
            &server_by_id
        ));
    }

    #[test]
    fn play_wait_builds_participant_shorthand_conditions() {
        let args = serde_json::json!({"action":"wait","role":"client","count":2})
            .as_object()
            .cloned()
            .unwrap();
        let conditions = play_wait_participant_conditions_from_args(&args).unwrap();
        assert_eq!(conditions.len(), 1);
        assert_eq!(
            conditions[0].get("role").and_then(|value| value.as_str()),
            Some("client")
        );
        assert_eq!(
            conditions[0].get("count").and_then(|value| value.as_u64()),
            Some(2)
        );
        assert_eq!(
            conditions[0].get("state").and_then(|value| value.as_str()),
            Some("ready")
        );
    }
}
