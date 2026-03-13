use loomle_mcp_server::mcp::handle_request;
use loomle_mcp_server::transport::{NdjsonRpcConnector, RpcEndpoint};
use loomle_mcp_server::McpService;
use serde_json::{json, Value};
use std::env;
use std::fs;
use std::io::{self, BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::time::Instant;

/// Maximum allowed line length (16 MiB). Requests exceeding this are rejected
/// with a parse error to prevent unbounded memory allocation.
const MAX_LINE_BYTES: usize = 16 * 1024 * 1024;

fn log(level: &str, msg: &str) {
    eprintln!("[loomle-mcp][{level}] {msg}");
}

fn main() {
    let project_root = match parse_project_root_arg() {
        Ok(path) => path,
        Err(msg) => {
            log("ERROR", &format!("project-root configuration error: {msg}"));
            std::process::exit(2);
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

    let stdin = io::stdin();
    let mut reader = BufReader::new(stdin.lock());
    let mut stdout = io::stdout();
    let mut buf = Vec::new();
    let mut request_count: u64 = 0;

    loop {
        buf.clear();
        match reader.read_until(b'\n', &mut buf) {
            Ok(0) => break,
            Ok(_) => {}
            Err(_) => break,
        }

        if buf.len() > MAX_LINE_BYTES {
            log(
                "WARN",
                &format!("rejecting oversized request ({} bytes)", buf.len()),
            );
            let response = json!({
                "jsonrpc": "2.0",
                "id": null,
                "error": {
                    "code": -32700,
                    "message": "Parse error",
                    "data": {
                        "detail": format!("request exceeds maximum line length of {} bytes", MAX_LINE_BYTES)
                    }
                }
            });
            if write_response(&mut stdout, &response).is_err() {
                break;
            }
            continue;
        }

        let trimmed = match std::str::from_utf8(&buf) {
            Ok(s) => s.trim(),
            Err(_) => continue,
        };
        if trimmed.is_empty() {
            continue;
        }

        let request = match serde_json::from_str::<Value>(trimmed) {
            Ok(v) => v,
            Err(e) => {
                log("WARN", &format!("parse error: {e}"));
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

        request_count += 1;
        let method = request
            .get("method")
            .and_then(Value::as_str)
            .unwrap_or("?")
            .to_owned();
        let id_str = request
            .get("id")
            .map(|v| v.to_string())
            .unwrap_or_else(|| String::from("null"));
        log(
            "DEBUG",
            &format!("req#{request_count} method={method} id={id_str}"),
        );

        let start = Instant::now();
        if let Some(response) = handle_request(&service, request) {
            let elapsed_ms = start.elapsed().as_millis();
            let has_error = response.get("error").is_some();
            if has_error {
                log(
                    "WARN",
                    &format!("req#{request_count} method={method} error elapsed={elapsed_ms}ms"),
                );
            } else {
                log(
                    "DEBUG",
                    &format!("req#{request_count} method={method} ok elapsed={elapsed_ms}ms"),
                );
            }
            if write_response(&mut stdout, &response).is_err() {
                break;
            }
        }
    }

    log(
        "INFO",
        &format!("shutting down after {request_count} requests"),
    );
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
