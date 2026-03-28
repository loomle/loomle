use rmcp::{
    model::{CallToolResult, JsonObject},
    ServiceExt,
};
use serde_json::Value;
use std::env;
use std::path::{Path, PathBuf};
#[cfg(unix)]
use tokio::net::UnixStream;
#[cfg(target_os = "windows")]
use tokio::net::windows::named_pipe::ClientOptions;
#[cfg(target_os = "windows")]
use tokio::time::{sleep, Duration};
#[cfg(target_os = "windows")]
use windows_sys::Win32::Foundation::ERROR_PIPE_BUSY;

pub type LoomleClient = rmcp::service::RunningService<rmcp::service::RoleClient, ()>;

pub mod install;
pub mod skill;

#[derive(Debug, Clone)]
pub struct Environment {
    pub project_root: PathBuf,
    pub plugin_root: PathBuf,
    pub runtime_endpoint_path: PathBuf,
}

impl Environment {
    pub fn for_project_root(project_root: PathBuf) -> Self {
        let plugin_root = project_root.join("Plugins").join("LoomleBridge");
        let runtime_endpoint_path = runtime_endpoint_path_for_project_root(&project_root);

        Self {
            project_root,
            plugin_root,
            runtime_endpoint_path,
        }
    }
}

pub fn runtime_endpoint_path<'a>(env_info: &'a Environment) -> &'a Path {
    &env_info.runtime_endpoint_path
}

pub async fn connect_client(env_info: &Environment) -> Result<LoomleClient, String> {
    #[cfg(unix)]
    {
        let stream = UnixStream::connect(&env_info.runtime_endpoint_path)
            .await
            .map_err(|error| {
                format!(
                    "failed to connect to LOOMLE runtime socket {}: {}",
                    env_info.runtime_endpoint_path.display(),
                    error
                )
            })?;
        return ()
            .serve(stream)
            .await
            .map_err(|error| format!("failed to establish MCP session: {error}"));
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
                Err(error) => {
                    return Err(format!(
                        "failed to connect to LOOMLE runtime pipe {}: {}",
                        env_info.runtime_endpoint_path.display(),
                        error
                    ));
                }
            }
        };
        return ()
            .serve(client)
            .await
            .map_err(|error| format!("failed to establish MCP session: {error}"));
    }

    #[cfg(not(any(unix, target_os = "windows")))]
    {
        Err(String::from(
            "unsupported platform: no MCP runtime transport configured",
        ))
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
    if !resolved.is_dir() {
        return Err(format!("path is not a directory: {}", resolved.display()));
    }
    if !has_uproject(&resolved) {
        return Err(format!("no .uproject found under: {}", resolved.display()));
    }
    Ok(resolved)
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

pub fn project_client_binary_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "loomle.exe"
    } else {
        "loomle"
    }
}

pub fn installer_binary_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "loomle-installer.exe"
    } else {
        "loomle-installer"
    }
}

pub fn is_installer_binary_path(path: &Path) -> bool {
    path.file_name()
        .and_then(|name| name.to_str())
        .map(|name| name.eq_ignore_ascii_case(installer_binary_name()))
        .unwrap_or(false)
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
fn stable_fnv1a64(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf29ce484222325u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x100000001b3u64);
    }
    hash
}
