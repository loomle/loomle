use loomle_mcp_server::mcp::handle_request;
use loomle_mcp_server::transport::{NdjsonRpcConnector, RpcEndpoint};
use loomle_mcp_server::McpService;
use serde_json::{json, Value};
use std::env;
use std::fs;
use std::io::{self, BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::sync::{mpsc, Arc};
use std::thread;
use std::time::Instant;

/// Maximum allowed line length (16 MiB). Requests exceeding this are rejected
/// with a parse error to prevent unbounded memory allocation.
const MAX_LINE_BYTES: usize = 16 * 1024 * 1024;

fn log(level: &str, msg: &str) {
    eprintln!("[loomle-mcp][{level}] {msg}");
}

struct RequestTask {
    request_count: u64,
    method: String,
    request: Value,
}

struct CompletedResponse {
    request_count: u64,
    method: String,
    response: Value,
    elapsed_ms: u128,
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
    let service = Arc::new(McpService::new(connector));
    let worker_count = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(4)
        .clamp(2, 16);
    let max_pending_tasks = worker_count * 32;

    log(
        "INFO",
        &format!(
            "concurrent intake enabled workers={worker_count} max-pending-tasks={max_pending_tasks}"
        ),
    );

    let stdin = io::stdin();
    let mut reader = BufReader::new(stdin.lock());
    let mut buf = Vec::new();
    let mut request_count: u64 = 0;
    let (task_tx, task_rx) = mpsc::sync_channel::<RequestTask>(max_pending_tasks);
    let (response_tx, response_rx) = mpsc::channel::<CompletedResponse>();
    let response_tx_main = response_tx.clone();
    let shared_task_rx = Arc::new(std::sync::Mutex::new(task_rx));
    let writer = thread::spawn(move || writer_loop(response_rx));
    let mut workers = Vec::with_capacity(worker_count);

    for worker_idx in 0..worker_count {
        let task_rx = Arc::clone(&shared_task_rx);
        let response_tx = response_tx.clone();
        let service = Arc::clone(&service);
        workers.push(thread::spawn(move || {
            worker_loop(worker_idx, service, task_rx, response_tx);
        }));
    }
    drop(response_tx);

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
            request_count += 1;
            let response = parse_error_response(format!(
                "request exceeds maximum line length of {} bytes",
                MAX_LINE_BYTES
            ));
            if send_completed_response(
                &response_tx_main,
                request_count,
                String::from("parse_error"),
                response,
            )
            .is_err()
            {
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
                request_count += 1;
                let response = parse_error_response(e.to_string());
                if send_completed_response(
                    &response_tx_main,
                    request_count,
                    String::from("parse_error"),
                    response,
                )
                .is_err()
                {
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
        if task_tx
            .send(RequestTask {
                request_count,
                method,
                request,
            })
            .is_err()
        {
            break;
        }
    }

    drop(task_tx);
    for worker in workers {
        let _ = worker.join();
    }
    drop(response_tx_main);
    let _ = writer.join();

    log(
        "INFO",
        &format!("shutting down after {request_count} requests"),
    );
}

fn worker_loop(
    worker_idx: usize,
    service: Arc<McpService<NdjsonRpcConnector>>,
    task_rx: Arc<std::sync::Mutex<mpsc::Receiver<RequestTask>>>,
    response_tx: mpsc::Sender<CompletedResponse>,
) {
    loop {
        let task = {
            let rx = match task_rx.lock() {
                Ok(rx) => rx,
                Err(_) => return,
            };
            match rx.recv() {
                Ok(task) => task,
                Err(_) => return,
            }
        };

        let start = Instant::now();
        if let Some(response) = handle_request(&service, task.request) {
            let elapsed_ms = start.elapsed().as_millis();
            if response_tx
                .send(CompletedResponse {
                    request_count: task.request_count,
                    method: task.method,
                    response,
                    elapsed_ms,
                })
                .is_err()
            {
                return;
            }
        } else {
            log(
                "DEBUG",
                &format!(
                    "worker#{worker_idx} req#{} method={} notification completed",
                    task.request_count, task.method
                ),
            );
        }
    }
}

fn writer_loop(response_rx: mpsc::Receiver<CompletedResponse>) {
    let mut stdout = io::stdout();
    for completed in response_rx {
        let has_error = completed.response.get("error").is_some();
        if has_error {
            log(
                "WARN",
                &format!(
                    "req#{} method={} error elapsed={}ms",
                    completed.request_count, completed.method, completed.elapsed_ms
                ),
            );
        } else {
            log(
                "DEBUG",
                &format!(
                    "req#{} method={} ok elapsed={}ms",
                    completed.request_count, completed.method, completed.elapsed_ms
                ),
            );
        }

        if let Err(err) = write_response(&mut stdout, &completed.response) {
            log("ERROR", &format!("stdout write failed: {err}"));
            break;
        }
    }
}

fn parse_error_response(detail: String) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": null,
        "error": {
            "code": -32700,
            "message": "Parse error",
            "data": {
                "detail": detail
            }
        }
    })
}

fn send_completed_response(
    response_tx: &mpsc::Sender<CompletedResponse>,
    request_count: u64,
    method: String,
    response: Value,
) -> Result<(), ()> {
    response_tx
        .send(CompletedResponse {
            request_count,
            method,
            response,
            elapsed_ms: 0,
        })
        .map_err(|_| ())
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
