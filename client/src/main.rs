use loomle::{
    connect_client, read_active_install_state, resolve_project_root, should_handoff_to_active_client, Environment,
    LoomleClient,
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

    let project_root = match resolve_project_root(cli.project_root.as_deref()) {
        Ok(path) => path,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(2);
        }
    };
    if let Some(code) = maybe_handoff_to_active_client(&project_root) {
        return code;
    }
    let env_info = Environment::for_project_root(project_root);
    let runtime_client = match connect_client(&env_info).await {
        Ok(client) => Arc::new(client),
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(1);
        }
    };

    let server = LoomleProxyServer::new(runtime_client);
    let running = match server.serve(stdio()).await {
        Ok(running) => running,
        Err(error) => {
            eprintln!("[loomle][ERROR] failed to start stdio MCP service: {error}");
            return ExitCode::from(1);
        }
    };

    match running.waiting().await {
        Ok(_) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("[loomle][ERROR] stdio MCP service failed: {error}");
            ExitCode::from(1)
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
            eprintln!("[loomle][ERROR] failed to resolve current executable: {error}");
            return Some(ExitCode::from(1));
        }
    };
    let handoff_decision = match should_handoff_to_active_client(&current_exe, &state) {
        Ok(value) => value,
        Err(error) => {
            eprintln!("[loomle][ERROR] {error}");
            return Some(ExitCode::from(1));
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
            eprintln!(
                "[loomle][ERROR] failed to start active client {} for version {}: {}",
                state.active_client_path.display(),
                state.active_version,
                error
            );
            return Some(ExitCode::from(1));
        }
    };
    let status = match child.wait() {
        Ok(status) => status,
        Err(error) => {
            eprintln!(
                "[loomle][ERROR] failed while waiting for active client {}: {}",
                state.active_client_path.display(),
                error
            );
            return Some(ExitCode::from(1));
        }
    };
    Some(exit_code_from_status(status))
}

fn exit_code_from_status(status: ExitStatus) -> ExitCode {
    match status.code() {
        Some(code) => ExitCode::from(code as u8),
        None => ExitCode::from(1),
    }
}

struct LoomleProxyServer {
    runtime_client: Arc<LoomleClient>,
}

impl LoomleProxyServer {
    fn new(runtime_client: Arc<LoomleClient>) -> Self {
        Self { runtime_client }
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
        let tools = self
            .runtime_client
            .peer()
            .list_all_tools()
            .await
            .map_err(map_service_error)?;
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
        self.runtime_client
            .peer()
            .call_tool(request)
            .await
            .map_err(map_service_error)
    }
}

fn map_service_error(error: ServiceError) -> McpError {
    McpError::internal_error(format!("runtime proxy failed: {error}"), None)
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
