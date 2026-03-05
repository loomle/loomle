use loomle_mcp_server::mcp::handle_request;
use loomle_mcp_server::transport::{NdjsonRpcConnector, RpcEndpoint};
use loomle_mcp_server::McpService;
use serde_json::{json, Value};
use std::env;
use std::io::{self, BufRead, Write};
use std::path::{Path, PathBuf};

fn main() {
    let project_root = detect_project_root();
    let connector = NdjsonRpcConnector::new(RpcEndpoint::for_project_root(&project_root));
    let service = McpService::new(connector);

    let stdin = io::stdin();
    let mut stdout = io::stdout();

    for line_result in stdin.lock().lines() {
        let line = match line_result {
            Ok(v) => v,
            Err(_) => break,
        };

        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }

        let request = match serde_json::from_str::<Value>(trimmed) {
            Ok(v) => v,
            Err(e) => {
                let response = json!({
                    "jsonrpc": "2.0",
                    "id": null,
                    "error": {
                        "code": -32700,
                        "message": "Parse error",
                        "data": {
                            "detail": e.to_string()
                        }
                    }
                });
                if write_response(&mut stdout, &response).is_err() {
                    break;
                }
                continue;
            }
        };

        if let Some(response) = handle_request(&service, request) {
            if write_response(&mut stdout, &response).is_err() {
                break;
            }
        }
    }
}

fn write_response(stdout: &mut impl Write, response: &Value) -> io::Result<()> {
    let mut line = serde_json::to_string(response)
        .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e.to_string()))?;
    line.push('\n');
    stdout.write_all(line.as_bytes())?;
    stdout.flush()
}

fn detect_project_root() -> PathBuf {
    if let Ok(v) = env::var("LOOMLE_PROJECT_ROOT") {
        let p = PathBuf::from(v);
        if p.join("Loomle.uproject").exists() {
            return p;
        }
    }

    let cwd = env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
    if let Some(found) = find_upwards(&cwd, "Loomle.uproject") {
        return found;
    }

    cwd
}

fn find_upwards(start: &Path, marker_file: &str) -> Option<PathBuf> {
    let mut current = Some(start.to_path_buf());
    while let Some(path) = current {
        if path.join(marker_file).exists() {
            return Some(path);
        }
        current = path.parent().map(Path::to_path_buf);
    }
    None
}
