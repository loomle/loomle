#![recursion_limit = "512"]

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
        Ok(ListToolsResult::with_all_items(all_declared_tools()))
    }

    fn get_tool(&self, _name: &str) -> Option<Tool> {
        None
    }

    async fn call_tool(
        &self,
        request: CallToolRequestParams,
        _context: RequestContext<RoleServer>,
    ) -> Result<CallToolResult, McpError> {
        if is_local_tool(request.name.as_ref()) {
            return Ok(self.call_pre_attach_tool(request).await);
        }
        if let Some(result) = self
            .call_public_blueprint_tool(
                request.name.as_ref(),
                request.arguments.clone().unwrap_or_default(),
            )
            .await?
        {
            return Ok(result);
        }
        if let Some(result) = self
            .call_public_material_tool(
                request.name.as_ref(),
                request.arguments.clone().unwrap_or_default(),
            )
            .await?
        {
            return Ok(result);
        }
        if let Some(result) = self
            .call_public_pcg_tool(
                request.name.as_ref(),
                request.arguments.clone().unwrap_or_default(),
            )
            .await?
        {
            return Ok(result);
        }
        if request.name.as_ref() == "play"
            && request
                .arguments
                .as_ref()
                .and_then(|args| args.get("action"))
                .and_then(|value| value.as_str())
                == Some("wait")
        {
            return self
                .call_play_wait(request.arguments.unwrap_or_default())
                .await;
        }
        self.runtime_call(request.name.as_ref(), request.arguments.unwrap_or_default())
            .await
    }
}

impl LoomleProxyServer {
    async fn call_pre_attach_tool(&self, request: CallToolRequestParams) -> CallToolResult {
        let args = request.arguments.unwrap_or_default();
        match request.name.as_ref() {
            "loomle" => self.call_loomle_runtime_status().await,
            "loomle.status" => self.call_loomle_status().await,
            "project.list" => call_project_list(&args),
            "project.attach" => self.call_project_attach(&args).await,
            "project.install" => call_project_install(&args),
            _ => bridge_unavailable_result("unknown pre-attach tool"),
        }
    }

    async fn call_loomle_status(&self) -> CallToolResult {
        self.try_auto_attach().await;
        let projects = discover_runtime_projects(ProjectStatusFilter::All);
        let env_info = self.env_info.lock().await.clone();
        let attached_project_root = env_info
            .as_ref()
            .map(|env_info| env_info.project_root.display().to_string());
        let update_check = check_for_updates();
        let observability = if let Some(env_info) = env_info.as_ref() {
            self.observability_state_for_env(env_info).await
        } else {
            None
        };
        let payload = serde_json::json!({
            "loomleVersion": env!("CARGO_PKG_VERSION"),
            "attached": attached_project_root.is_some(),
            "attachedProject": attached_project_root,
            "onlineProjectCount": projects.iter().filter(|project| project.status == "online").count(),
            "projectCount": projects.len(),
            "update": update_check,
            "observability": observability,
        });
        structured_result(payload)
    }

    async fn call_loomle_runtime_status(&self) -> CallToolResult {
        self.try_auto_attach().await;
        let Some(env_info) = self.env_info.lock().await.clone() else {
            return structured_result(serde_json::json!({
                "status": "error",
                "domainCode": "",
                "message": "No project attached",
                "runtime": {
                    "rpcConnected": false,
                    "listenerReady": false,
                    "isPIE": false,
                    "editorBusyReason": "NO_PROJECT_ATTACHED"
                }
            }));
        };

        let health = match rpc_health(&env_info).await {
            Ok(value) => value,
            Err(RpcClientError::Startup(err)) => {
                return structured_result(serde_json::json!({
                    "status": "error",
                    "domainCode": "",
                    "message": err.message,
                    "runtime": {
                        "rpcConnected": false,
                        "listenerReady": false,
                        "isPIE": false,
                        "editorBusyReason": "RUNTIME_UNAVAILABLE"
                    }
                }));
            }
            Err(RpcClientError::Invoke(error)) => {
                return CallToolResult::structured_error(serde_json::json!({
                    "code": error.code,
                    "message": error.message,
                    "retryable": error.retryable,
                    "detail": error.detail,
                    "payload": error.payload
                }));
            }
            Err(RpcClientError::Protocol(message)) => {
                return CallToolResult::structured_error(serde_json::json!({
                    "message": message
                }));
            }
        };
        let capabilities = rpc_capabilities(&env_info).await.ok();
        let status = health
            .get("status")
            .and_then(|value| value.as_str())
            .unwrap_or("error");
        let is_pie = health
            .get("isPIE")
            .and_then(|value| value.as_bool())
            .unwrap_or(false);
        let editor_busy_reason = health
            .get("editorBusyReason")
            .and_then(|value| value.as_str())
            .unwrap_or("");

        structured_result(serde_json::json!({
            "status": status,
            "domainCode": "",
            "message": "",
            "runtime": {
                "rpcConnected": true,
                "listenerReady": true,
                "isPIE": is_pie,
                "editorBusyReason": editor_busy_reason,
                "rpcHealth": health,
                "capabilities": capabilities
            }
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
        let selected = projects.into_iter().find(|project| {
            project_id
                .as_ref()
                .is_some_and(|id| *id == project.project_id)
                || project_root
                    .as_ref()
                    .is_some_and(|root| *root == project.project_root.display().to_string())
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
            "blueprint.validate" => Ok(Some(self.call_blueprint_validate(args).await?)),
            "blueprint.compile" => Ok(Some(self.call_blueprint_compile(args).await?)),
            "blueprint.inspect" => Ok(Some(self.call_blueprint_inspect(args).await?)),
            "blueprint.edit" => Ok(Some(self.runtime_call("blueprint.edit", args).await?)),
            "blueprint.enum.inspect" => Ok(Some(
                self.runtime_call("blueprint.enum.inspect", args).await?,
            )),
            "blueprint.enum.edit" => {
                Ok(Some(self.runtime_call("blueprint.enum.edit", args).await?))
            }
            "blueprint.member.inspect" => Ok(Some(self.call_blueprint_member_inspect(args).await?)),
            "blueprint.member.edit" => Ok(Some(
                self.runtime_call("blueprint.member.edit", args).await?,
            )),
            "blueprint.graph.refactor" => Ok(Some(self.call_blueprint_graph_refactor(args).await?)),
            "blueprint.graph.generate" => Ok(Some(self.call_blueprint_graph_generate(args).await?)),
            "blueprint.graph.recipe.list" => Ok(Some(call_blueprint_graph_recipe_list(&args))),
            "blueprint.graph.recipe.inspect" => {
                Ok(Some(call_blueprint_graph_recipe_inspect(&args)))
            }
            "blueprint.graph.recipe.validate" => {
                Ok(Some(call_blueprint_graph_recipe_validate(&args)))
            }
            "blueprint.palette" => Ok(Some(self.call_blueprint_palette(args).await?)),
            _ => Ok(None),
        }
    }

    async fn call_public_material_tool(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<Option<CallToolResult>, McpError> {
        match tool_name {
            "material.query" => {
                let query_args = match translate_material_query_args(&args) {
                    Ok(value) => value,
                    Err(error) => return Ok(Some(error)),
                };
                Ok(Some(self.runtime_call("material.query", query_args).await?))
            }
            _ => Ok(None),
        }
    }

    async fn call_public_pcg_tool(
        &self,
        tool_name: &str,
        args: rmcp::model::JsonObject,
    ) -> Result<Option<CallToolResult>, McpError> {
        match tool_name {
            "pcg.query" => {
                let query_args = match translate_pcg_query_args(&args) {
                    Ok(value) => value,
                    Err(error) => return Ok(Some(error)),
                };
                Ok(Some(self.runtime_call("pcg.query", query_args).await?))
            }
            _ => Ok(None),
        }
    }

    async fn call_blueprint_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let payload = self.runtime_payload("blueprint.describe", args).await?;
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
        let payload = self.runtime_payload("blueprint.describe", args).await?;
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
        Ok(self.runtime_call("blueprint.list", legacy_args).await?)
    }

    async fn call_blueprint_graph_inspect(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let legacy_args = match translate_blueprint_graph_inspect_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self.runtime_payload("blueprint.query", legacy_args).await?;
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
        Ok(structured_result(serde_json::Value::Object(result)))
    }

    async fn call_blueprint_palette(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let payload = self.runtime_payload("blueprint.palette", args).await?;
        if payload.get("isError").and_then(|v| v.as_bool()) == Some(true) {
            return Ok(CallToolResult::structured_error(payload));
        }
        Ok(structured_result(payload))
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
            .runtime_payload("blueprint.mutate", legacy_args)
            .await?;
        Ok(structured_result(augment_blueprint_mutate_result(payload)))
    }

    async fn call_blueprint_graph_refactor(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let edit_args = match compile_blueprint_refactor_request(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        self.call_blueprint_graph_edit(edit_args).await
    }

    async fn call_blueprint_graph_generate(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let edit_args = match compile_blueprint_generate_request(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        self.call_blueprint_graph_edit(edit_args).await
    }

    async fn call_blueprint_validate(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let legacy_args = match translate_blueprint_validate_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        Ok(self.runtime_call("blueprint.verify", legacy_args).await?)
    }

    async fn call_blueprint_compile(
        &self,
        args: rmcp::model::JsonObject,
    ) -> Result<CallToolResult, McpError> {
        let legacy_args = match translate_blueprint_validate_args(&args) {
            Ok(value) => value,
            Err(error) => return Ok(error),
        };
        let payload = self
            .runtime_payload("blueprint.verify", legacy_args)
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
}

fn invalid_argument_result(message: impl Into<String>) -> CallToolResult {
    CallToolResult::structured_error(serde_json::json!({
        "isError": true,
        "code": "INVALID_ARGUMENT",
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

fn copy_if_present(
    source: &rmcp::model::JsonObject,
    target: &mut rmcp::model::JsonObject,
    field: &str,
) {
    if let Some(value) = source.get(field) {
        target.insert(field.to_owned(), value.clone());
    }
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

fn translate_material_query_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    translate_asset_query_args(args, "material.query")
}

fn translate_pcg_query_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    translate_asset_query_args(args, "pcg.query")
}

fn translate_blueprint_graph_list_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint.graph.list")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    copy_if_present(args, &mut translated, "includeCompositeSubgraphs");
    Ok(translated)
}

fn translate_blueprint_graph_inspect_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint.graph.inspect")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    let graph_address =
        read_optional_graph_address(args, &asset_path, true, "blueprint.graph.inspect")?;
    write_optional_graph_address(&mut translated, graph_address);
    for field in [
        "nodeIds",
        "nodeClasses",
        "includeComments",
        "includePinDefaults",
        "includeConnections",
        "limit",
        "cursor",
        "layoutDetail",
    ] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
}

fn translate_blueprint_validate_args(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint.validate")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    let graph_address =
        read_optional_graph_address(args, &asset_path, false, "blueprint.validate")?
            .unwrap_or_else(|| BlueprintGraphAddress::Name("EventGraph".to_string()));
    write_optional_graph_address(&mut translated, Some(graph_address));
    for field in ["limit", "cursor", "layoutDetail"] {
        copy_if_present(args, &mut translated, field);
    }
    Ok(translated)
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

fn copy_add_node_type_payload_fields(
    node_type_obj: &serde_json::Map<String, serde_json::Value>,
    args: &mut serde_json::Map<String, serde_json::Value>,
) {
    for (key, value) in node_type_obj {
        if matches!(
            key.as_str(),
            "id" | "kind"
                | "nodeClassPath"
                | "functionClassPath"
                | "functionName"
                | "customEventName"
        ) {
            continue;
        }
        args.entry(key.clone()).or_insert_with(|| value.clone());
    }
}

fn infer_add_node_op(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Result<(String, serde_json::Map<String, serde_json::Value>), String> {
    let mut args = serde_json::Map::new();
    if let Some(position) = command.get("position") {
        args.insert("position".into(), position.clone());
    }
    if let Some(anchor) = command.get("anchor") {
        args.insert("anchor".into(), anchor.clone());
    }
    if let Some(from) = command.get("from") {
        args.insert("from".into(), from.clone());
    }

    let node_type = command
        .get("nodeType")
        .or_else(|| command.get("wrapper"))
        .or_else(|| command.get("call"))
        .ok_or_else(|| "addNode requires nodeType.".to_owned())?;
    let node_type_obj = node_type
        .as_object()
        .ok_or_else(|| "nodeType must be an object.".to_owned())?;
    let node_type_id = node_type_obj
        .get("id")
        .and_then(|value| value.as_str())
        .or_else(|| command.get("nodeTypeId").and_then(|value| value.as_str()));
    let node_kind = node_type_obj
        .get("kind")
        .and_then(|value| value.as_str())
        .or_else(|| command.get("nodeKind").and_then(|value| value.as_str()));

    if matches!(node_kind, Some("branch")) {
        return Ok(("addNode.branch".into(), args));
    }
    if matches!(node_kind, Some("sequence")) {
        return Ok(("addNode.sequence".into(), args));
    }
    if matches!(node_kind, Some("knot" | "reroute")) {
        return Ok(("addNode.knot".into(), args));
    }
    if matches!(node_kind, Some("comment")) {
        if let Some(text) = command.get("text").or_else(|| command.get("comment")) {
            args.insert("text".into(), text.clone());
        }
        return Ok(("addNode.comment".into(), args));
    }
    if matches!(node_kind, Some("customEvent" | "custom_event")) {
        let event_name = node_type_obj
            .get("name")
            .and_then(|value| value.as_str())
            .or_else(|| {
                node_type_obj
                    .get("eventName")
                    .and_then(|value| value.as_str())
            })
            .or_else(|| {
                node_type_obj
                    .get("customEventName")
                    .and_then(|value| value.as_str())
            })
            .or_else(|| command.get("name").and_then(|value| value.as_str()))
            .or_else(|| command.get("eventName").and_then(|value| value.as_str()))
            .ok_or_else(|| "Custom event nodes require name.".to_owned())?;
        args.insert("name".into(), serde_json::json!(event_name));
        copy_add_node_type_payload_fields(node_type_obj, &mut args);
        if let Some(inputs) = command.get("inputs").or_else(|| command.get("parameters")) {
            args.insert("inputs".into(), inputs.clone());
        }
        for field in ["replication", "rpc", "netMode", "reliable", "isReliable"] {
            copy_if_present(command, &mut args, field);
        }
        return Ok(("addNode.customEvent".into(), args));
    }
    if matches!(node_kind, Some("cast")) {
        let target_class = node_type_obj
            .get("targetClassPath")
            .and_then(|value| value.as_str())
            .or_else(|| {
                command
                    .get("targetClassPath")
                    .and_then(|value| value.as_str())
            })
            .or_else(|| command.get("targetType").and_then(|value| value.as_str()))
            .ok_or_else(|| "Cast nodes require targetClassPath or targetType.".to_owned())?;
        args.insert("targetClassPath".into(), serde_json::json!(target_class));
        return Ok(("addNode.cast".into(), args));
    }

    if let Some(node_type_id) = node_type_id {
        if let Some(function_descriptor) = node_type_id.strip_prefix("ufunction:") {
            let Some((class_path, function_name)) = function_descriptor.rsplit_once(':') else {
                return Err(
                    "ufunction nodeType ids must use ufunction:<classPath>:<functionName>.".into(),
                );
            };
            args.insert("functionClassPath".into(), serde_json::json!(class_path));
            args.insert("functionName".into(), serde_json::json!(function_name));
            return Ok(("addNode.byFunction".into(), args));
        }
        if let Some(class_path) = node_type_id.strip_prefix("class:") {
            args.insert("nodeClassPath".into(), serde_json::json!(class_path));
            copy_add_node_type_payload_fields(node_type_obj, &mut args);
            return Ok(("addNode.byClass".into(), args));
        }
        if let Some(class_path) = node_type_id.strip_prefix("event:") {
            let Some((event_class_path, event_name)) = class_path.rsplit_once(':') else {
                return Err("event nodeType ids must use event:<classPath>:<eventName>.".into());
            };
            args.insert("eventClassPath".into(), serde_json::json!(event_class_path));
            args.insert("eventName".into(), serde_json::json!(event_name));
            return Ok(("addNode.byEvent".into(), args));
        }
        if node_type_id.starts_with("/Script/") {
            args.insert("nodeClassPath".into(), serde_json::json!(node_type_id));
            copy_add_node_type_payload_fields(node_type_obj, &mut args);
            return Ok(("addNode.byClass".into(), args));
        }
    }

    if let Some(node_class_path) = node_type_obj
        .get("nodeClassPath")
        .and_then(|value| value.as_str())
        .or_else(|| {
            command
                .get("nodeClassPath")
                .and_then(|value| value.as_str())
        })
    {
        args.insert("nodeClassPath".into(), serde_json::json!(node_class_path));
        copy_add_node_type_payload_fields(node_type_obj, &mut args);
        return Ok(("addNode.byClass".into(), args));
    }

    if let Some(function_name) = node_type_obj
        .get("functionName")
        .and_then(|value| value.as_str())
        .or_else(|| command.get("functionName").and_then(|value| value.as_str()))
    {
        args.insert("functionName".into(), serde_json::json!(function_name));
        if let Some(function_class_path) = node_type_obj
            .get("functionClassPath")
            .and_then(|value| value.as_str())
            .or_else(|| {
                command
                    .get("functionClassPath")
                    .and_then(|value| value.as_str())
            })
        {
            args.insert(
                "functionClassPath".into(),
                serde_json::json!(function_class_path),
            );
        }
        return Ok(("addNode.byFunction".into(), args));
    }

    if let Some(variable_name) = node_type_obj
        .get("variableName")
        .and_then(|value| value.as_str())
        .or_else(|| command.get("variableName").and_then(|value| value.as_str()))
    {
        args.insert("variableName".into(), serde_json::json!(variable_name));
        if let Some(mode) = node_type_obj
            .get("mode")
            .and_then(|value| value.as_str())
            .or_else(|| command.get("mode").and_then(|value| value.as_str()))
        {
            args.insert("mode".into(), serde_json::json!(mode));
        }
        if let Some(variable_class_path) = node_type_obj
            .get("variableClassPath")
            .and_then(|value| value.as_str())
            .or_else(|| {
                command
                    .get("variableClassPath")
                    .and_then(|value| value.as_str())
            })
        {
            args.insert(
                "variableClassPath".into(),
                serde_json::json!(variable_class_path),
            );
        }
        return Ok(("addNode.byVariable".into(), args));
    }

    Err("Unable to infer addNode op from nodeType.".into())
}

fn compile_add_node_command(
    command: &serde_json::Map<String, serde_json::Value>,
) -> Result<Vec<serde_json::Value>, String> {
    let (op_name, args) = infer_add_node_op(command)?;
    let mut op = serde_json::Map::new();
    op.insert("op".into(), serde_json::json!(op_name));
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
            "addNode" => compile_add_node_command(command_obj),
            "addNode.customEvent" => {
                let event_name = command_obj
                    .get("name")
                    .and_then(|value| value.as_str())
                    .or_else(|| {
                        command_obj
                            .get("eventName")
                            .and_then(|value| value.as_str())
                    })
                    .ok_or_else(|| invalid_argument_result("addNode.customEvent requires name."))?;
                let mut args = serde_json::Map::new();
                args.insert("name".into(), serde_json::json!(event_name));
                for field in [
                    "position",
                    "anchor",
                    "inputs",
                    "parameters",
                    "replication",
                    "rpc",
                    "netMode",
                    "reliable",
                    "isReliable",
                ] {
                    copy_if_present(command_obj, &mut args, field);
                }
                let mut op = serde_json::Map::new();
                op.insert("op".into(), serde_json::json!("addNode.customEvent"));
                if let Some(alias) = command_obj.get("alias").and_then(|value| value.as_str()) {
                    op.insert("clientRef".into(), serde_json::json!(alias));
                }
                op.insert("args".into(), serde_json::Value::Object(args));
                Ok(vec![serde_json::Value::Object(op)])
            }
            "addNode.timeline" => {
                if command_obj.contains_key("settings") || command_obj.contains_key("tracks") {
                    return Err(invalid_argument_result(
                        "addNode.timeline currently creates the timeline node/template only; use the returned secondarySurface guidance for deeper timeline settings and tracks.",
                    ));
                }
                let mut args = serde_json::Map::new();
                args.insert(
                    "nodeClassPath".into(),
                    serde_json::json!("/Script/BlueprintGraph.K2Node_Timeline"),
                );
                for field in ["position", "anchor", "timelineName", "name"] {
                    copy_if_present(command_obj, &mut args, field);
                }
                if !args.contains_key("timelineName") {
                    if let Some(name) = command_obj.get("name") {
                        args.insert("timelineName".into(), name.clone());
                    }
                }
                let mut op = serde_json::Map::new();
                op.insert("op".into(), serde_json::json!("addNode.byClass"));
                if let Some(alias) = command_obj.get("alias").and_then(|value| value.as_str()) {
                    op.insert("clientRef".into(), serde_json::json!(alias));
                }
                op.insert("args".into(), serde_json::Value::Object(args));
                Ok(vec![serde_json::Value::Object(op)])
            }
            "addNode.byMacro" => {
                let macro_library = command_obj
                    .get("macroLibraryAssetPath")
                    .and_then(|value| value.as_str())
                    .or_else(|| command_obj.get("macroLibrary").and_then(|value| value.as_str()))
                    .ok_or_else(|| {
                        invalid_argument_result(
                            "addNode.byMacro requires macroLibraryAssetPath or macroLibrary.",
                        )
                    })?;
                let macro_graph = command_obj
                    .get("macroGraphName")
                    .and_then(|value| value.as_str())
                    .or_else(|| command_obj.get("macroName").and_then(|value| value.as_str()))
                    .ok_or_else(|| {
                        invalid_argument_result(
                            "addNode.byMacro requires macroGraphName or macroName.",
                        )
                    })?;
                let mut args = serde_json::Map::new();
                args.insert(
                    "macroLibraryAssetPath".into(),
                    serde_json::json!(macro_library),
                );
                args.insert("macroGraphName".into(), serde_json::json!(macro_graph));
                for field in ["position", "anchor"] {
                    copy_if_present(command_obj, &mut args, field);
                }
                let mut op = serde_json::Map::new();
                op.insert("op".into(), serde_json::json!("addNode.byMacro"));
                if let Some(alias) = command_obj.get("alias").and_then(|value| value.as_str()) {
                    op.insert("clientRef".into(), serde_json::json!(alias));
                }
                op.insert("args".into(), serde_json::Value::Object(args));
                Ok(vec![serde_json::Value::Object(op)])
            }
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
                Ok(vec![serde_json::json!({"op":"reconstructNode","args": args})])
            }
            "rebindMatchingPins" => {
                let from_node = command_obj
                    .get("fromNode")
                    .or_else(|| command_obj.get("from"))
                    .ok_or_else(|| "rebindMatchingPins requires fromNode.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                let to_node = command_obj
                    .get("toNode")
                    .or_else(|| command_obj.get("to"))
                    .ok_or_else(|| "rebindMatchingPins requires toNode.".to_owned())
                    .and_then(extract_node_token)
                    .map_err(invalid_argument_result)?;
                Ok(vec![serde_json::json!({
                    "op":"rebindMatchingPins",
                    "args": {
                        "fromNode": from_node,
                        "toNode": to_node
                    }
                })])
            }
            "moveInputLinks" => {
                let from = command_obj
                    .get("from")
                    .ok_or_else(|| "moveInputLinks requires from.".to_owned())
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                let to = command_obj
                    .get("to")
                    .ok_or_else(|| "moveInputLinks requires to.".to_owned())
                    .and_then(extract_pin_endpoint)
                    .map_err(invalid_argument_result)?;
                Ok(vec![serde_json::json!({
                    "op":"moveInputLinks",
                    "args": {
                        "from": from,
                        "to": to
                    }
                })])
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
                op.insert("op".into(), serde_json::json!("addNode.knot"));
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
                op.insert("op".into(), serde_json::json!("addNode.comment"));
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
    let asset_path = read_required_asset_path(args, "blueprint.graph.edit")?;
    let graph_address =
        read_optional_graph_address(args, &asset_path, true, "blueprint.graph.edit")?;
    let mut translated = rmcp::model::JsonObject::new();
    translated.insert("assetPath".into(), serde_json::json!(asset_path));
    write_optional_graph_address(&mut translated, graph_address);
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

fn compile_blueprint_refactor_request(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint.graph.refactor")?;
    let graph_address =
        read_optional_graph_address(args, &asset_path, true, "blueprint.graph.refactor")?;
    let transforms = args
        .get("transforms")
        .and_then(|value| value.as_array())
        .ok_or_else(|| invalid_argument_result("blueprint.graph.refactor requires transforms."))?;
    let mut commands = Vec::new();
    for (index, transform) in transforms.iter().enumerate() {
        let Some(transform_obj) = transform.as_object() else {
            return Err(invalid_argument_result(format!(
                "transform at index {index} must be an object."
            )));
        };
        let Some(kind) = transform_obj.get("kind").and_then(|value| value.as_str()) else {
            return Err(invalid_argument_result(format!(
                "transform at index {index} requires kind."
            )));
        };
        match kind {
            "insertBetween" => {
                let alias = transform_obj
                    .get("alias")
                    .and_then(|value| value.as_str())
                    .unwrap_or("insertedNode");
                let add_node = serde_json::json!({
                    "kind": "addNode",
                    "alias": alias,
                    "nodeType": transform_obj.get("nodeType").cloned().unwrap_or(serde_json::Value::Null),
                    "from": transform_obj.get("link").and_then(|value| value.get("from")).cloned().unwrap_or(serde_json::Value::Null)
                });
                let input_pin = transform_obj
                    .get("inputPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("execute");
                let output_pin = transform_obj
                    .get("outputPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("then");
                let link = transform_obj
                    .get("link")
                    .and_then(|value| value.as_object())
                    .ok_or_else(|| invalid_argument_result("insertBetween requires link."))?;
                let from = link
                    .get("from")
                    .cloned()
                    .ok_or_else(|| invalid_argument_result("insertBetween link requires from."))?;
                let to = link
                    .get("to")
                    .cloned()
                    .ok_or_else(|| invalid_argument_result("insertBetween link requires to."))?;
                commands.push(add_node);
                commands.push(serde_json::json!({"kind":"disconnect","from":from,"to":to}));
                commands.push(serde_json::json!({
                    "kind":"connect",
                    "from": from,
                    "to": { "node": { "alias": alias }, "pin": input_pin }
                }));
                commands.push(serde_json::json!({
                    "kind":"connect",
                    "from": { "node": { "alias": alias }, "pin": output_pin },
                    "to": to
                }));
            }
            "replaceNode" => {
                if transform_obj.contains_key("node") || transform_obj.contains_key("nodeType") {
                    return Err(invalid_argument_result(
                        "replaceNode uses target/replacement only; node/nodeType are no longer supported.",
                    ));
                }
                let alias = transform_obj
                    .get("alias")
                    .and_then(|value| value.as_str())
                    .unwrap_or("replacementNode");
                let rebind_policy = transform_obj
                    .get("rebindPolicy")
                    .and_then(|value| value.as_str())
                    .unwrap_or("none");
                if rebind_policy != "none" && rebind_policy != "matchingPins" {
                    return Err(invalid_argument_result(
                        "replaceNode rebindPolicy must be none or matchingPins.",
                    ));
                }
                let target = transform_obj
                    .get("target")
                    .cloned()
                    .ok_or_else(|| invalid_argument_result("replaceNode requires target."))?;
                let replacement = transform_obj
                    .get("replacement")
                    .cloned()
                    .ok_or_else(|| invalid_argument_result("replaceNode requires replacement."))?;
                commands.push(serde_json::json!({
                    "kind": "addNode",
                    "alias": alias,
                    "nodeType": replacement,
                    "anchor": target
                }));
                if rebind_policy == "matchingPins" {
                    commands.push(serde_json::json!({
                        "kind": "rebindMatchingPins",
                        "fromNode": target,
                        "toNode": { "alias": alias }
                    }));
                }
                if transform_obj
                    .get("removeOriginal")
                    .and_then(|value| value.as_bool())
                    .unwrap_or(false)
                {
                    commands.push(serde_json::json!({"kind":"removeNode","node": target}));
                }
            }
            "wrapWith" => {
                if transform_obj.contains_key("link") {
                    return Err(invalid_argument_result(
                        "wrapWith wraps a target node; use insertBetween for link insertion.",
                    ));
                }
                let alias = transform_obj
                    .get("alias")
                    .and_then(|value| value.as_str())
                    .unwrap_or("wrapperNode");
                let target = transform_obj
                    .get("target")
                    .cloned()
                    .ok_or_else(|| invalid_argument_result("wrapWith requires target."))?;
                let wrapper = transform_obj
                    .get("wrapper")
                    .cloned()
                    .ok_or_else(|| invalid_argument_result("wrapWith requires wrapper."))?;
                let entry_pin = transform_obj
                    .get("entryPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("execute");
                let target_entry_pin = transform_obj
                    .get("targetEntryPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("execute");
                let wrapper_exit_pin = transform_obj
                    .get("wrapperExitPin")
                    .and_then(|value| value.as_str())
                    .unwrap_or("then");
                commands.push(serde_json::json!({
                    "kind":"addNode",
                    "alias": alias,
                    "nodeType": wrapper,
                    "anchor": target
                }));
                commands.push(serde_json::json!({
                    "kind":"moveInputLinks",
                    "from": { "node": target, "pin": target_entry_pin },
                    "to": { "node": { "alias": alias }, "pin": entry_pin }
                }));
                commands.push(serde_json::json!({
                    "kind":"connect",
                    "from": { "node": { "alias": alias }, "pin": wrapper_exit_pin },
                    "to": { "node": target, "pin": target_entry_pin }
                }));
            }
            "fanoutExec" => {
                let alias = transform_obj
                    .get("alias")
                    .and_then(|value| value.as_str())
                    .unwrap_or("sequenceNode");
                let source = transform_obj
                    .get("source")
                    .cloned()
                    .ok_or_else(|| invalid_argument_result("fanoutExec requires source."))?;
                let targets = transform_obj
                    .get("targets")
                    .and_then(|value| value.as_array())
                    .ok_or_else(|| invalid_argument_result("fanoutExec requires targets."))?;
                commands.push(serde_json::json!({
                    "kind":"addNode",
                    "alias": alias,
                    "nodeType": { "kind": "sequence" },
                    "from": source
                }));
                commands.push(serde_json::json!({
                    "kind":"connect",
                    "from": source,
                    "to": { "node": { "alias": alias }, "pin": "execute" }
                }));
                for (target_index, target) in targets.iter().enumerate() {
                    commands.push(serde_json::json!({
                        "kind":"connect",
                        "from": { "node": { "alias": alias }, "pin": format!("Then {}", target_index) },
                        "to": target
                    }));
                }
            }
            "cleanupReroutes" => {
                return Err(not_implemented_result(
                    "blueprint.graph.refactor",
                    "cleanupReroutes is not implemented yet.",
                ));
            }
            other => {
                return Err(not_implemented_result(
                    "blueprint.graph.refactor",
                    &format!("Unsupported transform kind: {other}"),
                ));
            }
        }
    }
    let mut edit_args = rmcp::model::JsonObject::new();
    edit_args.insert("assetPath".into(), serde_json::json!(asset_path));
    write_optional_graph_address(&mut edit_args, graph_address);
    edit_args.insert("commands".into(), serde_json::Value::Array(commands));
    for field in [
        "dryRun",
        "returnDiff",
        "returnDiagnostics",
        "expectedRevision",
    ] {
        copy_if_present(args, &mut edit_args, field);
    }
    Ok(edit_args)
}

fn file_recipe_from_source(
    recipe_source: &serde_json::Map<String, serde_json::Value>,
) -> Result<serde_json::Value, CallToolResult> {
    let path = recipe_source
        .get("path")
        .and_then(|value| value.as_str())
        .or_else(|| recipe_source.get("id").and_then(|value| value.as_str()))
        .ok_or_else(|| invalid_argument_result("File recipe sources require path or id."))?;
    let data = fs::read_to_string(path).map_err(|error| {
        CallToolResult::structured_error(serde_json::json!({
            "code": "INVALID_ARGUMENT",
            "message": format!("Failed to read recipe file: {error}"),
            "suggestion": "Verify that the recipe file exists and is readable.",
            "retryable": false,
        }))
    })?;
    serde_json::from_str::<serde_json::Value>(&data).map_err(|error| {
        CallToolResult::structured_error(serde_json::json!({
            "code": "INVALID_ARGUMENT",
            "message": format!("Failed to parse recipe JSON: {error}"),
            "retryable": false,
        }))
    })
}

fn compile_builtin_recipe(
    recipe_id: &str,
    inputs: &serde_json::Map<String, serde_json::Value>,
    attach: Option<&serde_json::Map<String, serde_json::Value>>,
) -> Result<Vec<serde_json::Value>, CallToolResult> {
    let mut commands = Vec::new();
    let attach_mode = attach
        .and_then(|value| value.get("mode"))
        .and_then(|value| value.as_str())
        .unwrap_or("append_exec");
    if attach_mode != "append_exec" {
        return Err(not_implemented_result(
            "blueprint.graph.generate",
            "Only attach.mode=append_exec is implemented for built-in Blueprint recipes right now.",
        ));
    }
    let attach_from = attach.and_then(|value| value.get("from")).cloned();
    match recipe_id {
        "branch_then_call" => {
            let call_node_type = inputs
                .get("callNodeType")
                .cloned()
                .unwrap_or_else(|| serde_json::json!({ "id": "ufunction:/Script/Engine.KismetSystemLibrary:PrintString" }));
            commands.push(serde_json::json!({"kind":"addNode","alias":"branchNode","nodeType":{"kind":"branch"}}));
            commands.push(
                serde_json::json!({"kind":"addNode","alias":"callNode","nodeType":call_node_type}),
            );
            commands.push(serde_json::json!({"kind":"connect","from":{"node":{"alias":"branchNode"},"pin":"then"},"to":{"node":{"alias":"callNode"},"pin":"execute"}}));
            if let Some(condition) = inputs.get("condition") {
                commands.push(serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"branchNode"},"pin":"Condition"},"value":condition}));
            }
            if let Some(from) = attach_from {
                commands.push(serde_json::json!({"kind":"connect","from":from,"to":{"node":{"alias":"branchNode"},"pin":"execute"}}));
            }
        }
        "delay_then_call" => {
            let call_node_type = inputs
                .get("callNodeType")
                .cloned()
                .unwrap_or_else(|| serde_json::json!({ "id": "ufunction:/Script/Engine.KismetSystemLibrary:PrintString" }));
            commands.push(serde_json::json!({"kind":"addNode","alias":"delayNode","nodeType":{"id":"class:/Script/BlueprintGraph.K2Node_Delay"}}));
            if let Some(duration) = inputs.get("duration") {
                commands.push(serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"delayNode"},"pin":"Duration"},"value":duration}));
            }
            commands.push(
                serde_json::json!({"kind":"addNode","alias":"callNode","nodeType":call_node_type}),
            );
            commands.push(serde_json::json!({"kind":"connect","from":{"node":{"alias":"delayNode"},"pin":"Completed"},"to":{"node":{"alias":"callNode"},"pin":"execute"}}));
            if let Some(from) = attach_from {
                commands.push(serde_json::json!({"kind":"connect","from":from,"to":{"node":{"alias":"delayNode"},"pin":"execute"}}));
            }
        }
        "sequence_fanout" => {
            commands.push(serde_json::json!({"kind":"addNode","alias":"sequenceNode","nodeType":{"kind":"sequence"}}));
            if let Some(from) = attach_from {
                commands.push(serde_json::json!({"kind":"connect","from":from,"to":{"node":{"alias":"sequenceNode"},"pin":"execute"}}));
            }
        }
        "set_variable_then_call" => {
            let variable_name = inputs
                .get("variableName")
                .and_then(|value| value.as_str())
                .ok_or_else(|| {
                    invalid_argument_result("set_variable_then_call requires inputs.variableName.")
                })?;
            let value = inputs.get("value").cloned().ok_or_else(|| {
                invalid_argument_result("set_variable_then_call requires inputs.value.")
            })?;
            let call_node_type = inputs
                .get("callNodeType")
                .cloned()
                .unwrap_or_else(|| serde_json::json!({ "id": "ufunction:/Script/Engine.KismetSystemLibrary:PrintString" }));
            commands.push(serde_json::json!({"kind":"addNode","alias":"setNode","nodeType":{"variableName":variable_name,"mode":"set"}}));
            commands.push(serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"setNode"},"pin":variable_name},"value":value}));
            commands.push(
                serde_json::json!({"kind":"addNode","alias":"callNode","nodeType":call_node_type}),
            );
            commands.push(serde_json::json!({"kind":"connect","from":{"node":{"alias":"setNode"},"pin":"then"},"to":{"node":{"alias":"callNode"},"pin":"execute"}}));
            if let Some(from) = attach_from {
                commands.push(serde_json::json!({"kind":"connect","from":from,"to":{"node":{"alias":"setNode"},"pin":"execute"}}));
            }
        }
        "guard_clause" => {
            commands.push(serde_json::json!({"kind":"addNode","alias":"guardNode","nodeType":{"kind":"branch"}}));
            if let Some(condition) = inputs.get("condition") {
                commands.push(serde_json::json!({"kind":"setPinDefault","target":{"node":{"alias":"guardNode"},"pin":"Condition"},"value":condition}));
            }
            if let Some(from) = attach_from {
                commands.push(serde_json::json!({"kind":"connect","from":from,"to":{"node":{"alias":"guardNode"},"pin":"execute"}}));
            }
        }
        "cast_then_call" => {
            let target_type = inputs
                .get("targetType")
                .and_then(|value| value.as_str())
                .ok_or_else(|| {
                    invalid_argument_result("cast_then_call requires inputs.targetType.")
                })?;
            let call_node_type = inputs
                .get("callNodeType")
                .cloned()
                .unwrap_or_else(|| serde_json::json!({ "id": "ufunction:/Script/Engine.KismetSystemLibrary:PrintString" }));
            commands.push(serde_json::json!({"kind":"addNode","alias":"castNode","nodeType":{"kind":"cast","targetClassPath":target_type}}));
            commands.push(
                serde_json::json!({"kind":"addNode","alias":"callNode","nodeType":call_node_type}),
            );
            commands.push(serde_json::json!({"kind":"connect","from":{"node":{"alias":"castNode"},"pin":"Cast Succeeded"},"to":{"node":{"alias":"callNode"},"pin":"execute"}}));
            if let Some(from) = attach_from {
                commands.push(serde_json::json!({"kind":"connect","from":from,"to":{"node":{"alias":"castNode"},"pin":"execute"}}));
            }
        }
        other => {
            return Err(CallToolResult::structured_error(serde_json::json!({
                "code":"INVALID_ARGUMENT",
                "message": format!("Unsupported built-in recipe id: {other}"),
                "suggestion":"Use blueprint.graph.recipe.list to discover valid built-in recipes.",
                "retryable": false,
            })));
        }
    }
    Ok(commands)
}

fn compile_blueprint_generate_request(
    args: &rmcp::model::JsonObject,
) -> Result<rmcp::model::JsonObject, CallToolResult> {
    let asset_path = read_required_asset_path(args, "blueprint.graph.generate")?;
    let graph_address =
        read_optional_graph_address(args, &asset_path, true, "blueprint.graph.generate")?;
    let recipe_source = args
        .get("recipeSource")
        .and_then(|value| value.as_object())
        .ok_or_else(|| {
            invalid_argument_result("blueprint.graph.generate requires recipeSource.")
        })?;
    let kind = recipe_source
        .get("kind")
        .and_then(|value| value.as_str())
        .ok_or_else(|| invalid_argument_result("recipeSource.kind is required."))?;
    let inputs = args
        .get("inputs")
        .and_then(|value| value.as_object())
        .cloned()
        .unwrap_or_default();
    let attach = args.get("attach").and_then(|value| value.as_object());

    let commands = match kind {
        "builtIn" => {
            let recipe_id = recipe_source
                .get("id")
                .and_then(|value| value.as_str())
                .ok_or_else(|| {
                    invalid_argument_result("Built-in recipe generation requires recipeSource.id.")
                })?;
            compile_builtin_recipe(recipe_id, &inputs, attach)?
        }
        "inline" => {
            let recipe = recipe_source
                .get("recipe")
                .and_then(|value| value.as_object())
                .ok_or_else(|| {
                    invalid_argument_result(
                        "Inline recipe generation requires recipeSource.recipe.",
                    )
                })?;
            recipe
                .get("commands")
                .and_then(|value| value.as_array())
                .cloned()
                .or_else(|| {
                    recipe
                        .get("steps")
                        .and_then(|value| value.as_array())
                        .cloned()
                })
                .ok_or_else(|| {
                    invalid_argument_result("Inline recipes must provide commands or steps arrays.")
                })?
        }
        "file" => {
            let recipe = file_recipe_from_source(recipe_source)?;
            let recipe_obj = recipe.as_object().ok_or_else(|| {
                invalid_argument_result("File recipe contents must be an object.")
            })?;
            recipe_obj
                .get("commands")
                .and_then(|value| value.as_array())
                .cloned()
                .or_else(|| {
                    recipe_obj
                        .get("steps")
                        .and_then(|value| value.as_array())
                        .cloned()
                })
                .ok_or_else(|| {
                    invalid_argument_result("File recipes must provide commands or steps arrays.")
                })?
        }
        other => {
            return Err(invalid_argument_result(format!(
                "Unsupported recipeSource.kind: {other}"
            )));
        }
    };

    let mut edit_args = rmcp::model::JsonObject::new();
    edit_args.insert("assetPath".into(), serde_json::json!(asset_path));
    write_optional_graph_address(&mut edit_args, graph_address);
    edit_args.insert("commands".into(), serde_json::Value::Array(commands));
    for field in [
        "dryRun",
        "returnDiff",
        "returnDiagnostics",
        "expectedRevision",
    ] {
        copy_if_present(args, &mut edit_args, field);
    }
    Ok(edit_args)
}

fn pre_attach_tools() -> Vec<Tool> {
    use std::sync::Arc;
    vec![
        Tool::new(
            "loomle.status",
            "Return the current LOOMLE MCP session status, including whether this session is attached to a UE project.",
            Arc::new(empty_schema()),
        ),
        Tool::new(
            "project.list",
            "List Unreal Engine projects known to LOOMLE. Defaults to online projects.",
            Arc::new(project_list_schema()),
        ),
        Tool::new(
            "project.attach",
            "Attach this MCP session to an online LOOMLE-enabled Unreal Engine project.",
            Arc::new(project_attach_schema()),
        ),
        Tool::new(
            "project.install",
            "Install or update LOOMLE support for an Unreal Engine project, including the LoomleBridge plugin and required project settings.",
            Arc::new(project_install_schema()),
        ),
    ]
}

fn all_declared_tools() -> Vec<Tool> {
    let mut tools = pre_attach_tools();
    tools.extend(runtime_declared_tools());
    tools
}

fn runtime_declared_tools() -> Vec<Tool> {
    use std::sync::Arc;
    vec![
        Tool::new("loomle", "Bridge health and runtime status.", Arc::new(empty_schema())),
        Tool::new("context", "Read active editor context and selection.", Arc::new(context_schema())),
        Tool::new("execute", "Execute Unreal-side Python inside the editor process.", Arc::new(execute_schema())),
        Tool::new("jobs", "Inspect or retrieve long-running job state, results, and logs.", Arc::new(jobs_schema())),
        Tool::new("profiling", "Bridge official Unreal profiling data families such as stat unit, stat groups, ticks, memory reports, and capture workflows.", Arc::new(profiling_schema())),
        Tool::new("play", "Inspect and control Unreal play sessions; supports PIE status, start, stop, and wait.", Arc::new(play_schema())),
        Tool::new("editor.open", "Open or focus the editor for a specific Unreal asset path.", Arc::new(editor_open_schema())),
        Tool::new("editor.focus", "Focus a semantic panel inside an asset editor, such as graph, viewport, details, palette, or find.", Arc::new(editor_focus_schema())),
        Tool::new("editor.screenshot", "Capture a PNG of the active editor window and return the written file path.", Arc::new(editor_screenshot_schema())),
        Tool::new("blueprint.inspect", "Inspect a Blueprint and its class-level contract.", Arc::new(blueprint_inspect_schema())),
        Tool::new("blueprint.edit", "Edit Blueprint-level properties and relationships.", Arc::new(blueprint_edit_schema())),
        Tool::new("blueprint.enum.inspect", "Inspect a Blueprint user-defined enum asset.", Arc::new(blueprint_enum_inspect_schema())),
        Tool::new("blueprint.enum.edit", "Create or edit a Blueprint user-defined enum asset.", Arc::new(blueprint_enum_edit_schema())),
        Tool::new("blueprint.member.inspect", "Inspect Blueprint members such as variables, functions, macros, dispatchers, events, and components.", Arc::new(blueprint_member_inspect_schema())),
        Tool::new("blueprint.member.edit", "Edit Blueprint members such as variables, functions, macros, dispatchers, events, and components.", Arc::new(blueprint_member_edit_schema())),
        Tool::new("blueprint.graph.list", "List Blueprint graphs in an asset.", Arc::new(blueprint_graph_list_schema())),
        Tool::new("blueprint.graph.inspect", "Read graph, node, pin, and link data from a Blueprint graph.", Arc::new(blueprint_graph_inspect_schema())),
        Tool::new("blueprint.graph.edit", "Apply explicit local graph edit commands to a Blueprint graph.", Arc::new(blueprint_graph_edit_schema())),
        Tool::new("blueprint.graph.refactor", "Apply structural graph refactors to a Blueprint graph.", Arc::new(blueprint_graph_refactor_schema())),
        Tool::new("blueprint.graph.generate", "Generate a Blueprint graph snippet from a recipe source.", Arc::new(blueprint_graph_generate_schema())),
        Tool::new("blueprint.graph.recipe.list", "List discoverable Blueprint graph recipes.", Arc::new(blueprint_graph_recipe_list_schema())),
        Tool::new("blueprint.graph.recipe.inspect", "Inspect the contract of a Blueprint graph recipe.", Arc::new(blueprint_graph_recipe_inspect_schema())),
        Tool::new("blueprint.graph.recipe.validate", "Validate a Blueprint graph recipe definition.", Arc::new(blueprint_graph_recipe_validate_schema())),
        Tool::new("blueprint.palette", "Search Blueprint palette for addable nodes.", Arc::new(blueprint_palette_schema())),
        Tool::new("blueprint.compile", "Compile a Blueprint asset.", Arc::new(blueprint_compile_schema())),
        Tool::new("blueprint.validate", "Run read-only structural validation for a Blueprint graph or asset.", Arc::new(blueprint_validate_schema())),
        Tool::new("material.list", "List material expressions in a material asset.", Arc::new(asset_path_only_schema("Material asset path."))),
        Tool::new("material.query", "Read expression nodes and pin data from a material.", Arc::new(material_query_schema())),
        Tool::new("material.mutate", "Apply a batch of write operations to a material asset.", Arc::new(material_mutate_schema())),
        Tool::new("material.verify", "Compile a material and return diagnostics.", Arc::new(asset_path_only_schema("Material asset path."))),
        Tool::new("material.describe", "Describe a material expression class or instance.", Arc::new(material_describe_schema())),
        Tool::new("pcg.list", "List nodes in a PCG graph asset.", Arc::new(asset_path_only_schema("PCG graph asset path."))),
        Tool::new("pcg.query", "Read node and pin data from a PCG graph.", Arc::new(pcg_query_schema())),
        Tool::new("pcg.mutate", "Apply a batch of write operations to a PCG graph.", Arc::new(pcg_mutate_schema())),
        Tool::new("pcg.verify", "Run read-only validation for a PCG graph.", Arc::new(asset_path_only_schema("PCG graph asset path."))),
        Tool::new("pcg.describe", "Describe a PCG settings class or instance.", Arc::new(pcg_describe_schema())),
        Tool::new("diagnostic.tail", "Read persisted structured diagnostics incrementally by sequence cursor.", Arc::new(diagnostic_tail_schema())),
        Tool::new("log.tail", "Read persisted Unreal output log events incrementally by sequence cursor.", Arc::new(log_tail_schema())),
        Tool::new("widget.query", "Read the UMG WidgetTree of a WidgetBlueprint asset.", Arc::new(widget_query_schema())),
        Tool::new("widget.mutate", "Apply structural write operations to the UMG WidgetTree of a WidgetBlueprint asset.", Arc::new(widget_mutate_schema())),
        Tool::new("widget.verify", "Compile a WidgetBlueprint and return diagnostics.", Arc::new(asset_path_only_schema("WidgetBlueprint asset path."))),
        Tool::new("widget.describe", "Enumerate the editable properties of a UMG widget class, with optional current values from a live instance.", Arc::new(widget_describe_schema())),
    ]
}

fn bridge_unavailable_result(reason: &str) -> CallToolResult {
    CallToolResult::error(vec![rmcp::model::Content::text(format!(
        "LOOMLE Bridge unavailable: {reason}"
    ))])
}

fn is_local_tool(name: &str) -> bool {
    matches!(
        name,
        "loomle" | "loomle.status" | "project.list" | "project.attach" | "project.install"
    )
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

fn not_implemented_result(tool_name: &str, detail: &str) -> CallToolResult {
    CallToolResult::structured_error(serde_json::json!({
        "isError": true,
        "code": "NOT_IMPLEMENTED",
        "message": format!("{tool_name} is not implemented yet."),
        "detail": detail,
        "retryable": false,
    }))
}

fn builtin_blueprint_recipe_ids() -> &'static [&'static str] {
    &[
        "branch_then_call",
        "delay_then_call",
        "sequence_fanout",
        "set_variable_then_call",
        "guard_clause",
        "cast_then_call",
    ]
}

fn builtin_blueprint_recipe(id: &str) -> Option<serde_json::Value> {
    let recipe = match id {
        "branch_then_call" => serde_json::json!({
            "kind": "builtIn",
            "id": "branch_then_call",
            "title": "Branch Then Call",
            "summary": "Create a Branch and route the true path into a call node.",
            "description": "Builds a control-flow snippet that evaluates a boolean condition and routes the true branch into a single call node.",
            "version": "1",
            "inputs": [
                {"name":"condition","type":"bool","required":false,"default":null,"description":"Optional bool source for Branch.Condition."},
                {"name":"callNodeType","type":"nodeType","required":true,"default":null,"description":"The call node type placed on the true branch."}
            ],
            "attach": {
                "modes": ["append_exec", "insert_between_exec"],
                "entryPoints": ["exec_in"],
                "exitPoints": ["then_true", "then_false"],
                "constraints": []
            },
            "outputs": [
                {"name":"branchNode","kind":"node","description":"Created Branch node."},
                {"name":"callNode","kind":"node","description":"Created call node on the true branch."}
            ],
            "structureSummary": {
                "nodeCount": 2,
                "linkCount": 2,
                "containsExecFlow": true,
                "containsLatentFlow": false,
                "primaryNodeKinds": ["branch", "call"]
            },
            "steps": [
                "Create a Branch node.",
                "Create the requested call node.",
                "Connect Branch.then to the call execute pin."
            ],
            "constraints": [],
            "example": {
                "inputs": {"callNodeType":"ufunction:/Script/Engine.KismetSystemLibrary:PrintString"},
                "attach": {"mode":"append_exec"},
                "expectedOutputs": ["branchNode", "callNode"]
            },
            "tags": ["flow", "branch", "call"],
            "mutable": false
        }),
        "delay_then_call" => serde_json::json!({
            "kind": "builtIn",
            "id": "delay_then_call",
            "title": "Delay Then Call",
            "summary": "Create a Delay node followed by a call node.",
            "description": "Builds a latent execution snippet that waits for a duration and then executes a call node.",
            "version": "1",
            "inputs": [
                {"name":"duration","type":"float","required":false,"default":0.2,"description":"Delay duration in seconds."},
                {"name":"callNodeType","type":"nodeType","required":true,"default":null,"description":"The call node type placed after the delay."}
            ],
            "attach": {
                "modes": ["append_exec", "insert_between_exec"],
                "entryPoints": ["exec_in"],
                "exitPoints": ["then"],
                "constraints": []
            },
            "outputs": [
                {"name":"delayNode","kind":"node","description":"Created Delay node."},
                {"name":"callNode","kind":"node","description":"Created call node after the delay."}
            ],
            "structureSummary": {
                "nodeCount": 2,
                "linkCount": 2,
                "containsExecFlow": true,
                "containsLatentFlow": true,
                "primaryNodeKinds": ["call"]
            },
            "steps": [
                "Create a Delay node.",
                "Create the requested call node.",
                "Connect Delay.Completed to the call execute pin."
            ],
            "constraints": [],
            "example": {
                "inputs": {"duration":0.2,"callNodeType":"ufunction:/Script/Engine.KismetSystemLibrary:PrintString"},
                "attach": {"mode":"append_exec"},
                "expectedOutputs": ["delayNode", "callNode"]
            },
            "tags": ["flow", "latent", "call"],
            "mutable": false
        }),
        "sequence_fanout" => serde_json::json!({
            "kind": "builtIn",
            "id": "sequence_fanout",
            "title": "Sequence Fanout",
            "summary": "Create a Sequence node with configurable execution branches.",
            "description": "Builds a Sequence node that fans one execution input into multiple deterministic branches.",
            "version": "1",
            "inputs": [
                {"name":"branches","type":"int","required":false,"default":2,"description":"Number of sequence outputs to create."}
            ],
            "attach": {
                "modes": ["append_exec", "insert_between_exec"],
                "entryPoints": ["exec_in"],
                "exitPoints": ["then"],
                "constraints": []
            },
            "outputs": [
                {"name":"sequenceNode","kind":"node","description":"Created Sequence node."}
            ],
            "structureSummary": {
                "nodeCount": 1,
                "linkCount": 0,
                "containsExecFlow": true,
                "containsLatentFlow": false,
                "primaryNodeKinds": ["sequence"]
            },
            "steps": [
                "Create a Sequence node.",
                "Resize execution outputs to the requested branch count."
            ],
            "constraints": [],
            "example": {
                "inputs": {"branches":3},
                "attach": {"mode":"append_exec"},
                "expectedOutputs": ["sequenceNode"]
            },
            "tags": ["flow", "sequence", "fanout"],
            "mutable": false
        }),
        "set_variable_then_call" => serde_json::json!({
            "kind": "builtIn",
            "id": "set_variable_then_call",
            "title": "Set Variable Then Call",
            "summary": "Set a variable and then invoke a call node.",
            "description": "Builds a small execution snippet that writes one Blueprint variable and then executes a call node.",
            "version": "1",
            "inputs": [
                {"name":"variableName","type":"string","required":true,"default":null,"description":"Blueprint variable to set."},
                {"name":"value","type":"wildcard","required":true,"default":null,"description":"Value assigned to the variable set pin."},
                {"name":"callNodeType","type":"nodeType","required":true,"default":null,"description":"The call node type executed after the variable set."}
            ],
            "attach": {
                "modes": ["append_exec", "insert_between_exec"],
                "entryPoints": ["exec_in"],
                "exitPoints": ["then"],
                "constraints": []
            },
            "outputs": [
                {"name":"setNode","kind":"node","description":"Created variable set node."},
                {"name":"callNode","kind":"node","description":"Created call node."}
            ],
            "structureSummary": {
                "nodeCount": 2,
                "linkCount": 2,
                "containsExecFlow": true,
                "containsLatentFlow": false,
                "primaryNodeKinds": ["variable_set", "call"]
            },
            "steps": [
                "Create a variable set node.",
                "Bind the requested value to the variable input.",
                "Create the requested call node and connect execution."
            ],
            "constraints": [],
            "example": {
                "inputs": {"variableName":"bIsReady","value":true,"callNodeType":"ufunction:/Script/Engine.KismetSystemLibrary:PrintString"},
                "attach": {"mode":"append_exec"},
                "expectedOutputs": ["setNode", "callNode"]
            },
            "tags": ["variable", "call", "flow"],
            "mutable": false
        }),
        "guard_clause" => serde_json::json!({
            "kind": "builtIn",
            "id": "guard_clause",
            "title": "Guard Clause",
            "summary": "Wrap execution with a boolean guard.",
            "description": "Builds a Branch-based guard clause that only allows downstream execution when the provided condition is true.",
            "version": "1",
            "inputs": [
                {"name":"condition","type":"bool","required":true,"default":null,"description":"Condition evaluated by the guard."}
            ],
            "attach": {
                "modes": ["append_exec", "insert_between_exec"],
                "entryPoints": ["exec_in"],
                "exitPoints": ["then_true"],
                "constraints": []
            },
            "outputs": [
                {"name":"guardNode","kind":"node","description":"Created Branch node implementing the guard."}
            ],
            "structureSummary": {
                "nodeCount": 1,
                "linkCount": 0,
                "containsExecFlow": true,
                "containsLatentFlow": false,
                "primaryNodeKinds": ["branch"]
            },
            "steps": [
                "Create a Branch node.",
                "Bind the requested condition to Branch.Condition."
            ],
            "constraints": [],
            "example": {
                "inputs": {"condition":true},
                "attach": {"mode":"insert_between_exec"},
                "expectedOutputs": ["guardNode"]
            },
            "tags": ["flow", "branch", "guard"],
            "mutable": false
        }),
        "cast_then_call" => serde_json::json!({
            "kind": "builtIn",
            "id": "cast_then_call",
            "title": "Cast Then Call",
            "summary": "Cast an object to a target type and then invoke a call node.",
            "description": "Builds a cast node followed by a call node on the success path.",
            "version": "1",
            "inputs": [
                {"name":"targetType","type":"class","required":true,"default":null,"description":"Target class for the cast."},
                {"name":"callNodeType","type":"nodeType","required":true,"default":null,"description":"The call node type placed after a successful cast."}
            ],
            "attach": {
                "modes": ["append_exec", "insert_between_exec", "attach_data_input"],
                "entryPoints": ["exec_in", "object_in"],
                "exitPoints": ["then_true", "cast_result"],
                "constraints": []
            },
            "outputs": [
                {"name":"castNode","kind":"node","description":"Created cast node."},
                {"name":"callNode","kind":"node","description":"Created call node on cast success."}
            ],
            "structureSummary": {
                "nodeCount": 2,
                "linkCount": 2,
                "containsExecFlow": true,
                "containsLatentFlow": false,
                "primaryNodeKinds": ["cast", "call"]
            },
            "steps": [
                "Create a cast node for the requested target class.",
                "Create the requested call node.",
                "Connect cast success to the call execute pin."
            ],
            "constraints": [],
            "example": {
                "inputs": {"targetType":"/Script/Engine.Actor","callNodeType":"ufunction:/Script/Engine.KismetSystemLibrary:PrintString"},
                "attach": {"mode":"append_exec"},
                "expectedOutputs": ["castNode", "callNode"]
            },
            "tags": ["cast", "call", "flow"],
            "mutable": false
        }),
        _ => return None,
    };
    Some(recipe)
}

fn builtin_blueprint_recipe_summaries() -> Vec<serde_json::Value> {
    builtin_blueprint_recipe_ids()
        .iter()
        .filter_map(|id| builtin_blueprint_recipe(id))
        .map(|recipe| {
            serde_json::json!({
                "kind": recipe.get("kind").cloned().unwrap_or(serde_json::Value::Null),
                "id": recipe.get("id").cloned().unwrap_or(serde_json::Value::Null),
                "title": recipe.get("title").cloned().unwrap_or(serde_json::Value::Null),
                "summary": recipe.get("summary").cloned().unwrap_or(serde_json::Value::Null),
                "inputs": recipe.get("inputs").cloned().unwrap_or(serde_json::json!([])),
                "attachModes": recipe
                    .get("attach")
                    .and_then(|attach| attach.get("modes"))
                    .cloned()
                    .unwrap_or(serde_json::json!([])),
                "outputs": recipe.get("outputs").cloned().unwrap_or(serde_json::json!([])),
                "tags": recipe.get("tags").cloned().unwrap_or(serde_json::json!([])),
                "mutable": recipe.get("mutable").cloned().unwrap_or(serde_json::json!(false))
            })
        })
        .collect()
}

fn call_blueprint_graph_recipe_list(args: &rmcp::model::JsonObject) -> CallToolResult {
    let kind = args.get("kind").and_then(|value| value.as_str());
    let tag = args.get("tag").and_then(|value| value.as_str());
    let recipes = if matches!(kind, Some("file")) {
        Vec::new()
    } else {
        builtin_blueprint_recipe_summaries()
            .into_iter()
            .filter(|recipe| {
                tag.is_none_or(|needle| {
                    recipe
                        .get("tags")
                        .and_then(|value| value.as_array())
                        .is_some_and(|tags| tags.iter().any(|tag| tag.as_str() == Some(needle)))
                })
            })
            .collect::<Vec<_>>()
    };
    structured_result(serde_json::json!({
        "recipes": recipes
    }))
}

fn call_blueprint_graph_recipe_inspect(args: &rmcp::model::JsonObject) -> CallToolResult {
    let Some(kind) = args.get("kind").and_then(|value| value.as_str()) else {
        return CallToolResult::structured_error(serde_json::json!({
            "code": "INVALID_ARGUMENT",
            "message": "blueprint.graph.recipe.inspect requires kind.",
            "retryable": false
        }));
    };
    let Some(id) = args.get("id").and_then(|value| value.as_str()) else {
        return CallToolResult::structured_error(serde_json::json!({
            "code": "INVALID_ARGUMENT",
            "message": "blueprint.graph.recipe.inspect requires id.",
            "retryable": false
        }));
    };
    match kind {
        "builtIn" => match builtin_blueprint_recipe(id) {
            Some(recipe) => structured_result(recipe),
            None => CallToolResult::structured_error(serde_json::json!({
                "code": "RECIPE_NOT_FOUND",
                "message": format!("Built-in Blueprint recipe not found: {id}"),
                "suggestion": "Use blueprint.graph.recipe.list to discover valid built-in recipe ids.",
                "retryable": false
            })),
        },
        "file" => {
            let recipe = match file_recipe_from_source(&serde_json::Map::from_iter([
                ("kind".into(), serde_json::json!("file")),
                ("id".into(), serde_json::json!(id)),
            ])) {
                Ok(value) => value,
                Err(error) => return error,
            };
            structured_result(recipe)
        }
        other => CallToolResult::structured_error(serde_json::json!({
            "code": "INVALID_ARGUMENT",
            "message": format!("Unsupported recipe kind: {other}"),
            "retryable": false
        })),
    }
}

fn validate_recipe_shape(
    recipe: &serde_json::Value,
    kind: &str,
    source: serde_json::Value,
) -> CallToolResult {
    let mut diagnostics = Vec::new();

    let title_ok = recipe
        .get("title")
        .and_then(|value| value.as_str())
        .is_some_and(|value| !value.trim().is_empty());
    if !title_ok {
        diagnostics.push(serde_json::json!({
            "level": "error",
            "code": "RECIPE_TITLE_REQUIRED",
            "message": "Recipe title is required.",
            "reason": "The recipe object must contain a non-empty title string.",
            "suggestion": "Add a non-empty title field to the recipe definition."
        }));
    }

    if let Some(inputs) = recipe.get("inputs") {
        if !inputs.is_array() {
            diagnostics.push(serde_json::json!({
                "level": "error",
                "code": "RECIPE_INPUTS_MUST_BE_ARRAY",
                "message": "Recipe inputs must be an array.",
                "reason": "The recipe input contract is defined as an array of input definitions.",
                "suggestion": "Replace the inputs field with an array of input definition objects."
            }));
        }
    }

    if let Some(attach) = recipe.get("attach") {
        if !attach.is_object() {
            diagnostics.push(serde_json::json!({
                "level": "error",
                "code": "RECIPE_ATTACH_MUST_BE_OBJECT",
                "message": "Recipe attach contract must be an object.",
                "reason": "Attach metadata is expected to be a structured object.",
                "suggestion": "Replace the attach field with an object describing modes, entry points, and constraints."
            }));
        }
    }

    if let Some(outputs) = recipe.get("outputs") {
        if !outputs.is_array() {
            diagnostics.push(serde_json::json!({
                "level": "error",
                "code": "RECIPE_OUTPUTS_MUST_BE_ARRAY",
                "message": "Recipe outputs must be an array.",
                "reason": "Recipe outputs are declared as an array of named output definitions.",
                "suggestion": "Replace the outputs field with an array of output definition objects."
            }));
        }
    }

    if let Some(steps) = recipe.get("steps") {
        if !steps.is_array() {
            diagnostics.push(serde_json::json!({
                "level": "error",
                "code": "RECIPE_STEPS_MUST_BE_ARRAY",
                "message": "Recipe steps must be an array.",
                "reason": "Recipe steps are represented as an ordered list of semantic steps.",
                "suggestion": "Replace the steps field with an array of strings or structured step objects."
            }));
        }
    }

    structured_result(serde_json::json!({
        "kind": kind,
        "source": source,
        "valid": diagnostics.iter().all(|diagnostic| diagnostic.get("level").and_then(|v| v.as_str()) != Some("error")),
        "diagnostics": diagnostics
    }))
}

fn call_blueprint_graph_recipe_validate(args: &rmcp::model::JsonObject) -> CallToolResult {
    let Some(recipe_source) = args.get("recipeSource").and_then(|value| value.as_object()) else {
        return CallToolResult::structured_error(serde_json::json!({
            "code": "INVALID_ARGUMENT",
            "message": "blueprint.graph.recipe.validate requires recipeSource.",
            "retryable": false
        }));
    };
    let Some(kind) = recipe_source.get("kind").and_then(|value| value.as_str()) else {
        return CallToolResult::structured_error(serde_json::json!({
            "code": "INVALID_ARGUMENT",
            "message": "recipeSource.kind is required.",
            "retryable": false
        }));
    };
    match kind {
        "inline" => {
            let Some(recipe) = recipe_source.get("recipe") else {
                return CallToolResult::structured_error(serde_json::json!({
                    "code": "INVALID_ARGUMENT",
                    "message": "Inline recipe validation requires recipeSource.recipe.",
                    "retryable": false
                }));
            };
            validate_recipe_shape(recipe, "inline", serde_json::json!({"kind":"inline"}))
        }
        "file" => {
            if let Some(path) = recipe_source.get("path").and_then(|value| value.as_str()) {
                match fs::read_to_string(path) {
                    Ok(raw) => match serde_json::from_str::<serde_json::Value>(&raw) {
                        Ok(recipe) => validate_recipe_shape(
                            &recipe,
                            "file",
                            serde_json::json!({"kind":"file","path":path}),
                        ),
                        Err(error) => CallToolResult::structured_error(serde_json::json!({
                            "code": "RECIPE_PARSE_FAILED",
                            "message": format!("Failed to parse recipe JSON: {error}"),
                            "suggestion": "Ensure the file contains valid JSON.",
                            "retryable": false
                        })),
                    },
                    Err(error) => CallToolResult::structured_error(serde_json::json!({
                        "code": "RECIPE_READ_FAILED",
                        "message": format!("Failed to read recipe file: {error}"),
                        "suggestion": "Verify that the recipe file exists and is readable.",
                        "retryable": false
                    })),
                }
            } else {
                not_implemented_result(
                    "blueprint.graph.recipe.validate",
                    "File recipe validation currently requires recipeSource.path.",
                )
            }
        }
        "builtIn" => {
            let Some(id) = recipe_source.get("id").and_then(|value| value.as_str()) else {
                return CallToolResult::structured_error(serde_json::json!({
                    "code": "INVALID_ARGUMENT",
                    "message": "Built-in recipe validation requires recipeSource.id.",
                    "retryable": false
                }));
            };
            match builtin_blueprint_recipe(id) {
                Some(recipe) => validate_recipe_shape(
                    &recipe,
                    "builtIn",
                    serde_json::json!({"kind":"builtIn","id":id}),
                ),
                None => CallToolResult::structured_error(serde_json::json!({
                    "code": "RECIPE_NOT_FOUND",
                    "message": format!("Built-in Blueprint recipe not found: {id}"),
                    "suggestion": "Use blueprint.graph.recipe.list to discover valid built-in recipe ids.",
                    "retryable": false
                })),
            }
        }
        other => CallToolResult::structured_error(serde_json::json!({
            "code": "INVALID_ARGUMENT",
            "message": format!("Unsupported recipeSource.kind: {other}"),
            "retryable": false
        })),
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

fn graph_mutate_schema(op_names: &[&str], include_graph_name: bool) -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    if include_graph_name {
        properties.insert(
            "graphName".into(),
            serde_json::json!({"type":"string","minLength":1,"default":"EventGraph"}),
        );
    }
    properties.insert(
        "expectedRevision".into(),
        serde_json::json!({"type":"string"}),
    );
    properties.insert(
        "idempotencyKey".into(),
        serde_json::json!({"type":"string"}),
    );
    properties.insert(
        "dryRun".into(),
        serde_json::json!({"type":"boolean","default":false}),
    );
    properties.insert(
        "continueOnError".into(),
        serde_json::json!({"type":"boolean","default":false}),
    );
    properties.insert(
        "ops".into(),
        serde_json::json!({
            "type": "array",
            "minItems": 1,
            "maxItems": 200,
            "items": {
                "type": "object",
                "properties": {
                    "op": { "type": "string", "enum": op_names },
                    "clientRef": { "type": "string" },
                    "graphName": { "type": "string" },
                    "newName": { "type": "string" },
                    "nodeId": { "type": "string" },
                    "nodeRef": { "type": "string" },
                    "nodePath": { "type": "string" },
                    "nodeName": { "type": "string" },
                    "name": { "type": "string" },
                    "nodeClass": { "type": "string" },
                    "functionClass": { "type": "string" },
                    "functionName": { "type": "string" },
                    "eventName": { "type": "string" },
                    "eventClass": { "type": "string" },
                    "variableName": { "type": "string" },
                    "variableClass": { "type": "string" },
                    "mode": { "type": "string", "enum": ["get","set","exec","eval","sync","job"] },
                    "macroLibrary": { "type": "string" },
                    "macroName": { "type": "string" },
                    "targetClass": { "type": "string" },
                    "text": { "type": "string" },
                    "comment": { "type": "string" },
                    "enabled": { "type": "boolean" },
                    "width": { "type": "integer" },
                    "height": { "type": "integer" },
                    "x": { "type": "integer" },
                    "y": { "type": "integer" },
                    "dx": { "type": "integer" },
                    "dy": { "type": "integer" },
                    "pinName": { "type": "string" },
                    "fromPin": { "type": "string" },
                    "toPin": { "type": "string" },
                    "fromNodeId": { "type": "string" },
                    "fromNodeRef": { "type": "string" },
                    "toNodeId": { "type": "string" },
                    "toNodeRef": { "type": "string" },
                    "value": { "type": "string" },
                    "property": { "type": "string" },
                    "algorithm": { "type": "string" },
                    "scope": { "type": "string", "enum": ["touched","all"] },
                    "nodeIds": { "type": "array", "items": { "type": "string" } },
                    "nodes": {
                        "type": "array",
                        "items": {
                            "type": "object",
                            "properties": {
                                "nodeId": { "type": "string" },
                                "dx": { "type": "integer" },
                                "dy": { "type": "integer" }
                            },
                            "additionalProperties": false
                        }
                    },
                    "from": { "type": "object", "additionalProperties": true },
                    "to": { "type": "object", "additionalProperties": true },
                    "target": { "type": "object", "additionalProperties": true }
                },
                "required": ["op"],
                "additionalProperties": false
            }
        }),
    );
    schema_from_value(serde_json::Value::Object(serde_json::Map::from_iter([
        ("type".into(), serde_json::json!("object")),
        ("properties".into(), serde_json::Value::Object(properties)),
        ("required".into(), serde_json::json!(["assetPath", "ops"])),
        ("additionalProperties".into(), serde_json::json!(false)),
    ])))
}

fn context_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "resolveIds":{"type":"array","items":{"type":"string"}},
            "resolveFields":{"type":"array","items":{"type":"string"}}
        },
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
                    "waitMs":{"type":"integer"}
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

fn blueprint_inspect_schema() -> rmcp::model::JsonObject {
    asset_path_only_schema("Blueprint asset path.")
}

fn blueprint_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "operation".into(),
        serde_json::json!({"type":"string","enum":["create","duplicate","rename","delete","reparent","setMetadata","getDefaults","setDefaults","setParent","listInterfaces","addInterface","removeInterface"]}),
    );
    properties.insert("args".into(), serde_json::json!({"type":"object"}));
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","operation"],
        "additionalProperties": false
    }))
}

fn blueprint_enum_inspect_schema() -> rmcp::model::JsonObject {
    asset_path_only_schema("Blueprint enum asset path.")
}

fn blueprint_enum_edit_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "operation".into(),
        serde_json::json!({"type":"string","enum":["create","updateEntries"]}),
    );
    properties.insert("args".into(), serde_json::json!({"type":"object"}));
    mutation_control_fields(&mut properties);
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
        serde_json::json!({"type":"string","enum":["variable","function","macro","dispatcher","event","customEvent","component"]}),
    );
    properties.insert(
        "operation".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("args".into(), serde_json::json!({"type":"object"}));
    mutation_control_fields(&mut properties);
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
            "graphName":{"type":"string","minLength":1},
            "nodeIds":{"type":"array","items":{"type":"string"}},
            "nodeClasses":{"type":"array","items":{"type":"string"}},
            "includeComments":{"type":"boolean","default":false},
            "includePinDefaults":{"type":"boolean","default":false},
            "includeConnections":{"type":"boolean","default":false},
            "limit":{"type":"integer"},
            "cursor":{"type":"string"},
            "layoutDetail":{"type":"string","enum":["basic","measured"]}
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
        "graphName".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "commands".into(),
        serde_json::json!({
            "type":"array",
            "items":{
                "type":"object",
                "properties":{
                    "kind":{"type":"string","minLength":1},
                    "alias":{"type":"string","minLength":1},
                    "node":{"type":"object"},
                    "nodeType":{"type":"object"},
                    "from":{"type":"object"},
                    "to":{"type":"object"},
                    "fromNode":{"type":"object"},
                    "toNode":{"type":"object"},
                    "target":{"type":"object"},
                    "value":{},
                    "position":{"type":"object"},
                    "anchor":{"type":"object"},
                    "preserveLinks":{"type":"boolean"}
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

fn blueprint_graph_refactor_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), graph_ref_schema());
    properties.insert(
        "graphName".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "transforms".into(),
        serde_json::json!({
            "type":"array",
            "items":{
                "type":"object",
                "properties":{
                    "kind":{"type":"string","enum":["insertBetween","replaceNode","wrapWith","fanoutExec","cleanupReroutes"]},
                    "alias":{"type":"string","minLength":1},
                    "link":{"type":"object"},
                    "nodeType":{"type":"object"},
                    "inputPin":{"type":"string","minLength":1},
                    "outputPin":{"type":"string","minLength":1},
                    "target":{"type":"object"},
                    "replacement":{"type":"object"},
                    "wrapper":{"type":"object"},
                    "source":{"type":"object"},
                    "targets":{"type":"array","items":{"type":"object"}},
                    "rebindPolicy":{"type":"string","enum":["none","matchingPins"]},
                    "removeOriginal":{"type":"boolean"},
                    "entryPin":{"type":"string","minLength":1},
                    "targetEntryPin":{"type":"string","minLength":1},
                    "wrapperExitPin":{"type":"string","minLength":1}
                },
                "required":["kind"],
                "additionalProperties": false
            }
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","transforms"],
        "additionalProperties": false
    }))
}

fn blueprint_graph_generate_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), graph_ref_schema());
    properties.insert(
        "graphName".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "recipeSource".into(),
        serde_json::json!({
            "type":"object",
            "properties":{
                "kind":{"type":"string","enum":["builtIn","file","inline"]},
                "id":{"type":"string","minLength":1},
                "path":{"type":"string","minLength":1},
                "recipe":{"type":"object"}
            },
            "required":["kind"],
            "additionalProperties": false
        }),
    );
    properties.insert("inputs".into(), serde_json::json!({"type":"object"}));
    properties.insert("attach".into(), serde_json::json!({"type":"object"}));
    properties.insert(
        "outputBindings".into(),
        serde_json::json!({"type":"object"}),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath","recipeSource"],
        "additionalProperties": false
    }))
}

fn blueprint_graph_recipe_list_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "kind":{"type":"string","enum":["builtIn","file"]},
            "tag":{"type":"string","minLength":1}
        },
        "additionalProperties": false
    }))
}

fn blueprint_graph_recipe_inspect_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "kind":{"type":"string","enum":["builtIn","file"]},
            "id":{"type":"string","minLength":1},
            "path":{"type":"string","minLength":1}
        },
        "required":["kind"],
        "additionalProperties": false
    }))
}

fn blueprint_graph_recipe_validate_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "recipeSource".into(),
        serde_json::json!({
            "type":"object",
            "properties":{
                "kind":{"type":"string","enum":["builtIn","file","inline"]},
                "id":{"type":"string","minLength":1},
                "path":{"type":"string","minLength":1},
                "recipe":{"type":"object"}
            },
            "required":["kind"],
            "additionalProperties": false
        }),
    );
    mutation_control_fields(&mut properties);
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["recipeSource"],
        "additionalProperties": false
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
        serde_json::json!({"type":"string","minLength":1}),
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
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("query".into(), serde_json::json!({"type":"string"}));
    properties.insert("family".into(), serde_json::json!({"type":"string"}));
    properties.insert(
        "limit".into(),
        serde_json::json!({"type":"integer","minimum":1,"default":100}),
    );
    properties.insert(
        "offset".into(),
        serde_json::json!({"type":"integer","minimum":0,"default":0}),
    );
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath"],
        "additionalProperties": false
    }))
}

fn blueprint_validate_schema() -> rmcp::model::JsonObject {
    let mut properties = serde_json::Map::new();
    properties.insert(
        "assetPath".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert("graph".into(), graph_ref_schema());
    properties.insert(
        "graphName".into(),
        serde_json::json!({"type":"string","minLength":1}),
    );
    properties.insert(
        "returnDiagnostics".into(),
        serde_json::json!({"type":"boolean","default":true}),
    );
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties": properties,
        "required":["assetPath"],
        "additionalProperties": false
    }))
}

fn material_query_schema() -> rmcp::model::JsonObject {
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

fn material_mutate_schema() -> rmcp::model::JsonObject {
    graph_mutate_schema(
        &[
            "addNode.byClass",
            "removeNode",
            "moveNode",
            "moveNodeBy",
            "moveNodes",
            "connectPins",
            "disconnectPins",
            "setProperty",
            "layoutGraph",
            "compile",
        ],
        false,
    )
}

fn material_describe_schema() -> rmcp::model::JsonObject {
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

fn pcg_query_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type": "object",
        "properties": {
            "assetPath": { "type": "string", "minLength": 1, "description": "PCG graph asset path." },
            "graphName": { "type": "string", "minLength": 1 },
            "graph": { "$ref": "#/$defs/pcgGraphRef" },
            "graphRef": { "$ref": "#/$defs/pcgGraphRef" },
            "nodeIds": { "type": "array", "items": { "type": "string" } },
            "nodeClasses": { "type": "array", "items": { "type": "string" } },
            "includeConnections": { "type": "boolean", "default": false }
        },
        "$defs": {
            "pcgGraphRef": {
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

fn pcg_mutate_schema() -> rmcp::model::JsonObject {
    graph_mutate_schema(
        &[
            "addNode.byClass",
            "removeNode",
            "moveNode",
            "moveNodeBy",
            "moveNodes",
            "connectPins",
            "disconnectPins",
            "setProperty",
            "layoutGraph",
            "compile",
        ],
        false,
    )
}

fn pcg_describe_schema() -> rmcp::model::JsonObject {
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

fn widget_query_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "includeSlotProperties":{"type":"boolean","default":false}
        },
        "required":["assetPath"],
        "additionalProperties": false
    }))
}

fn widget_mutate_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "assetPath":{"type":"string","minLength":1},
            "expectedRevision":{"type":"string"},
            "dryRun":{"type":"boolean","default":false},
            "continueOnError":{"type":"boolean","default":false},
            "ops":{
                "type":"array",
                "minItems":1,
                "items":{
                    "type":"object",
                    "properties":{
                        "op":{"type":"string","enum":["addWidget","removeWidget","setProperty","reparentWidget"]},
                        "args":{
                            "type":"object",
                            "properties":{
                                "widgetClass":{"type":"string"},
                                "name":{"type":"string"},
                                "parentName":{"type":"string"},
                                "parent":{"type":"string"},
                                "newParent":{"type":"string"},
                                "property":{"type":"string"},
                                "value":{"type":"string"},
                                "slot":{"type":"object","additionalProperties":true}
                            },
                            "additionalProperties": false
                        }
                    },
                    "required":["op","args"],
                    "additionalProperties": false
                }
            }
        },
        "required":["assetPath","ops"],
        "additionalProperties": false
    }))
}

fn widget_describe_schema() -> rmcp::model::JsonObject {
    schema_from_value(serde_json::json!({
        "type":"object",
        "properties":{
            "widgetClass":{"type":"string","minLength":1},
            "assetPath":{"type":"string","minLength":1},
            "widgetName":{"type":"string","minLength":1}
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
    #[serde(rename = "pluginVersion")]
    plugin_version: Option<String>,
    #[serde(rename = "protocolVersion")]
    protocol_version: Option<u64>,
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
    let plugin_source = loomle_root()
        .join("versions")
        .join(&active_version)
        .join("plugin-cache")
        .join("LoomleBridge");
    if !plugin_source.is_dir() {
        return CallToolResult::error(vec![rmcp::model::Content::text(format!(
            "LOOMLE plugin cache missing: {}",
            plugin_source.display()
        ))]);
    }

    let _lock = match acquire_project_install_lock(&project_root) {
        Ok(lock) => lock,
        Err(message) => return CallToolResult::error(vec![rmcp::model::Content::text(message)]),
    };

    let plugin_destination = project_root.join("Plugins").join("LoomleBridge");
    let previous_version = read_plugin_version(&plugin_destination);
    if previous_version.as_deref() == Some(active_version.as_str()) && !force {
        return structured_result(serde_json::json!({
            "projectRoot": project_root.display().to_string(),
            "pluginPath": plugin_destination.display().to_string(),
            "changed": false,
            "previousVersion": previous_version,
            "installedVersion": active_version,
            "requiresEditorRestart": false,
            "message": "LOOMLE project support is already installed at the active version."
        }));
    }

    if let Err(message) = copy_tree_replace(&plugin_source, &plugin_destination) {
        return CallToolResult::error(vec![rmcp::model::Content::text(message)]);
    }
    if let Err(message) = ensure_editor_performance_setting(&project_root) {
        return CallToolResult::error(vec![rmcp::model::Content::text(message)]);
    }

    structured_result(serde_json::json!({
        "projectRoot": project_root.display().to_string(),
        "pluginPath": plugin_destination.display().to_string(),
        "changed": true,
        "previousVersion": previous_version,
        "installedVersion": active_version,
        "requiresEditorRestart": true,
        "message": "LOOMLE project support installed. Restart Unreal Editor to activate LoomleBridge."
    }))
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
    let runtime_dir = loomle_root().join("state").join("runtimes");
    let Ok(entries) = std::fs::read_dir(runtime_dir) else {
        return Vec::new();
    };

    let mut projects = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|ext| ext.to_str()) != Some("json") {
            continue;
        }
        let Ok(raw) = std::fs::read_to_string(&path) else {
            continue;
        };
        let Ok(record) = serde_json::from_str::<RuntimeRecord>(&raw) else {
            continue;
        };
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

fn runtime_record_to_project(record: RuntimeRecord) -> RuntimeProject {
    let project_root = record.project_root;
    let endpoint = record.endpoint.unwrap_or_else(|| {
        Environment::for_project_root(project_root.clone()).runtime_endpoint_path
    });
    let endpoint_exists = endpoint.exists();
    let status = if endpoint_exists { "online" } else { "offline" }.to_string();
    let reason = if endpoint_exists {
        None
    } else {
        Some("LOOMLE runtime endpoint is not available".to_string())
    };
    let project_id = record
        .project_id
        .or(record.runtime_id)
        .unwrap_or_else(|| stable_project_id(&project_root));
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
        attachable: endpoint_exists,
        plugin_installed: true,
        plugin_version: record.plugin_version,
        protocol_version: record.protocol_version,
        last_seen_at: record.last_seen_at,
        reason,
    }
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
        "pluginVersion": project.plugin_version,
        "protocolVersion": project.protocol_version,
        "lastSeenAt": project.last_seen_at,
        "reason": project.reason,
    });
    if include_diagnostics {
        value["diagnostics"] = serde_json::json!({
            "endpoint": project.endpoint.display().to_string(),
            "endpointExists": project.endpoint.exists(),
        });
    }
    value
}

fn read_global_active_version() -> Result<String, String> {
    let active_path = loomle_root().join("install").join("active.json");
    let raw = fs::read_to_string(&active_path).map_err(|error| {
        format!(
            "failed to read global active install state {}: {error}",
            active_path.display()
        )
    })?;
    let value: serde_json::Value = serde_json::from_str(&raw).map_err(|error| {
        format!(
            "failed to parse global active install state {}: {error}",
            active_path.display()
        )
    })?;
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
    let value: serde_json::Value = serde_json::from_str(&raw).map_err(|error| {
        format!(
            "failed to parse global active install state {}: {error}",
            active_path.display()
        )
    })?;
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
    Ok(Some(loomle::ActiveInstallState {
        active_version,
        launcher_path: PathBuf::from(launcher_path),
        active_client_path: PathBuf::from(active_client_path),
    }))
}

fn check_for_updates() -> serde_json::Value {
    let current = env!("CARGO_PKG_VERSION");
    match read_or_fetch_latest_version() {
        Ok(latest) => {
            let update_available = compare_semver(&latest, current)
                .map(|ordering| ordering == std::cmp::Ordering::Greater)
                .unwrap_or(latest != current);
            serde_json::json!({
                "status": "ok",
                "latestVersion": latest,
                "updateAvailable": update_available,
                "updateCommand": if update_available { Some("loomle update".to_string()) } else { None::<String> },
            })
        }
        Err(error) => serde_json::json!({
            "status": "unavailable",
            "message": error,
            "updateAvailable": false,
        }),
    }
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
    let value: serde_json::Value = serde_json::from_str(&raw).ok()?;
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
    let value: serde_json::Value = serde_json::from_str(&raw).ok()?;
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
            fs::rename(destination, &old).map_err(|error| {
                format!("failed to rename {}: {error}", destination.display())
            })?;
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

struct FileLock {
    path: PathBuf,
}

impl Drop for FileLock {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.path);
    }
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
    match OpenOptions::new().write(true).create_new(true).open(path) {
        Ok(_) => Ok(FileLock {
            path: path.to_path_buf(),
        }),
        Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {
            Err(busy_message.to_string())
        }
        Err(error) => Err(format!(
            "failed to acquire lock {}: {error}",
            path.display()
        )),
    }
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
        compare_semver, compile_blueprint_refactor_request, current_platform_client_binary_name,
        infer_attached_project_root, material_query_schema, pcg_query_schema,
        play_participant_wait_conditions_met, play_schema,
        play_wait_participant_conditions_from_args, runtime_declared_tools,
        switch_to_installed_version, translate_blueprint_graph_edit_args,
        translate_material_query_args, translate_pcg_query_args, Cli, RuntimeProject,
    };
    use rmcp::model::JsonObject;
    use std::ffi::OsString;
    use std::fs;
    use std::path::PathBuf;

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
    fn blueprint_graph_edit_translates_custom_event_command() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "addNode.customEvent",
                    "alias": "collected",
                    "name": "OnCollected",
                    "position": { "x": 320, "y": 160 },
                    "replication": "netMulticast",
                    "reliable": false,
                    "inputs": [
                        { "name": "Collector", "type": { "category": "object", "object": "/Script/Engine.Actor" } }
                    ]
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
            Some("addNode.customEvent")
        );
        assert_eq!(
            ops[0].get("clientRef").and_then(|value| value.as_str()),
            Some("collected")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("name"))
                .and_then(|value| value.as_str()),
            Some("OnCollected")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("replication"))
                .and_then(|value| value.as_str()),
            Some("netMulticast")
        );
    }

    #[test]
    fn blueprint_graph_edit_translates_timeline_command() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "addNode.timeline",
                    "alias": "fade",
                    "name": "FadeTimeline"
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
            Some("addNode.byClass")
        );
        assert_eq!(
            ops[0].get("clientRef").and_then(|value| value.as_str()),
            Some("fade")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("nodeClassPath"))
                .and_then(|value| value.as_str()),
            Some("/Script/BlueprintGraph.K2Node_Timeline")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("timelineName"))
                .and_then(|value| value.as_str()),
            Some("FadeTimeline")
        );
    }

    #[test]
    fn blueprint_graph_edit_translates_macro_command() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "addNode.byMacro",
                    "alias": "authority",
                    "macroLibraryAssetPath": "/Engine/EditorBlueprintResources/StandardMacros",
                    "macroGraphName": "Gate",
                    "position": { "x": 160, "y": 240 }
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
            Some("addNode.byMacro")
        );
        assert_eq!(
            ops[0].get("clientRef").and_then(|value| value.as_str()),
            Some("authority")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("macroLibraryAssetPath"))
                .and_then(|value| value.as_str()),
            Some("/Engine/EditorBlueprintResources/StandardMacros")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("macroGraphName"))
                .and_then(|value| value.as_str()),
            Some("Gate")
        );
    }

    #[test]
    fn blueprint_graph_edit_preserves_macro_instance_context_by_class() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "addNode",
                    "alias": "doOnce",
                    "nodeType": {
                        "id": "class:/Script/BlueprintGraph.K2Node_MacroInstance",
                        "macroLibraryAssetPath": "/Engine/EditorBlueprintResources/StandardMacros",
                        "macroGraphName": "DoOnce"
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
            op_args
                .get("nodeClassPath")
                .and_then(|value| value.as_str()),
            Some("/Script/BlueprintGraph.K2Node_MacroInstance")
        );
        assert_eq!(
            op_args
                .get("macroLibraryAssetPath")
                .and_then(|value| value.as_str()),
            Some("/Engine/EditorBlueprintResources/StandardMacros")
        );
        assert_eq!(
            op_args
                .get("macroGraphName")
                .and_then(|value| value.as_str()),
            Some("DoOnce")
        );
    }

    #[test]
    fn blueprint_graph_edit_translates_self_node_by_class() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "name": "EventGraph" }));
        args.insert(
            "commands".into(),
            serde_json::json!([
                {
                    "kind": "addNode",
                    "alias": "self",
                    "nodeType": { "id": "class:/Script/BlueprintGraph.K2Node_Self" }
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
            Some("addNode.byClass")
        );
        assert_eq!(
            ops[0]
                .get("args")
                .and_then(|value| value.get("nodeClassPath"))
                .and_then(|value| value.as_str()),
            Some("/Script/BlueprintGraph.K2Node_Self")
        );
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
    fn blueprint_graph_refactor_preserves_graph_id_address() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graph".into(), serde_json::json!({ "id": "graph-guid" }));
        args.insert(
            "transforms".into(),
            serde_json::json!([
                {
                    "kind": "replaceNode",
                    "target": { "id": "node-guid" },
                    "replacement": { "kind": "branch" }
                }
            ]),
        );

        let edit_args = compile_blueprint_refactor_request(&args).expect("edit args");
        assert_eq!(edit_args.get("graph"), None);
        assert_eq!(
            edit_args
                .get("graphRef")
                .and_then(|value| value.get("graphId"))
                .and_then(|value| value.as_str()),
            Some("graph-guid")
        );
    }

    #[test]
    fn blueprint_graph_refactor_rejects_legacy_replace_node_shape() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graphName".into(), serde_json::json!("EventGraph"));
        args.insert(
            "transforms".into(),
            serde_json::json!([
                {
                    "kind": "replaceNode",
                    "node": { "id": "node-guid" },
                    "nodeType": { "kind": "branch" }
                }
            ]),
        );

        assert!(compile_blueprint_refactor_request(&args).is_err());
    }

    #[test]
    fn blueprint_graph_refactor_replace_node_matching_pins_translates_to_rebind() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graphName".into(), serde_json::json!("EventGraph"));
        args.insert(
            "transforms".into(),
            serde_json::json!([
                {
                    "kind": "replaceNode",
                    "target": { "id": "old-node" },
                    "replacement": { "kind": "branch" },
                    "rebindPolicy": "matchingPins",
                    "removeOriginal": true
                }
            ]),
        );

        let edit_args = compile_blueprint_refactor_request(&args).expect("edit args");
        let commands = edit_args
            .get("commands")
            .and_then(|value| value.as_array())
            .expect("commands");
        assert_eq!(commands.len(), 3);
        assert_eq!(
            commands[0].get("kind").and_then(|value| value.as_str()),
            Some("addNode")
        );
        assert_eq!(
            commands[1].get("kind").and_then(|value| value.as_str()),
            Some("rebindMatchingPins")
        );
        assert_eq!(
            commands[2].get("kind").and_then(|value| value.as_str()),
            Some("removeNode")
        );
    }

    #[test]
    fn blueprint_graph_refactor_wrap_with_translates_to_move_input_links() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graphName".into(), serde_json::json!("EventGraph"));
        args.insert(
            "transforms".into(),
            serde_json::json!([
                {
                    "kind": "wrapWith",
                    "target": { "id": "target-node" },
                    "wrapper": { "kind": "branch" },
                    "alias": "guard",
                    "entryPin": "execute",
                    "targetEntryPin": "execute",
                    "wrapperExitPin": "then"
                }
            ]),
        );

        let edit_args = compile_blueprint_refactor_request(&args).expect("edit args");
        let commands = edit_args
            .get("commands")
            .and_then(|value| value.as_array())
            .expect("commands");
        assert_eq!(commands.len(), 3);
        assert_eq!(
            commands[0].get("kind").and_then(|value| value.as_str()),
            Some("addNode")
        );
        assert_eq!(
            commands[1].get("kind").and_then(|value| value.as_str()),
            Some("moveInputLinks")
        );
        assert_eq!(
            commands[2].get("kind").and_then(|value| value.as_str()),
            Some("connect")
        );
        assert_eq!(
            commands[1]
                .get("from")
                .and_then(|value| value.get("pin"))
                .and_then(|value| value.as_str()),
            Some("execute")
        );
        assert_eq!(
            commands[2]
                .get("from")
                .and_then(|value| value.get("pin"))
                .and_then(|value| value.as_str()),
            Some("then")
        );
    }

    #[test]
    fn blueprint_graph_edit_reconstruct_node_translates_to_op() {
        let mut args = JsonObject::new();
        args.insert("assetPath".into(), serde_json::json!("/Game/BP_Test"));
        args.insert("graphName".into(), serde_json::json!("EventGraph"));
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
    fn material_query_accepts_child_graph_ref_as_graph() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "asset",
                "assetPath": "/Game/MF_Test"
            }),
        );
        args.insert("includeConnections".into(), serde_json::json!(true));

        let translated = translate_material_query_args(&args).expect("translated args");
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
    fn material_query_rejects_inline_graph_ref() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "inline",
                "assetPath": "/Game/MF_Test",
                "nodeGuid": "node-guid"
            }),
        );

        assert!(translate_material_query_args(&args).is_err());
    }

    #[test]
    fn material_query_schema_has_openai_compatible_top_level() {
        let schema = material_query_schema();
        assert_eq!(
            schema.get("type").and_then(|value| value.as_str()),
            Some("object")
        );
        for keyword in ["oneOf", "anyOf", "allOf", "enum", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "material.query schema should not expose top-level {keyword}"
            );
        }
    }

    #[test]
    fn pcg_query_accepts_child_graph_ref_as_graph() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "asset",
                "assetPath": "/Game/PCG_Child"
            }),
        );
        args.insert("includeConnections".into(), serde_json::json!(true));

        let translated = translate_pcg_query_args(&args).expect("translated args");
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
    fn pcg_query_rejects_inline_graph_ref() {
        let mut args = JsonObject::new();
        args.insert(
            "graph".into(),
            serde_json::json!({
                "kind": "inline",
                "assetPath": "/Game/PCG_Parent",
                "nodeGuid": "node-guid"
            }),
        );

        assert!(translate_pcg_query_args(&args).is_err());
    }

    #[test]
    fn pcg_query_schema_has_openai_compatible_top_level() {
        let schema = pcg_query_schema();
        assert_eq!(
            schema.get("type").and_then(|value| value.as_str()),
            Some("object")
        );
        for keyword in ["oneOf", "anyOf", "allOf", "enum", "not"] {
            assert!(
                !schema.contains_key(keyword),
                "pcg.query schema should not expose top-level {keyword}"
            );
        }
    }

    #[test]
    fn diagnostic_and_log_tools_are_declared() {
        let tool_names = runtime_declared_tools()
            .into_iter()
            .map(|tool| tool.name.to_string())
            .collect::<std::collections::HashSet<_>>();

        assert!(tool_names.contains("play"));
        assert!(tool_names.contains("diagnostic.tail"));
        assert!(tool_names.contains("log.tail"));
        assert!(!tool_names.contains("log.subscribe"));
        assert!(!tool_names.contains("diag.tail"));
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
