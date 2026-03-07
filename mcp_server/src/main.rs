use loomle_mcp_server::mcp::handle_request;
use loomle_mcp_server::transport::{NdjsonRpcConnector, RpcEndpoint};
use loomle_mcp_server::McpService;
use serde_json::{json, Value};
use std::env;
use std::fs;
use std::io::{self, BufRead, Write};
use std::path::{Path, PathBuf};

fn main() {
    let project_root = match parse_project_root_arg() {
        Ok(path) => path,
        Err(msg) => {
            eprintln!("project-root configuration error: {msg}");
            std::process::exit(2);
        }
    };
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
    let p = PathBuf::from(raw);
    if !p.is_dir() {
        return Err(format!("path is not a directory: {}", p.display()));
    }
    if !has_uproject_file(&p) {
        return Err(format!("no .uproject found under: {}", p.display()));
    }
    Ok(p)
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
