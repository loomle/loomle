use crate::{RpcConnector, RpcError, RpcHealth, RpcMeta};
use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

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
            let _ = project_root;
            return Self::NamedPipe {
                pipe_name: String::from("loomle"),
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
        let id = self.next_id.fetch_add(1, Ordering::Relaxed).to_string();
        let request = json!({
            "jsonrpc": "2.0",
            "id": id,
            "method": method,
            "params": params,
        });

        let response = send_and_wait(&self.endpoint, &request)?;
        parse_response(response, &id)
    }
}

impl RpcConnector for NdjsonRpcConnector {
    fn health(&self) -> Result<RpcHealth, RpcError> {
        let value = self.call("rpc.health", json!({}))?;
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
