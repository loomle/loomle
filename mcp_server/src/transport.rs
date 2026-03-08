use crate::{RpcConnector, RpcError, RpcHealth, RpcMeta};
use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::sync::mpsc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RpcEndpoint {
    #[cfg(windows)]
    NamedPipe { pipe_name: String },
    #[cfg(unix)]
    UnixSocket { socket_path: PathBuf },
}

impl RpcEndpoint {
    pub fn for_project_root(project_root: &Path) -> Self {
        #[cfg(windows)]
        {
            return Self::NamedPipe {
                pipe_name: windows_pipe_name_for_project_root(project_root),
            };
        }

        #[cfg(unix)]
        {
            return Self::UnixSocket {
                socket_path: project_root.join("Intermediate").join("loomle.sock"),
            };
        }
    }
}

#[cfg(any(windows, test))]
fn normalize_project_root(project_root: &Path) -> String {
    let raw = project_root.to_string_lossy().replace('\\', "/");
    let trimmed = raw.trim_end_matches('/');
    if trimmed.is_empty() {
        String::from("/")
    } else {
        trimmed.chars().flat_map(char::to_lowercase).collect()
    }
}

#[cfg(any(windows, test))]
fn stable_fnv1a_64(input: &str) -> u64 {
    const OFFSET_BASIS: u64 = 0xcbf29ce484222325;
    const PRIME: u64 = 0x100000001b3;

    let mut hash = OFFSET_BASIS;
    for byte in input.as_bytes() {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(PRIME);
    }
    hash
}

#[cfg(any(windows, test))]
fn windows_pipe_name_for_project_root(project_root: &Path) -> String {
    let normalized = normalize_project_root(project_root);
    format!("loomle-{:016x}", stable_fnv1a_64(&normalized))
}

pub struct NdjsonRpcConnector {
    endpoint: RpcEndpoint,
    next_id: AtomicU64,
}

impl NdjsonRpcConnector {
    pub fn new(endpoint: RpcEndpoint) -> Self {
        Self {
            endpoint,
            next_id: AtomicU64::new(1),
        }
    }

    fn call(&self, method: &str, params: Value) -> Result<Value, RpcError> {
        self.call_with_timeout(method, params, None)
    }

    fn call_with_timeout(
        &self,
        method: &str,
        params: Value,
        timeout: Option<Duration>,
    ) -> Result<Value, RpcError> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed).to_string();
        let request = json!({
            "jsonrpc": "2.0",
            "id": id,
            "method": method,
            "params": params,
        });

        if let Some(timeout) = timeout {
            return call_with_deadline(self.endpoint.clone(), request, id, timeout);
        }

        let response = send_and_wait(&self.endpoint, &request)?;
        parse_response(response, &id)
    }
}

impl RpcConnector for NdjsonRpcConnector {
    fn health(&self) -> Result<RpcHealth, RpcError> {
        let value = self.call_with_timeout(
            "rpc.health",
            json!({}),
            Some(Duration::from_millis(200)),
        )?;
        serde_json::from_value::<RpcHealth>(value).map_err(|e| RpcError {
            code: 1011,
            message: String::from("INTERNAL_ERROR"),
            retryable: false,
            detail: format!("invalid rpc.health payload: {e}"),
        })
    }

    fn invoke(&self, tool: &str, args: Value, meta: RpcMeta) -> Result<Value, RpcError> {
        let params = json!({
            "tool": tool,
            "args": args,
            "meta": {
                "requestId": meta.request_id,
                "traceId": meta.trace_id,
                "rpcVersion": "1.0",
                "deadlineMs": meta.deadline_ms,
            }
        });

        let value = self.call("rpc.invoke", params)?;
        if value.get("ok").and_then(Value::as_bool) == Some(true) {
            Ok(value.get("payload").cloned().unwrap_or_else(|| json!({})))
        } else {
            Err(RpcError {
                code: 1011,
                message: String::from("INTERNAL_ERROR"),
                retryable: false,
                detail: String::from("rpc.invoke returned ok=false without rpc error envelope"),
            })
        }
    }
}

fn parse_response(response: Value, expected_id: &str) -> Result<Value, RpcError> {
    let id_matches = response
        .get("id")
        .and_then(Value::as_str)
        .map(|id| id == expected_id)
        .unwrap_or(false);

    if !id_matches {
        return Err(RpcError {
            code: 1011,
            message: String::from("INTERNAL_ERROR"),
            retryable: false,
            detail: format!("response id mismatch, expected={expected_id}"),
        });
    }

    if let Some(err) = response.get("error") {
        return Err(RpcError {
            code: err.get("code").and_then(Value::as_i64).unwrap_or(1011) as u16,
            message: err
                .get("message")
                .and_then(Value::as_str)
                .unwrap_or("INTERNAL_ERROR")
                .to_string(),
            retryable: err
                .get("data")
                .and_then(|v| v.get("retryable"))
                .and_then(Value::as_bool)
                .unwrap_or(false),
            detail: err
                .get("data")
                .and_then(|v| v.get("detail"))
                .and_then(Value::as_str)
                .unwrap_or("")
                .to_string(),
        });
    }

    response.get("result").cloned().ok_or_else(|| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: false,
        detail: String::from("missing result in RPC response"),
    })
}

fn call_with_deadline(
    endpoint: RpcEndpoint,
    request: Value,
    expected_id: String,
    timeout: Duration,
) -> Result<Value, RpcError> {
    let (tx, rx) = mpsc::sync_channel(1);
    std::thread::spawn(move || {
        let result = send_and_wait(&endpoint, &request).and_then(|response| parse_response(response, &expected_id));
        let _ = tx.send(result);
    });

    rx.recv_timeout(timeout).unwrap_or_else(|_| {
        Err(RpcError {
            code: 1010,
            message: String::from("EXECUTION_TIMEOUT"),
            retryable: true,
            detail: format!("rpc.health timeout after {}ms", timeout.as_millis()),
        })
    })
}

#[cfg(unix)]
fn send_and_wait(endpoint: &RpcEndpoint, request: &Value) -> Result<Value, RpcError> {
    use std::os::unix::net::UnixStream;

    let path = match endpoint {
        RpcEndpoint::UnixSocket { socket_path } => socket_path,
    };

    let mut stream = UnixStream::connect(path).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: true,
        detail: format!("failed to connect unix socket {}: {e}", path.display()),
    })?;

    let mut line = serde_json::to_string(request).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: false,
        detail: format!("failed to encode request: {e}"),
    })?;
    line.push('\n');

    stream.write_all(line.as_bytes()).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: true,
        detail: format!("failed to write request: {e}"),
    })?;

    let mut reader = BufReader::new(stream);
    let mut response_line = String::new();
    let bytes = reader.read_line(&mut response_line).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: true,
        detail: format!("failed to read response: {e}"),
    })?;

    if bytes == 0 {
        return Err(RpcError {
            code: 1011,
            message: String::from("INTERNAL_ERROR"),
            retryable: true,
            detail: String::from("rpc listener closed connection"),
        });
    }

    serde_json::from_str::<Value>(response_line.trim()).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: false,
        detail: format!("invalid rpc response json: {e}"),
    })
}

#[cfg(windows)]
fn send_and_wait(endpoint: &RpcEndpoint, request: &Value) -> Result<Value, RpcError> {
    use std::fs::OpenOptions;

    let pipe_path = match endpoint {
        RpcEndpoint::NamedPipe { pipe_name } => format!("\\\\.\\pipe\\{pipe_name}"),
    };

    let mut pipe = OpenOptions::new()
        .read(true)
        .write(true)
        .open(&pipe_path)
        .map_err(|e| RpcError {
            code: 1011,
            message: String::from("INTERNAL_ERROR"),
            retryable: true,
            detail: format!("failed to open named pipe {pipe_path}: {e}"),
        })?;

    let mut line = serde_json::to_string(request).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: false,
        detail: format!("failed to encode request: {e}"),
    })?;
    line.push('\n');

    pipe.write_all(line.as_bytes()).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: true,
        detail: format!("failed to write request: {e}"),
    })?;

    let mut reader = BufReader::new(pipe);
    let mut response_line = String::new();
    let bytes = reader.read_line(&mut response_line).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: true,
        detail: format!("failed to read response: {e}"),
    })?;

    if bytes == 0 {
        return Err(RpcError {
            code: 1011,
            message: String::from("INTERNAL_ERROR"),
            retryable: true,
            detail: String::from("rpc listener closed connection"),
        });
    }

    serde_json::from_str::<Value>(response_line.trim()).map_err(|e| RpcError {
        code: 1011,
        message: String::from("INTERNAL_ERROR"),
        retryable: false,
        detail: format!("invalid rpc response json: {e}"),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn windows_pipe_name_is_stable_across_path_separators_and_case() {
        let a = windows_pipe_name_for_project_root(Path::new(r"D:\LoomleDevHost\"));
        let b = windows_pipe_name_for_project_root(Path::new("d:/loomledevhost"));

        assert_eq!(a, b);
        assert!(a.starts_with("loomle-"));
    }

    #[test]
    fn windows_pipe_name_changes_per_project_root() {
        let a = windows_pipe_name_for_project_root(Path::new("D:/LoomleDevHost"));
        let b = windows_pipe_name_for_project_root(Path::new("D:/OtherProject"));

        assert_ne!(a, b);
    }
}
