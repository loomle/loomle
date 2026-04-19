use loomle::{
    connect_client, read_active_install_state, resolve_project_root, should_handoff_to_active_client, Environment,
    LoomleClient, StartupAction, StartupError, StartupErrorKind,
};
use rmcp::{
    model::{
        CallToolRequestParams, CallToolResult, Implementation, ListToolsResult, PaginatedRequestParams,
        ServerCapabilities, ServerInfo, Tool,
    },
    service::RequestContext,
    transport::stdio,
    ErrorData as McpError, RoleServer, ServerHandler, ServiceError, ServiceExt,
};
use std::env;
use std::ffi::OsString;
use std::path::PathBuf;
use std::process::{Command, ExitStatus, Stdio};
use std::process::ExitCode;
use std::sync::Arc;
use tokio::sync::Mutex;

#[tokio::main(flavor = "multi_thread")]
async fn main() -> ExitCode {
    let cli = match Cli::parse(env::args_os()) {
        Ok(cli) => cli,
        Err(message) => {
            eprintln!("{message}");
            print_usage();
            return ExitCode::from(2);
        }
    };

    let project_root = resolve_project_root(cli.project_root.as_deref()).ok();
    let needs_handoff = project_root
        .as_deref()
        .and_then(|p| maybe_handoff_to_active_client(p));
    if let Some(code) = needs_handoff {
        return code;
    }
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

fn maybe_handoff_to_active_client(project_root: &std::path::Path) -> Option<ExitCode> {
    let state = match read_active_install_state(project_root) {
        Ok(state) => state,
        Err(_) => return None,
    };
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
            emit_startup_error(
                &StartupError::new(
                    StartupErrorKind::ActiveInstallStateInvalid,
                    StartupAction::ReinstallLoomle,
                    15,
                    false,
                    error,
                ),
            );
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
    env_info: Option<Arc<Environment>>,
    client: Mutex<Option<LoomleClient>>,
}

impl LoomleProxyServer {
    fn new(env_info: Option<Environment>) -> Self {
        Self {
            env_info: env_info.map(Arc::new),
            client: Mutex::new(None),
        }
    }

    /// Returns a connected client, attempting to connect if not already connected.
    /// Returns None (with a human-readable reason) when the Bridge is unavailable.
    async fn get_or_connect(&self) -> Result<(), String> {
        let Some(env_info) = &self.env_info else {
            return Err(
                "No Unreal project found. Open a terminal inside a UE project directory \
                 or pass --project-root."
                    .into(),
            );
        };

        let mut guard = self.client.lock().await;

        // Already connected.
        if guard.is_some() {
            return Ok(());
        }

        // Try to connect.
        match connect_client(env_info).await {
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
}

impl ServerHandler for LoomleProxyServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo::new(ServerCapabilities::builder().enable_tools().build()).with_server_info(
            Implementation::new("loomle", env!("CARGO_PKG_VERSION"))
                .with_title("LOOMLE")
                .with_description(
                    "Project-local LOOMLE MCP proxy that forwards standard MCP over stdio to the Unreal runtime.",
                ),
        )
    }

    async fn list_tools(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<RoleServer>,
    ) -> Result<ListToolsResult, McpError> {
        match self.get_or_connect().await {
            Ok(()) => {
                let guard = self.client.lock().await;
                let client = guard.as_ref().expect("connected above");
                match client.peer().list_all_tools().await {
                    Ok(tools) => Ok(ListToolsResult::with_all_items(tools)),
                    Err(_) => {
                        drop(guard);
                        self.invalidate().await;
                        Ok(ListToolsResult::with_all_items(bridge_unavailable_tools()))
                    }
                }
            }
            Err(_) => Ok(ListToolsResult::with_all_items(bridge_unavailable_tools())),
        }
    }

    fn get_tool(&self, _name: &str) -> Option<Tool> {
        None
    }

    async fn call_tool(
        &self,
        request: CallToolRequestParams,
        _context: RequestContext<RoleServer>,
    ) -> Result<CallToolResult, McpError> {
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

fn map_service_error(error: ServiceError) -> McpError {
    McpError::internal_error(format!("runtime proxy failed: {error}"), None)
}

fn bridge_unavailable_tools() -> Vec<Tool> {
    use std::sync::Arc;
    let schema: rmcp::model::JsonObject = serde_json::from_str(
        r#"{"type":"object","properties":{},"required":[]}"#,
    )
    .expect("valid schema");
    vec![Tool::new(
        "loomle",
        "Check LOOMLE Bridge status. Returns the current availability of the Unreal Engine runtime connection.",
        Arc::new(schema),
    )]
}

fn bridge_unavailable_result(reason: &str) -> CallToolResult {
    CallToolResult::error(vec![rmcp::model::Content::text(format!(
        "LOOMLE Bridge unavailable: {reason}"
    ))])
}

#[derive(Debug, Clone)]
struct Cli {
    project_root: Option<PathBuf>,
}

impl Cli {
    fn parse<I, T>(args: I) -> Result<Self, String>
    where
        I: IntoIterator<Item = T>,
        T: Into<OsString>,
    {
        let mut iter = args.into_iter().map(Into::into);
        let _program = iter.next();

        let mut project_root = None;

        while let Some(arg) = iter.next() {
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

        Ok(Self { project_root })
    }
}

fn print_usage() {
    eprintln!("Usage:");
    eprintln!("  loomle [--project-root <ProjectRoot>]");
    eprintln!();
    eprintln!("This binary is a stdio MCP proxy. It connects to the project-local");
    eprintln!("LOOMLE runtime endpoint inside Unreal and serves standard MCP over");
    eprintln!("stdin/stdout for the host.");
    eprintln!();
    eprintln!("If --project-root is omitted, loomle searches upward from the current");
    eprintln!("directory until it finds a .uproject.");
}

#[cfg(test)]
mod tests {
    use super::Cli;
    use std::ffi::OsString;
    use std::path::PathBuf;

    #[test]
    fn parse_default_stdio_proxy() {
        let cli = Cli::parse(vec![OsString::from("loomle")]).expect("cli");
        assert!(cli.project_root.is_none());
    }

    #[test]
    fn parse_project_root() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("--project-root"),
            OsString::from("/tmp/project"),
        ])
        .expect("cli");
        assert_eq!(cli.project_root, Some(PathBuf::from("/tmp/project")));
    }
}
