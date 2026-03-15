use rmcp::{
    model::{CallToolResult, JsonObject},
    transport::{ConfigureCommandExt, TokioChildProcess},
    ServiceExt,
};
use serde_json::Value;
use std::env;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use tokio::process::Command as TokioCommand;

pub type LoomleClient = rmcp::service::RunningService<rmcp::service::RoleClient, ()>;

pub mod install;

#[derive(Debug, Clone)]
pub struct Environment {
    pub project_root: PathBuf,
    pub plugin_root: PathBuf,
    pub server_path: PathBuf,
}

impl Environment {
    pub fn for_project_root(project_root: PathBuf) -> Self {
        let plugin_root = project_root.join("Plugins").join("LoomleBridge");
        let server_path = plugin_root
            .join("Tools")
            .join("mcp")
            .join(platform_key())
            .join(server_binary_name());

        Self {
            project_root,
            plugin_root,
            server_path,
        }
    }
}

pub async fn connect_client(env_info: &Environment) -> Result<LoomleClient, String> {
    let transport = spawn_server_transport(env_info)?;
    ().serve(transport)
        .await
        .map_err(|error| format!("failed to establish MCP session: {error}"))
}

pub fn spawn_server_transport(env_info: &Environment) -> Result<TokioChildProcess, String> {
    if !env_info.server_path.is_file() {
        return Err(format!(
            "cannot launch server; binary not found: {}",
            env_info.server_path.display()
        ));
    }

    TokioChildProcess::new(TokioCommand::new(&env_info.server_path).configure(|cmd| {
        cmd.arg("--project-root")
            .arg(&env_info.project_root)
            .stderr(Stdio::inherit());
    }))
    .map_err(|error| {
        format!(
            "failed to spawn MCP server {}: {}",
            env_info.server_path.display(),
            error
        )
    })
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

    let value: Value = serde_json::from_str(raw)
        .map_err(|error| format!("invalid JSON for {flag_name}: {error}"))?;
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

pub fn server_binary_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "loomle_mcp_server.exe"
    } else {
        "loomle_mcp_server"
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
