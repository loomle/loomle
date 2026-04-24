use rmcp::model::{CallToolResult, JsonObject};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::env;
use std::path::{Path, PathBuf};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
#[cfg(target_os = "windows")]
use tokio::net::windows::named_pipe::ClientOptions;
#[cfg(unix)]
use tokio::net::UnixStream;
#[cfg(target_os = "windows")]
use tokio::time::{sleep, Duration};
#[cfg(target_os = "windows")]
use windows_sys::Win32::Foundation::{ERROR_ACCESS_DENIED, ERROR_FILE_NOT_FOUND, ERROR_PIPE_BUSY};

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum StartupErrorKind {
    InvalidProjectRoot,
    EndpointMissing,
    AccessDenied,
    ConnectFailed,
    ActiveInstallStateInvalid,
    HandoffFailed,
    StdioServerStartFailed,
    StdioServerRuntimeFailed,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum StartupAction {
    FixProjectRoot,
    StartUnrealEditor,
    RetryWithElevation,
    VerifyRuntimeHealth,
    ReinstallLoomle,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct StartupError {
    pub kind: StartupErrorKind,
    pub action: StartupAction,
    #[serde(rename = "exitCode")]
    pub exit_code: u8,
    pub retryable: bool,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub endpoint: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
}

impl StartupError {
    pub fn new(
        kind: StartupErrorKind,
        action: StartupAction,
        exit_code: u8,
        retryable: bool,
        message: impl Into<String>,
    ) -> Self {
        Self {
            kind,
            action,
            exit_code,
            retryable,
            message: message.into(),
            endpoint: None,
            detail: None,
        }
    }

    pub fn with_endpoint(mut self, endpoint: impl Into<String>) -> Self {
        self.endpoint = Some(endpoint.into());
        self
    }

    pub fn with_detail(mut self, detail: impl Into<String>) -> Self {
        self.detail = Some(detail.into());
        self
    }
}

#[derive(Debug, Clone)]
pub struct Environment {
    pub project_root: PathBuf,
    pub runtime_endpoint_path: PathBuf,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ActiveInstallState {
    #[serde(rename = "activeVersion")]
    pub active_version: String,
    #[serde(rename = "launcherPath")]
    pub launcher_path: PathBuf,
    #[serde(rename = "activeClientPath")]
    pub active_client_path: PathBuf,
}

impl Environment {
    pub fn for_project_root(project_root: PathBuf) -> Self {
        let runtime_endpoint_path = runtime_endpoint_path_for_project_root(&project_root);

        Self {
            project_root,
            runtime_endpoint_path,
        }
    }
}

pub fn runtime_endpoint_path<'a>(env_info: &'a Environment) -> &'a Path {
    &env_info.runtime_endpoint_path
}

#[derive(Debug, Clone)]
pub struct RpcInvokeFailure {
    pub code: i64,
    pub message: String,
    pub retryable: bool,
    pub detail: Option<String>,
    pub payload: Option<Value>,
}

#[derive(Debug, Clone)]
pub enum RpcClientError {
    Startup(StartupError),
    Protocol(String),
    Invoke(RpcInvokeFailure),
}

trait RpcIo: tokio::io::AsyncRead + tokio::io::AsyncWrite + Unpin + Send {}
impl<T> RpcIo for T where T: tokio::io::AsyncRead + tokio::io::AsyncWrite + Unpin + Send {}

#[derive(Debug, Deserialize)]
struct RpcErrorData {
    #[serde(default)]
    retryable: bool,
    #[serde(default)]
    detail: Option<String>,
}

#[derive(Debug, Deserialize)]
struct RpcErrorObject {
    code: i64,
    message: String,
    #[serde(default)]
    data: Option<RpcErrorData>,
}

#[derive(Debug, Deserialize)]
struct RpcEnvelope {
    #[serde(default)]
    result: Option<Value>,
    #[serde(default)]
    error: Option<RpcErrorObject>,
}

async fn connect_rpc_stream(
    env_info: &Environment,
) -> Result<BufReader<Box<dyn RpcIo>>, StartupError> {
    #[cfg(unix)]
    {
        let stream = UnixStream::connect(&env_info.runtime_endpoint_path)
            .await
            .map_err(|error| classify_unix_connect_error(env_info, &error))?;
        return Ok(BufReader::new(Box::new(stream)));
    }

    #[cfg(target_os = "windows")]
    {
        let pipe_path = env_info.runtime_endpoint_path.as_os_str();
        let client = loop {
            match ClientOptions::new().open(pipe_path) {
                Ok(client) => break client,
                Err(error) if error.raw_os_error() == Some(ERROR_PIPE_BUSY as i32) => {
                    sleep(Duration::from_millis(50)).await;
                }
                Err(error) => return Err(classify_windows_connect_error(env_info, &error)),
            }
        };
        return Ok(BufReader::new(Box::new(client)));
    }

    #[cfg(not(any(unix, target_os = "windows")))]
    {
        Err(StartupError::new(
            StartupErrorKind::ConnectFailed,
            StartupAction::VerifyRuntimeHealth,
            12,
            false,
            "unsupported platform: no runtime RPC transport configured",
        ))
    }
}

async fn rpc_request(
    env_info: &Environment,
    method: &str,
    params: Value,
) -> Result<Value, RpcClientError> {
    let mut stream = connect_rpc_stream(env_info)
        .await
        .map_err(RpcClientError::Startup)?;
    let request = serde_json::json!({
        "jsonrpc": "2.0",
        "id": "loomle-client",
        "method": method,
        "params": params,
    });
    let mut request_line = serde_json::to_string(&request).map_err(|error| {
        RpcClientError::Protocol(format!("failed to encode RPC request: {error}"))
    })?;
    request_line.push('\n');
    stream
        .get_mut()
        .write_all(request_line.as_bytes())
        .await
        .map_err(|error| {
            RpcClientError::Startup(
                StartupError::new(
                    StartupErrorKind::ConnectFailed,
                    StartupAction::VerifyRuntimeHealth,
                    12,
                    true,
                    "failed to write LOOMLE runtime RPC request.",
                )
                .with_detail(error.to_string()),
            )
        })?;
    stream.get_mut().flush().await.map_err(|error| {
        RpcClientError::Startup(
            StartupError::new(
                StartupErrorKind::ConnectFailed,
                StartupAction::VerifyRuntimeHealth,
                12,
                true,
                "failed to flush LOOMLE runtime RPC request.",
            )
            .with_detail(error.to_string()),
        )
    })?;

    let mut response_line = String::new();
    let read = stream
        .read_line(&mut response_line)
        .await
        .map_err(|error| {
            RpcClientError::Startup(
                StartupError::new(
                    StartupErrorKind::ConnectFailed,
                    StartupAction::VerifyRuntimeHealth,
                    12,
                    true,
                    "failed to read LOOMLE runtime RPC response.",
                )
                .with_detail(error.to_string()),
            )
        })?;
    if read == 0 {
        return Err(RpcClientError::Protocol(String::from(
            "runtime RPC connection closed without a response",
        )));
    }

    let envelope: RpcEnvelope = serde_json::from_str(response_line.trim()).map_err(|error| {
        RpcClientError::Protocol(format!("runtime RPC response is not valid JSON: {error}"))
    })?;
    if let Some(error) = envelope.error {
        let detail = error.data.as_ref().and_then(|data| data.detail.clone());
        let payload = detail
            .as_deref()
            .and_then(|raw| serde_json::from_str::<Value>(raw).ok());
        return Err(RpcClientError::Invoke(RpcInvokeFailure {
            code: error.code,
            message: error.message,
            retryable: error
                .data
                .as_ref()
                .map(|data| data.retryable)
                .unwrap_or(false),
            detail,
            payload,
        }));
    }

    envelope.result.ok_or_else(|| {
        RpcClientError::Protocol(String::from(
            "runtime RPC response missing both result and error",
        ))
    })
}

pub async fn rpc_health(env_info: &Environment) -> Result<Value, RpcClientError> {
    rpc_request(env_info, "rpc.health", Value::Object(JsonObject::new())).await
}

pub async fn rpc_capabilities(env_info: &Environment) -> Result<Value, RpcClientError> {
    rpc_request(
        env_info,
        "rpc.capabilities",
        Value::Object(JsonObject::new()),
    )
    .await
}

pub async fn rpc_invoke(
    env_info: &Environment,
    tool: &str,
    args: JsonObject,
) -> Result<Value, RpcClientError> {
    let result = rpc_request(
        env_info,
        "rpc.invoke",
        serde_json::json!({
            "tool": tool,
            "args": args,
        }),
    )
    .await?;

    let ok = result
        .get("ok")
        .and_then(|value| value.as_bool())
        .unwrap_or(false);
    if !ok {
        return Err(RpcClientError::Protocol(format!(
            "rpc.invoke for tool '{tool}' returned a non-ok result envelope"
        )));
    }

    Ok(result
        .get("payload")
        .cloned()
        .unwrap_or(Value::Object(JsonObject::new())))
}

#[cfg(unix)]
fn classify_unix_connect_error(env_info: &Environment, error: &std::io::Error) -> StartupError {
    use std::io::ErrorKind;

    let endpoint = env_info.runtime_endpoint_path.display().to_string();
    match error.kind() {
        ErrorKind::NotFound => StartupError::new(
            StartupErrorKind::EndpointMissing,
            StartupAction::StartUnrealEditor,
            10,
            true,
            "expected LOOMLE runtime endpoint was not found. Start Unreal Editor and wait for LoomleBridge to create the runtime socket.",
        )
        .with_endpoint(endpoint),
        ErrorKind::PermissionDenied => StartupError::new(
            StartupErrorKind::AccessDenied,
            StartupAction::RetryWithElevation,
            11,
            true,
            "LOOMLE runtime endpoint exists but the current client cannot access it.",
        )
        .with_endpoint(endpoint)
        .with_detail(error.to_string()),
        _ => StartupError::new(
            StartupErrorKind::ConnectFailed,
            StartupAction::VerifyRuntimeHealth,
            12,
            true,
            "failed to connect to the LOOMLE runtime socket. Verify that Unreal Editor is running and LoomleBridge is healthy.",
        )
        .with_endpoint(endpoint)
        .with_detail(error.to_string()),
    }
}

#[cfg(target_os = "windows")]
fn classify_windows_connect_error(env_info: &Environment, error: &std::io::Error) -> StartupError {
    let endpoint = env_info.runtime_endpoint_path.display().to_string();
    match error.raw_os_error() {
        Some(code) if code == ERROR_FILE_NOT_FOUND as i32 => StartupError::new(
            StartupErrorKind::EndpointMissing,
            StartupAction::StartUnrealEditor,
            10,
            true,
            "expected LOOMLE runtime pipe was not found. Start Unreal Editor and wait for LoomleBridge to create the named pipe.",
        )
        .with_endpoint(endpoint),
        Some(code) if code == ERROR_ACCESS_DENIED as i32 => StartupError::new(
            StartupErrorKind::AccessDenied,
            StartupAction::RetryWithElevation,
            11,
            true,
            "LOOMLE runtime pipe exists but the current client cannot access it.",
        )
        .with_endpoint(endpoint)
        .with_detail(error.to_string()),
        _ => StartupError::new(
            StartupErrorKind::ConnectFailed,
            StartupAction::VerifyRuntimeHealth,
            12,
            true,
            "failed to connect to the LOOMLE runtime pipe. Verify that Unreal Editor is running and LoomleBridge is healthy.",
        )
        .with_endpoint(endpoint)
        .with_detail(error.to_string()),
    }
}

pub fn resolve_project_root(explicit: Option<&Path>) -> Result<PathBuf, String> {
    if let Some(path) = explicit {
        return validate_project_root(path);
    }

    let cwd = env::current_dir().map_err(|error| format!("failed to read current dir: {error}"))?;
    for candidate in cwd.ancestors() {
        if has_uproject(candidate) {
            return Ok(candidate.to_path_buf());
        }
    }
    Err(format!(
        "could not discover a LOOMLE project root from {}. Use --project-root.",
        cwd.display()
    ))
}

pub fn validate_project_root(path: &Path) -> Result<PathBuf, String> {
    let resolved = path
        .canonicalize()
        .map_err(|error| format!("invalid --project-root {}: {error}", path.display()))?;
    #[cfg(target_os = "windows")]
    let resolved = strip_windows_verbatim_prefix(&resolved);
    if !resolved.is_dir() {
        return Err(format!("path is not a directory: {}", resolved.display()));
    }
    if !has_uproject(&resolved) {
        return Err(format!("no .uproject found under: {}", resolved.display()));
    }
    Ok(resolved)
}

pub fn should_handoff_to_active_client(
    current_exe: &Path,
    state: &ActiveInstallState,
) -> Result<Option<bool>, String> {
    let current = canonicalize_for_compare(current_exe).map_err(|error| {
        format!(
            "failed to resolve current executable {}: {error}",
            current_exe.display()
        )
    })?;
    let active = canonicalize_for_compare(&state.active_client_path).map_err(|error| {
        format!(
            "failed to resolve active client executable {}: {error}",
            state.active_client_path.display()
        )
    })?;
    if current == active {
        return Ok(Some(false));
    }
    let launcher = canonicalize_for_compare(&state.launcher_path).map_err(|error| {
        format!(
            "failed to resolve launcher executable {}: {error}",
            state.launcher_path.display()
        )
    })?;
    if current == launcher {
        return Ok(Some(true));
    }
    Ok(None)
}

pub fn parse_json_object(raw: Option<&str>, flag_name: &str) -> Result<JsonObject, String> {
    let Some(raw) = raw else {
        return Ok(JsonObject::new());
    };

    let value: Value = serde_json::from_str(raw).map_err(|error| {
        let mut message = format!("invalid JSON for {flag_name}: {error}");
        if cfg!(target_os = "windows") && flag_name == "--args" {
            message.push_str(
                ". On Windows PowerShell, prefer --args-file <path> for non-trivial JSON payloads.",
            );
        }
        message
    })?;
    match value {
        Value::Object(object) => Ok(object),
        _ => Err(format!("{flag_name} JSON must be an object")),
    }
}

pub fn extract_tool_payload(result: &CallToolResult) -> Result<Value, String> {
    if let Some(structured) = &result.structured_content {
        return Ok(structured.clone());
    }

    let Some(first) = result.content.first() else {
        return Err(String::from(
            "tool result missing both structuredContent and content",
        ));
    };
    let Some(text) = first.as_text() else {
        return Err(String::from("tool result content is not text"));
    };
    serde_json::from_str(&text.text)
        .map_err(|error| format!("tool result text is not valid JSON: {error}"))
}

pub fn render_json_pretty<T: serde::Serialize>(value: &T) -> Result<String, String> {
    serde_json::to_string_pretty(value).map_err(|error| format!("json encode failed: {error}"))
}

pub fn platform_key() -> &'static str {
    if cfg!(target_os = "macos") {
        "darwin"
    } else if cfg!(target_os = "windows") {
        "windows"
    } else {
        "linux"
    }
}

pub fn launcher_binary_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "loomle.exe"
    } else {
        "loomle"
    }
}

pub fn versioned_client_binary_name(version: &str) -> String {
    if cfg!(target_os = "windows") {
        format!("loomle-{version}.exe")
    } else {
        format!("loomle-{version}")
    }
}

fn has_uproject(dir: &Path) -> bool {
    match dir.read_dir() {
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

fn runtime_endpoint_path_for_project_root(project_root: &Path) -> PathBuf {
    #[cfg(unix)]
    {
        return project_root.join("Intermediate").join("loomle.sock");
    }

    #[cfg(target_os = "windows")]
    {
        return PathBuf::from(format!(
            r"\\.\pipe\{}",
            runtime_pipe_name_for_project_root(project_root)
        ));
    }

    #[cfg(not(any(unix, target_os = "windows")))]
    {
        let _ = project_root;
        return PathBuf::new();
    }
}

fn canonicalize_for_compare(path: &Path) -> std::io::Result<PathBuf> {
    let resolved = path.canonicalize()?;
    #[cfg(target_os = "windows")]
    {
        return Ok(strip_windows_verbatim_prefix(&resolved));
    }
    #[cfg(not(target_os = "windows"))]
    {
        Ok(resolved)
    }
}

#[cfg(target_os = "windows")]
fn runtime_pipe_name_for_project_root(project_root: &Path) -> String {
    let mut normalized = project_root.to_string_lossy().replace('\\', "/");
    while normalized.ends_with('/') {
        normalized.pop();
    }
    if normalized.is_empty() {
        normalized.push('/');
    }
    normalized.make_ascii_lowercase();
    format!("loomle-{:016x}", stable_fnv1a64(normalized.as_bytes()))
}

#[cfg(target_os = "windows")]
fn strip_windows_verbatim_prefix(path: &Path) -> PathBuf {
    let raw = path.to_string_lossy();
    if let Some(stripped) = raw.strip_prefix(r"\\?\UNC\") {
        return PathBuf::from(format!(r"\\{stripped}"));
    }
    if let Some(stripped) = raw.strip_prefix(r"\\?\") {
        return PathBuf::from(stripped);
    }
    path.to_path_buf()
}

#[cfg(target_os = "windows")]
fn stable_fnv1a64(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf29ce484222325u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x100000001b3u64);
    }
    hash
}

#[cfg(test)]
mod tests {
    use super::{
        launcher_binary_name, should_handoff_to_active_client, versioned_client_binary_name,
        ActiveInstallState,
    };
    #[cfg(target_os = "windows")]
    use super::{runtime_pipe_name_for_project_root, strip_windows_verbatim_prefix};
    use std::path::PathBuf;
    use std::{
        fs,
        time::{SystemTime, UNIX_EPOCH},
    };

    fn temp_dir(label: &str) -> PathBuf {
        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("time")
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("loomle-client-test-{label}-{unique}"));
        fs::create_dir_all(&dir).expect("mkdir");
        dir
    }

    #[test]
    fn handoff_needed_when_current_exe_differs_from_active_client() {
        let root = temp_dir("handoff");
        let launcher = root.join(launcher_binary_name());
        let active_dir = root.join("install").join("versions").join("0.5.0");
        fs::create_dir_all(&active_dir).expect("active dir");
        let active = active_dir.join(versioned_client_binary_name("0.5.0"));
        fs::write(&launcher, b"launcher").expect("write launcher");
        fs::write(&active, b"active").expect("write active");

        let state = ActiveInstallState {
            active_version: String::from("0.5.0"),
            launcher_path: launcher.clone(),
            active_client_path: active,
        };
        assert_eq!(
            should_handoff_to_active_client(&launcher, &state).expect("compare"),
            Some(true)
        );
    }

    #[test]
    fn handoff_not_needed_for_active_client_binary() {
        let root = temp_dir("active");
        let active_dir = root.join("install").join("versions").join("0.5.0");
        fs::create_dir_all(&active_dir).expect("active dir");
        let active = active_dir.join(versioned_client_binary_name("0.5.0"));
        fs::write(&active, b"active").expect("write active");

        let state = ActiveInstallState {
            active_version: String::from("0.5.0"),
            launcher_path: root.join(launcher_binary_name()),
            active_client_path: active.clone(),
        };
        assert_eq!(
            should_handoff_to_active_client(&active, &state).expect("compare"),
            Some(false)
        );
    }

    #[cfg(target_os = "windows")]
    #[test]
    fn strips_windows_verbatim_drive_prefix() {
        let path = PathBuf::from(r"\\?\E:\gaosh\actions-runner-work\lrh\LoomleRunnerHostWin");
        assert_eq!(
            strip_windows_verbatim_prefix(&path),
            PathBuf::from(r"E:\gaosh\actions-runner-work\lrh\LoomleRunnerHostWin")
        );
    }

    #[cfg(target_os = "windows")]
    #[test]
    fn pipe_name_matches_unreal_normalization_for_verbatim_paths() {
        let normal = PathBuf::from(r"E:\gaosh\actions-runner-work\lrh\LoomleRunnerHostWin");
        let verbatim = PathBuf::from(r"\\?\E:\gaosh\actions-runner-work\lrh\LoomleRunnerHostWin");
        assert_eq!(
            runtime_pipe_name_for_project_root(&normal),
            runtime_pipe_name_for_project_root(&strip_windows_verbatim_prefix(&verbatim))
        );
    }
}
