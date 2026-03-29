use rmcp::{
    model::{CallToolResult, JsonObject},
    ServiceExt,
};
use serde::Deserialize;
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

pub fn active_install_state_path(project_root: &Path) -> PathBuf {
    project_root.join("Loomle").join("install").join("active.json")
}

pub fn read_active_install_state(project_root: &Path) -> Result<ActiveInstallState, String> {
    let path = active_install_state_path(project_root);
    let raw = std::fs::read_to_string(&path)
        .map_err(|error| format!("failed to read active install state {}: {error}", path.display()))?;
    serde_json::from_str::<ActiveInstallState>(&raw)
        .map_err(|error| format!("failed to parse active install state {}: {error}", path.display()))
}

pub fn should_handoff_to_active_client(
    current_exe: &Path,
    state: &ActiveInstallState,
) -> Result<Option<bool>, String> {
    let current = canonicalize_for_compare(current_exe)
        .map_err(|error| format!("failed to resolve current executable {}: {error}", current_exe.display()))?;
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
        active_install_state_path, launcher_binary_name, should_handoff_to_active_client,
        versioned_client_binary_name, ActiveInstallState,
    };
    #[cfg(target_os = "windows")]
    use super::{runtime_pipe_name_for_project_root, strip_windows_verbatim_prefix};
    use std::path::PathBuf;
    use std::{fs, time::{SystemTime, UNIX_EPOCH}};

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
    fn active_install_state_path_is_under_loomle_install() {
        let project_root = PathBuf::from("/tmp/project");
        assert_eq!(
            active_install_state_path(&project_root),
            PathBuf::from("/tmp/project/Loomle/install/active.json")
        );
    }

    #[test]
    fn handoff_needed_when_current_exe_differs_from_active_client() {
        let root = temp_dir("handoff");
        let launcher = root.join(launcher_binary_name());
        let active_dir = root.join("install").join("versions").join("0.4.0");
        fs::create_dir_all(&active_dir).expect("active dir");
        let active = active_dir.join(versioned_client_binary_name("0.4.0"));
        fs::write(&launcher, b"launcher").expect("write launcher");
        fs::write(&active, b"active").expect("write active");

        let state = ActiveInstallState {
            active_version: String::from("0.4.0"),
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
        let active_dir = root.join("install").join("versions").join("0.4.0");
        fs::create_dir_all(&active_dir).expect("active dir");
        let active = active_dir.join(versioned_client_binary_name("0.4.0"));
        fs::write(&active, b"active").expect("write active");

        let state = ActiveInstallState {
            active_version: String::from("0.4.0"),
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
