use rmcp::{
    model::{CallToolRequestParams, JsonObject},
    transport::{ConfigureCommandExt, TokioChildProcess},
    ServiceExt,
};
use serde_json::Value;
use std::env;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode, Stdio};
use tokio::process::Command as TokioCommand;

type LoomleClient = rmcp::service::RunningService<rmcp::service::RoleClient, ()>;

#[tokio::main(flavor = "multi_thread")]
async fn main() -> ExitCode {
    let cli = match Cli::parse(env::args_os()) {
        Ok(cli) => cli,
        Err(message) => {
            eprintln!("{message}");
            print_usage();
            return ExitCode::from(2);
        }
    };

    let project_root = match resolve_project_root(cli.project_root.as_deref()) {
        Ok(path) => path,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(2);
        }
    };

    let env_info = Environment::for_project_root(project_root);
    match cli.command {
        CommandKind::Doctor => run_doctor(&env_info),
        CommandKind::ServerPath => {
            println!("{}", env_info.server_path.display());
            ExitCode::SUCCESS
        }
        CommandKind::RunServer { forwarded_args } => run_server(&env_info, &forwarded_args),
        CommandKind::ListTools => run_list_tools(&env_info).await,
        CommandKind::Call {
            tool_name,
            arguments_json,
        } => run_call(&env_info, &tool_name, arguments_json.as_deref()).await,
    }
}

#[derive(Debug)]
struct Cli {
    project_root: Option<PathBuf>,
    command: CommandKind,
}

#[derive(Debug)]
enum CommandKind {
    Doctor,
    ServerPath,
    RunServer {
        forwarded_args: Vec<OsString>,
    },
    ListTools,
    Call {
        tool_name: String,
        arguments_json: Option<String>,
    },
}

impl Cli {
    fn parse<I>(args: I) -> Result<Self, String>
    where
        I: IntoIterator<Item = OsString>,
    {
        let mut args = args.into_iter();
        let _program = args.next();

        let mut project_root = None;
        let mut command = None;
        let mut forwarded_args = Vec::new();
        let mut call_tool_name = None;
        let mut call_arguments_json = None;
        let mut in_run_server_passthrough = false;

        while let Some(arg) = args.next() {
            if in_run_server_passthrough {
                forwarded_args.push(arg);
                continue;
            }

            match arg.to_str() {
                Some("--") => {
                    if matches!(command, Some(CommandKind::RunServer { .. })) {
                        in_run_server_passthrough = true;
                    } else {
                        return Err(String::from("unexpected `--` before run-server"));
                    }
                }
                Some("--project-root") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --project-root"))?;
                    project_root = Some(PathBuf::from(value));
                }
                Some("--args") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --args"))?;
                    if !matches!(command, Some(CommandKind::Call { .. })) {
                        return Err(String::from("--args is only valid with call"));
                    }
                    let value = value
                        .into_string()
                        .map_err(|_| String::from("--args must be valid UTF-8 JSON"))?;
                    call_arguments_json = Some(value);
                }
                Some("doctor") => command = Some(CommandKind::Doctor),
                Some("server-path") => command = Some(CommandKind::ServerPath),
                Some("run-server") => {
                    command = Some(CommandKind::RunServer {
                        forwarded_args: Vec::new(),
                    });
                }
                Some("list-tools") => command = Some(CommandKind::ListTools),
                Some("call") => {
                    command = Some(CommandKind::Call {
                        tool_name: String::new(),
                        arguments_json: None,
                    });
                }
                Some("--help") | Some("-h") | None => {
                    return Err(String::from("help requested"));
                }
                Some(other) => match command {
                    Some(CommandKind::RunServer { .. }) => {
                        forwarded_args.push(OsString::from(other));
                    }
                    Some(CommandKind::Call { .. }) => {
                        if call_tool_name.is_none() {
                            call_tool_name = Some(other.to_owned());
                        } else {
                            return Err(format!("unexpected extra argument for call: {other}"));
                        }
                    }
                    _ => {
                        return Err(format!("unknown argument or command: {other}"));
                    }
                },
            }
        }

        let command = match command {
            Some(CommandKind::RunServer { .. }) => CommandKind::RunServer { forwarded_args },
            Some(CommandKind::Call { .. }) => CommandKind::Call {
                tool_name: call_tool_name
                    .ok_or_else(|| String::from("call requires a tool name"))?,
                arguments_json: call_arguments_json,
            },
            Some(other) => other,
            None => CommandKind::Doctor,
        };

        Ok(Self {
            project_root,
            command,
        })
    }
}

#[derive(Debug)]
struct Environment {
    project_root: PathBuf,
    workspace_root: PathBuf,
    plugin_root: PathBuf,
    server_path: PathBuf,
}

impl Environment {
    fn for_project_root(project_root: PathBuf) -> Self {
        let workspace_root = project_root.join("Loomle");
        let plugin_root = project_root.join("Plugins").join("LoomleBridge");
        let server_path = plugin_root
            .join("Tools")
            .join("mcp")
            .join(platform_key())
            .join(server_binary_name());

        Self {
            project_root,
            workspace_root,
            plugin_root,
            server_path,
        }
    }
}

fn run_doctor(env_info: &Environment) -> ExitCode {
    let mut ok = true;

    println!("LOOMLE client");
    println!("project_root={}", env_info.project_root.display());
    println!("workspace_root={}", env_info.workspace_root.display());
    println!("plugin_root={}", env_info.plugin_root.display());
    println!("server_path={}", env_info.server_path.display());

    if !env_info.workspace_root.is_dir() {
        ok = false;
        eprintln!(
            "[loomle][ERROR] workspace root not found: {}",
            env_info.workspace_root.display()
        );
    }
    if !env_info.plugin_root.is_dir() {
        ok = false;
        eprintln!(
            "[loomle][ERROR] plugin root not found: {}",
            env_info.plugin_root.display()
        );
    }
    if !env_info.server_path.is_file() {
        ok = false;
        eprintln!(
            "[loomle][ERROR] server binary not found: {}",
            env_info.server_path.display()
        );
    }

    if ok {
        println!("status=ok");
        ExitCode::SUCCESS
    } else {
        println!("status=error");
        ExitCode::from(1)
    }
}

fn run_server(env_info: &Environment, forwarded_args: &[OsString]) -> ExitCode {
    if !env_info.server_path.is_file() {
        eprintln!(
            "[loomle][ERROR] cannot launch server; binary not found: {}",
            env_info.server_path.display()
        );
        return ExitCode::from(1);
    }

    let mut command = Command::new(&env_info.server_path);
    command
        .arg("--project-root")
        .arg(&env_info.project_root)
        .args(forwarded_args)
        .stdin(Stdio::inherit())
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit());

    let status = match command.status() {
        Ok(status) => status,
        Err(error) => {
            eprintln!(
                "[loomle][ERROR] failed to launch server {}: {}",
                env_info.server_path.display(),
                error
            );
            return ExitCode::from(1);
        }
    };

    match status.code() {
        Some(code) => ExitCode::from(code as u8),
        None => ExitCode::from(1),
    }
}

async fn run_list_tools(env_info: &Environment) -> ExitCode {
    let mut client = match connect_client(env_info).await {
        Ok(client) => client,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(1);
        }
    };

    let result = client.peer().list_all_tools().await.map_err(|error| {
        format!(
            "failed to list tools from {}: {}",
            env_info.server_path.display(),
            error
        )
    });
    let close_result = client.close().await;

    match result.and_then(|tools| print_json(&tools)).and_then(|_| {
        close_result
            .map(|_| ())
            .map_err(|error| format!("failed to close MCP session cleanly: {error}"))
    }) {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            ExitCode::from(1)
        }
    }
}

async fn run_call(
    env_info: &Environment,
    tool_name: &str,
    arguments_json: Option<&str>,
) -> ExitCode {
    let arguments = match parse_arguments(arguments_json) {
        Ok(arguments) => arguments,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(2);
        }
    };

    let mut client = match connect_client(env_info).await {
        Ok(client) => client,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(1);
        }
    };

    let request = CallToolRequestParams::new(tool_name.to_owned()).with_arguments(arguments);
    let result = client.peer().call_tool(request).await.map_err(|error| {
        format!(
            "failed to call tool `{}` via {}: {}",
            tool_name,
            env_info.server_path.display(),
            error
        )
    });
    let close_result = client.close().await;

    match result
        .and_then(|response| print_json(&response))
        .and_then(|_| {
            close_result
                .map(|_| ())
                .map_err(|error| format!("failed to close MCP session cleanly: {error}"))
        }) {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            ExitCode::from(1)
        }
    }
}

async fn connect_client(env_info: &Environment) -> Result<LoomleClient, String> {
    let transport = spawn_server_transport(env_info)?;
    ().serve(transport)
        .await
        .map_err(|error| format!("failed to establish MCP session: {error}"))
}

fn spawn_server_transport(env_info: &Environment) -> Result<TokioChildProcess, String> {
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

fn parse_arguments(raw: Option<&str>) -> Result<JsonObject, String> {
    let Some(raw) = raw else {
        return Ok(JsonObject::new());
    };

    let value: Value =
        serde_json::from_str(raw).map_err(|error| format!("invalid JSON for --args: {error}"))?;
    match value {
        Value::Object(object) => Ok(object),
        _ => Err(String::from("--args JSON must be an object")),
    }
}

fn print_json<T: serde::Serialize>(value: &T) -> Result<(), String> {
    let rendered = serde_json::to_string_pretty(value)
        .map_err(|error| format!("json encode failed: {error}"))?;
    println!("{rendered}");
    Ok(())
}

fn resolve_project_root(explicit: Option<&Path>) -> Result<PathBuf, String> {
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

fn validate_project_root(path: &Path) -> Result<PathBuf, String> {
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

fn platform_key() -> &'static str {
    if cfg!(target_os = "macos") {
        "darwin"
    } else if cfg!(target_os = "windows") {
        "windows"
    } else {
        "linux"
    }
}

fn server_binary_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "loomle_mcp_server.exe"
    } else {
        "loomle_mcp_server"
    }
}

fn print_usage() {
    eprintln!("Usage:");
    eprintln!("  loomle [--project-root <ProjectRoot>] doctor");
    eprintln!("  loomle [--project-root <ProjectRoot>] server-path");
    eprintln!("  loomle [--project-root <ProjectRoot>] run-server [-- <extra server args...>]");
    eprintln!("  loomle [--project-root <ProjectRoot>] list-tools");
    eprintln!("  loomle [--project-root <ProjectRoot>] call <tool-name> [--args <json-object>]");
    eprintln!();
    eprintln!("If --project-root is omitted, loomle searches upward from the current directory for a .uproject.");
}

#[cfg(test)]
mod tests {
    use super::{platform_key, server_binary_name, Cli, CommandKind};
    use std::ffi::OsString;
    use std::path::PathBuf;

    #[test]
    fn cli_defaults_to_doctor() {
        let cli = Cli::parse(vec![OsString::from("loomle")]).expect("cli");
        assert!(matches!(cli.command, CommandKind::Doctor));
        assert!(cli.project_root.is_none());
    }

    #[test]
    fn cli_parses_project_root_and_run_server_args() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("run-server"),
            OsString::from("--project-root"),
            OsString::from("/tmp/project"),
            OsString::from("--"),
            OsString::from("--verbose"),
        ])
        .expect("cli");

        assert_eq!(cli.project_root, Some(PathBuf::from("/tmp/project")));
        match cli.command {
            CommandKind::RunServer { forwarded_args } => {
                assert_eq!(forwarded_args, vec![OsString::from("--verbose")]);
            }
            _ => panic!("expected run-server"),
        }
    }

    #[test]
    fn cli_parses_project_root_after_doctor() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("doctor"),
            OsString::from("--project-root"),
            OsString::from("/tmp/project"),
        ])
        .expect("cli");

        assert_eq!(cli.project_root, Some(PathBuf::from("/tmp/project")));
        assert!(matches!(cli.command, CommandKind::Doctor));
    }

    #[test]
    fn cli_parses_call_command_with_args() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("--project-root"),
            OsString::from("/tmp/project"),
            OsString::from("call"),
            OsString::from("graph.query"),
            OsString::from("--args"),
            OsString::from("{\"assetPath\":\"/Game/Test\"}"),
        ])
        .expect("cli");

        match cli.command {
            CommandKind::Call {
                tool_name,
                arguments_json,
            } => {
                assert_eq!(tool_name, "graph.query");
                assert_eq!(
                    arguments_json.as_deref(),
                    Some("{\"assetPath\":\"/Game/Test\"}")
                );
            }
            _ => panic!("expected call"),
        }
    }

    #[test]
    fn cli_parses_list_tools_command() {
        let cli =
            Cli::parse(vec![OsString::from("loomle"), OsString::from("list-tools")]).expect("cli");

        assert!(matches!(cli.command, CommandKind::ListTools));
    }

    #[test]
    fn platform_and_binary_names_match_supported_layout() {
        assert!(matches!(platform_key(), "darwin" | "linux" | "windows"));
        if cfg!(target_os = "windows") {
            assert_eq!(server_binary_name(), "loomle_mcp_server.exe");
        } else {
            assert_eq!(server_binary_name(), "loomle_mcp_server");
        }
    }
}
