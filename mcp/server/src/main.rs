use loomle_mcp_server::sdk::LoomleSdkServer;
use loomle_mcp_server::transport::{NdjsonRpcConnector, RpcEndpoint};
use loomle_mcp_server::McpService;
use rmcp::ServiceExt;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

fn log(level: &str, msg: &str) {
    eprintln!("[loomle-mcp][{level}] {msg}");
}

#[tokio::main(flavor = "multi_thread")]
async fn main() {
    std::process::exit(run().await);
}

async fn run() -> i32 {
    let project_root = match parse_project_root_arg() {
        Ok(path) => path,
        Err(msg) => {
            log("ERROR", &format!("project-root configuration error: {msg}"));
            return 2;
        }
    };

    log(
        "INFO",
        &format!(
            "starting loomle-mcp-server v{} project-root={}",
            env!("CARGO_PKG_VERSION"),
            project_root.display()
        ),
    );

    let connector = NdjsonRpcConnector::new(RpcEndpoint::for_project_root(&project_root));
    let service = McpService::new(connector);
    let server = LoomleSdkServer::new(std::sync::Arc::new(service));

    let running = match server
        .serve((tokio::io::stdin(), tokio::io::stdout()))
        .await
    {
        Ok(running) => running,
        Err(error) => {
            log("ERROR", &format!("rmcp server bootstrap failed: {error}"));
            return 1;
        }
    };

    match running.waiting().await {
        Ok(reason) => {
            log("INFO", &format!("shutting down: {reason:?}"));
            0
        }
        Err(error) => {
            log("ERROR", &format!("rmcp server task failed: {error}"));
            1
        }
    }
}

fn parse_project_root_arg() -> Result<PathBuf, String> {
    let mut args = env::args().skip(1);
    let key = args.next();
    let value = args.next();
    if args.next().is_some() {
        return Err(String::from(
            "unexpected extra arguments; usage: loomle_mcp_server --project-root <ProjectRoot>",
        ));
    }
    if key.as_deref() != Some("--project-root") {
        return Err(String::from(
            "missing required --project-root argument; usage: loomle_mcp_server --project-root <ProjectRoot>",
        ));
    }
    let raw = value.ok_or_else(|| String::from("missing value for --project-root"))?;
    let path = PathBuf::from(raw);
    if !path.is_dir() {
        return Err(format!("path is not a directory: {}", path.display()));
    }
    if !has_uproject_file(&path) {
        return Err(format!("no .uproject found under: {}", path.display()));
    }
    Ok(path)
}

fn has_uproject_file(dir: &Path) -> bool {
    match fs::read_dir(dir) {
        Ok(entries) => entries.flatten().any(|entry| {
            entry
                .path()
                .extension()
                .and_then(|ext| ext.to_str())
                .is_some_and(|ext| ext.eq_ignore_ascii_case("uproject"))
        }),
        Err(_) => false,
    }
}
