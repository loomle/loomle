use loomle::{connect_client, resolve_project_root, Environment, LoomleClient};
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
