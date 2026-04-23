use loomle::{
    connect_client, resolve_project_root, should_handoff_to_active_client, validate_project_root,
    Environment, LoomleClient, StartupAction, StartupError, StartupErrorKind,
};
use rmcp::{
    model::{
        CallToolRequestParams, CallToolResult, Implementation, ListToolsResult,
        PaginatedRequestParams, ServerCapabilities, ServerInfo, Tool,
    },
    service::RequestContext,
    transport::stdio,
    ErrorData as McpError, RoleServer, ServerHandler, ServiceError, ServiceExt,
};
use std::env;
use std::ffi::OsString;
use std::fs;
use std::fs::OpenOptions;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::process::{Command, ExitStatus, Stdio};
use std::sync::Arc;
use tokio::sync::Mutex;

#[tokio::main(flavor = "multi_thread")]
async fn main() -> ExitCode {
    if let Some(code) = maybe_handoff_to_global_active_client() {
        return code;
    }

    let command = match Cli::parse(env::args_os()) {
        Ok(cli) => cli,
        Err(message) => {
            eprintln!("{message}");
            print_usage();
            return ExitCode::from(2);
        }
    };

    match command {
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
        "  Claude: claude mcp add loomle --scope user {}/bin/loomle mcp",
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
    client: Mutex<Option<LoomleClient>>,
}

impl LoomleProxyServer {
    fn new(env_info: Option<Environment>) -> Self {
        Self {
            session_cwd: env::current_dir().ok(),
            env_info: Mutex::new(env_info.map(Arc::new)),
            client: Mutex::new(None),
        }
    }

    /// Returns a connected client, attempting to connect if not already connected.
    /// Returns None (with a human-readable reason) when the Bridge is unavailable.
    async fn get_or_connect(&self) -> Result<(), String> {
        self.try_auto_attach().await;
        let env_info = self.env_info.lock().await.clone();
        let Some(env_info) = env_info else {
            return Err(
                "No Unreal project is attached. Use project.list to find online projects, \
                 then project.attach to select one for this MCP session."
                    .into(),
            );
        };

        let mut guard = self.client.lock().await;

        // Already connected.
        if guard.is_some() {
            return Ok(());
        }

        // Try to connect.
        match connect_client(&env_info).await {
            Ok(client) => {
                *guard = Some(client);
                Ok(())
            }
            Err(err) => Err(format!(
                "Unreal Engine is not running or LoomleBridge is not loaded. \
                 Start Unreal Editor and wait for the Bridge to initialise. \
                 ({})",
                err.message
            )),
        }
    }

    /// Drop a stale client so the next call retries the connection.
    async fn invalidate(&self) {
        *self.client.lock().await = None;
    }

    async fn attach_project(&self, project: RuntimeProject) {
        let env_info = Environment::for_project_root(project.project_root);
        *self.env_info.lock().await = Some(Arc::new(env_info));
        self.invalidate().await;
    }

    async fn attached_project_root(&self) -> Option<PathBuf> {
        self.env_info
            .lock()
            .await
            .as_ref()
            .map(|env_info| env_info.project_root.clone())
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
        if is_pre_attach_tool(request.name.as_ref()) {
            return Ok(self.call_pre_attach_tool(request).await);
        }

        match self.get_or_connect().await {
            Ok(()) => {
                let guard = self.client.lock().await;
                let client = guard.as_ref().expect("connected above");
                match client.peer().call_tool(request).await {
                    Ok(result) => Ok(result),
                    Err(err) => {
                        drop(guard);
                        self.invalidate().await;
                        Err(map_service_error(err))
                    }
                }
            }
            Err(reason) => Ok(bridge_unavailable_result(&reason)),
        }
    }
}

impl LoomleProxyServer {
    async fn call_pre_attach_tool(&self, request: CallToolRequestParams) -> CallToolResult {
        let args = request.arguments.unwrap_or_default();
        match request.name.as_ref() {
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
        let attached_project_root = self.attached_project_root().await;
        let payload = serde_json::json!({
            "loomleVersion": env!("CARGO_PKG_VERSION"),
            "attached": attached_project_root.is_some(),
            "attachedProject": attached_project_root.map(|path| path.display().to_string()),
            "onlineProjectCount": projects.iter().filter(|project| project.status == "online").count(),
            "projectCount": projects.len(),
        });
        structured_result(payload)
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
}

fn map_service_error(error: ServiceError) -> McpError {
    McpError::internal_error(format!("runtime proxy failed: {error}"), None)
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

    const RUNTIME_TOOLS: [(&str, &str); 22] = [
        ("loomle", "Bridge health and runtime status."),
        ("context", "Read active editor context and selection."),
        ("execute", "Execute Unreal-side Python inside the editor process."),
        ("jobs", "Inspect or retrieve long-running job state, results, and logs."),
        (
            "profiling",
            "Bridge official Unreal profiling data families such as stat unit, stat groups, ticks, memory reports, and capture workflows.",
        ),
        ("editor.open", "Open or focus the editor for a specific Unreal asset path."),
        (
            "editor.focus",
            "Focus a semantic panel inside an asset editor, such as graph, viewport, details, palette, or find.",
        ),
        (
            "editor.screenshot",
            "Capture a PNG of the active editor window and return the written file path.",
        ),
        ("graph", "Read graph capability descriptor and runtime status."),
        ("blueprint.list", "List Blueprint graphs in an asset."),
        ("blueprint.query", "Read node and pin data from a Blueprint graph."),
        ("blueprint.mutate", "Apply a batch of write operations to a Blueprint graph."),
        ("blueprint.verify", "Run read-only structural validation for a Blueprint graph."),
        ("blueprint.describe", "Describe a Blueprint class or a specific Blueprint graph node."),
        ("material.list", "List material expressions in a material asset."),
        ("material.query", "Read expression nodes and pin data from a material."),
        ("material.mutate", "Apply a batch of write operations to a material asset."),
        ("material.verify", "Compile a material and return diagnostics."),
        ("material.describe", "Describe a material expression class or instance."),
        ("pcg.list", "List nodes in a PCG graph asset."),
        ("pcg.query", "Read node and pin data from a PCG graph."),
        ("pcg.mutate", "Apply a batch of write operations to a PCG graph."),
    ];
    const RUNTIME_TOOLS_TAIL: [(&str, &str); 6] = [
        ("pcg.verify", "Run read-only validation for a PCG graph."),
        ("pcg.describe", "Describe a PCG settings class or instance."),
        ("diag.tail", "Read persisted diagnostics incrementally by sequence cursor."),
        ("widget.query", "Read the UMG WidgetTree of a WidgetBlueprint asset."),
        (
            "widget.mutate",
            "Apply structural write operations to the UMG WidgetTree of a WidgetBlueprint asset.",
        ),
        (
            "widget.verify",
            "Compile a WidgetBlueprint and return diagnostics.",
        ),
    ];

    let mut tools: Vec<Tool> = RUNTIME_TOOLS
        .into_iter()
        .map(|(name, description)| Tool::new(name, description, Arc::new(loose_object_schema())))
        .collect();
    tools.extend(
        RUNTIME_TOOLS_TAIL.into_iter().map(|(name, description)| {
            Tool::new(name, description, Arc::new(loose_object_schema()))
        }),
    );
    tools.push(Tool::new(
        "widget.describe",
        "Enumerate the editable properties of a UMG widget class, with optional current values from a live instance.",
        Arc::new(loose_object_schema()),
    ));
    tools
}

fn bridge_unavailable_result(reason: &str) -> CallToolResult {
    CallToolResult::error(vec![rmcp::model::Content::text(format!(
        "LOOMLE Bridge unavailable: {reason}"
    ))])
}

fn is_pre_attach_tool(name: &str) -> bool {
    matches!(
        name,
        "loomle.status" | "project.list" | "project.attach" | "project.install"
    )
}

fn empty_schema() -> rmcp::model::JsonObject {
    serde_json::from_str(r#"{"type":"object","properties":{},"additionalProperties":false}"#)
        .expect("valid schema")
}

fn loose_object_schema() -> rmcp::model::JsonObject {
    serde_json::from_str(r#"{"type":"object","properties":{},"additionalProperties":true}"#)
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
            return Err(String::from("missing command"));
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
            return Err(String::from("help requested"));
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

fn print_usage() {
    eprintln!("Usage:");
    eprintln!("  loomle mcp");
    eprintln!("  loomle doctor");
    eprintln!("  loomle update");
    eprintln!();
    eprintln!("This binary is the global LOOMLE entrypoint.");
    eprintln!();
    eprintln!("Agent hosts should run `loomle mcp` as a stdio MCP server.");
}

#[cfg(test)]
mod tests {
    use super::{infer_attached_project_root, Cli, RuntimeProject};
    use std::ffi::OsString;
    use std::path::PathBuf;

    #[test]
    fn parse_default_requires_command() {
        assert!(Cli::parse(vec![OsString::from("loomle")]).is_err());
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
            plugin_version: Some("0.5.2".to_string()),
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
}
