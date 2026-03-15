use loomle::{
    connect_client,
    install::{install_release, update_release, InstallRequest, UpdateRequest},
    parse_json_object, render_json_pretty, resolve_project_root, Environment,
};
use rmcp::model::CallToolRequestParams;
use serde::Deserialize;
use serde_json::{json, Value};
use std::env;
use std::ffi::OsString;
use std::path::PathBuf;
use std::process::ExitCode;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::sync::mpsc;

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

    match cli.command {
        CommandKind::Install {
            version,
            manifest_path,
            manifest_url,
        } => {
            let project_root = match resolve_project_root(cli.project_root.as_deref()) {
                Ok(path) => path,
                Err(message) => {
                    eprintln!("[loomle][ERROR] {message}");
                    return ExitCode::from(2);
                }
            };

            match install_release(InstallRequest {
                project_root,
                version,
                manifest_path,
                manifest_url,
            })
            .and_then(|result| print_json(&result))
            {
                Ok(()) => ExitCode::SUCCESS,
                Err(message) => {
                    eprintln!("[loomle][ERROR] {message}");
                    ExitCode::from(1)
                }
            }
        }
        CommandKind::Update {
            version,
            manifest_path,
            manifest_url,
            apply,
        } => {
            let project_root = match resolve_project_root(cli.project_root.as_deref()) {
                Ok(path) => path,
                Err(message) => {
                    eprintln!("[loomle][ERROR] {message}");
                    return ExitCode::from(2);
                }
            };

            match update_release(UpdateRequest {
                project_root,
                version,
                manifest_path,
                manifest_url,
                apply,
            })
            .and_then(|result| print_json(&result))
            {
                Ok(()) => ExitCode::SUCCESS,
                Err(message) => {
                    eprintln!("[loomle][ERROR] {message}");
                    ExitCode::from(1)
                }
            }
        }
        command => {
            let project_root = match resolve_project_root(cli.project_root.as_deref()) {
                Ok(path) => path,
                Err(message) => {
                    eprintln!("[loomle][ERROR] {message}");
                    return ExitCode::from(2);
                }
            };
            let env_info = Environment::for_project_root(project_root);

            match command {
                CommandKind::Doctor => run_doctor(&env_info),
                CommandKind::ServerPath => {
                    println!("{}", env_info.server_path.display());
                    ExitCode::SUCCESS
                }
                CommandKind::Session => run_session(&env_info).await,
                CommandKind::ListTools => run_list_tools(&env_info).await,
                CommandKind::Call {
                    tool_name,
                    arguments_json,
                } => run_call(&env_info, &tool_name, arguments_json.as_deref()).await,
                CommandKind::Install { .. } | CommandKind::Update { .. } => {
                    unreachable!("install/update handled above")
                }
            }
        }
    }
}

#[derive(Debug)]
struct Cli {
    project_root: Option<PathBuf>,
    command: CommandKind,
}

#[derive(Debug)]
enum CommandKind {
    Install {
        version: Option<String>,
        manifest_path: Option<PathBuf>,
        manifest_url: Option<String>,
    },
    Update {
        version: Option<String>,
        manifest_path: Option<PathBuf>,
        manifest_url: Option<String>,
        apply: bool,
    },
    Doctor,
    ServerPath,
    Session,
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
        let mut call_tool_name = None;
        let mut call_arguments_json = None;
        let mut install_version = None;
        let mut install_manifest_path = None;
        let mut install_manifest_url = None;
        let mut update_apply = false;

        while let Some(arg) = args.next() {
            match arg.to_str() {
                Some("--project-root") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --project-root"))?;
                    project_root = Some(PathBuf::from(value));
                }
                Some("--version") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --version"))?;
                    if !matches!(
                        command,
                        Some(CommandKind::Install { .. } | CommandKind::Update { .. })
                    ) {
                        return Err(String::from("--version is only valid with install or update"));
                    }
                    install_version = Some(
                        value
                            .into_string()
                            .map_err(|_| String::from("--version must be valid UTF-8"))?,
                    );
                }
                Some("--manifest-path") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --manifest-path"))?;
                    if !matches!(
                        command,
                        Some(CommandKind::Install { .. } | CommandKind::Update { .. })
                    ) {
                        return Err(String::from(
                            "--manifest-path is only valid with install or update",
                        ));
                    }
                    install_manifest_path = Some(PathBuf::from(value));
                }
                Some("--manifest-url") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --manifest-url"))?;
                    if !matches!(
                        command,
                        Some(CommandKind::Install { .. } | CommandKind::Update { .. })
                    ) {
                        return Err(String::from(
                            "--manifest-url is only valid with install or update",
                        ));
                    }
                    install_manifest_url = Some(
                        value
                            .into_string()
                            .map_err(|_| String::from("--manifest-url must be valid UTF-8"))?,
                    );
                }
                Some("--apply") => {
                    if !matches!(command, Some(CommandKind::Update { .. })) {
                        return Err(String::from("--apply is only valid with update"));
                    }
                    update_apply = true;
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
                Some("install") => {
                    command = Some(CommandKind::Install {
                        version: None,
                        manifest_path: None,
                        manifest_url: None,
                    });
                }
                Some("update") => {
                    command = Some(CommandKind::Update {
                        version: None,
                        manifest_path: None,
                        manifest_url: None,
                        apply: false,
                    });
                }
                Some("doctor") => command = Some(CommandKind::Doctor),
                Some("server-path") => command = Some(CommandKind::ServerPath),
                Some("session") => command = Some(CommandKind::Session),
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
            Some(CommandKind::Install { .. }) => CommandKind::Install {
                version: install_version,
                manifest_path: install_manifest_path,
                manifest_url: install_manifest_url,
            },
            Some(CommandKind::Update { .. }) => CommandKind::Update {
                version: install_version,
                manifest_path: install_manifest_path,
                manifest_url: install_manifest_url,
                apply: update_apply,
            },
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

#[derive(Debug, Deserialize)]
struct SessionRequest {
    id: Value,
    #[serde(default)]
    method: Option<String>,
    #[serde(default)]
    params: Option<Value>,
    #[serde(default)]
    tool: Option<String>,
    #[serde(default)]
    arguments: Option<Value>,
}

fn run_doctor(env_info: &Environment) -> ExitCode {
    let mut ok = true;

    println!("LOOMLE client");
    println!("project_root={}", env_info.project_root.display());
    println!("plugin_root={}", env_info.plugin_root.display());
    println!("server_path={}", env_info.server_path.display());

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

async fn run_session(env_info: &Environment) -> ExitCode {
    let mut client = match connect_client(env_info).await {
        Ok(client) => client,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(1);
        }
    };

    let peer = client.peer().clone();
    let (response_tx, mut response_rx) = mpsc::unbounded_channel::<String>();
    let writer = tokio::spawn(async move {
        let mut stdout = tokio::io::stdout();
        while let Some(line) = response_rx.recv().await {
            if stdout.write_all(line.as_bytes()).await.is_err() {
                break;
            }
            if stdout.write_all(b"\n").await.is_err() {
                break;
            }
            if stdout.flush().await.is_err() {
                break;
            }
        }
    });

    let mut lines = BufReader::new(tokio::io::stdin()).lines();
    let mut tasks = Vec::new();

    loop {
        let line = match lines.next_line().await {
            Ok(Some(line)) => line,
            Ok(None) => break,
            Err(error) => {
                eprintln!("[loomle][ERROR] session stdin read failed: {error}");
                break;
            }
        };

        if line.trim().is_empty() {
            continue;
        }

        match serde_json::from_str::<SessionRequest>(&line) {
            Ok(request) => {
                let response_tx = response_tx.clone();
                let peer = peer.clone();
                tasks.push(tokio::spawn(async move {
                    let response = handle_session_request(peer, request).await;
                    let _ = response_tx.send(response);
                }));
            }
            Err(error) => {
                let response = json!({
                    "id": Value::Null,
                    "ok": false,
                    "error": format!("invalid session request JSON: {error}")
                });
                let _ = response_tx.send(response.to_string());
            }
        }
    }

    for task in tasks {
        let _ = task.await;
    }
    drop(response_tx);
    let _ = writer.await;

    match client.close().await {
        Ok(_) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("[loomle][ERROR] failed to close MCP session cleanly: {error}");
            ExitCode::from(1)
        }
    }
}

async fn handle_session_request(
    peer: rmcp::service::Peer<rmcp::service::RoleClient>,
    request: SessionRequest,
) -> String {
    let requested_method = request
        .method
        .clone()
        .or_else(|| request.tool.as_ref().map(|_| String::from("tools/call")))
        .unwrap_or_default();

    match requested_method.as_str() {
        "initialize" => {
            let result = peer.peer_info().cloned().unwrap_or_default();
            json!({
                "jsonrpc": "2.0",
                "id": request.id,
                "ok": true,
                "result": result
            })
            .to_string()
        }
        "tools/list" => match peer.list_all_tools().await {
            Ok(tools) => json!({
                "jsonrpc": "2.0",
                "id": request.id,
                "ok": true,
                "result": {
                    "tools": tools
                }
            })
            .to_string(),
            Err(error) => json!({
                "jsonrpc": "2.0",
                "id": request.id,
                "ok": false,
                "error": error.to_string()
            })
            .to_string(),
        },
        "tools/call" => {
            let (tool_name, arguments) = match session_tool_call_parts(&request) {
                Ok(parts) => parts,
                Err(error) => {
                    return json!({
                        "jsonrpc": "2.0",
                        "id": request.id,
                        "ok": false,
                        "error": error
                    })
                    .to_string();
                }
            };

            match peer
                .call_tool(CallToolRequestParams::new(tool_name).with_arguments(arguments))
                .await
            {
                Ok(result) => json!({
                    "jsonrpc": "2.0",
                    "id": request.id,
                    "ok": true,
                    "result": result
                })
                .to_string(),
                Err(error) => json!({
                    "jsonrpc": "2.0",
                    "id": request.id,
                    "ok": false,
                    "error": error.to_string()
                })
                .to_string(),
            }
        }
        _ => json!({
            "jsonrpc": "2.0",
            "id": request.id,
            "ok": false,
            "error": format!("unsupported session method: {}", requested_method)
        })
        .to_string(),
    }
}

fn session_tool_call_parts(
    request: &SessionRequest,
) -> Result<(String, serde_json::Map<String, Value>), String> {
    if let Some(tool) = &request.tool {
        let arguments = match &request.arguments {
            Some(Value::Object(object)) => object.clone(),
            Some(_) => {
                return Err(String::from(
                    "session request arguments must be a JSON object",
                ))
            }
            None => serde_json::Map::new(),
        };
        return Ok((tool.clone(), arguments));
    }

    let params = request
        .params
        .as_ref()
        .and_then(Value::as_object)
        .ok_or_else(|| String::from("tools/call requires params object"))?;
    let tool_name = params
        .get("name")
        .and_then(Value::as_str)
        .ok_or_else(|| String::from("tools/call requires params.name"))?;
    let arguments = match params.get("arguments") {
        Some(Value::Object(object)) => object.clone(),
        Some(_) => {
            return Err(String::from(
                "tools/call requires params.arguments to be an object",
            ))
        }
        None => serde_json::Map::new(),
    };
    Ok((tool_name.to_owned(), arguments))
}

async fn run_call(
    env_info: &Environment,
    tool_name: &str,
    arguments_json: Option<&str>,
) -> ExitCode {
    let arguments = match parse_json_object(arguments_json, "--args") {
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

fn print_json<T: serde::Serialize>(value: &T) -> Result<(), String> {
    let rendered = render_json_pretty(value)?;
    println!("{rendered}");
    Ok(())
}

fn print_usage() {
    eprintln!("Usage:");
    eprintln!("  loomle [--project-root <ProjectRoot>] install [--version <Version>] [--manifest-path <ManifestPath> | --manifest-url <ManifestUrl>]");
    eprintln!("  loomle [--project-root <ProjectRoot>] update [--apply] [--version <Version>] [--manifest-path <ManifestPath> | --manifest-url <ManifestUrl>]");
    eprintln!("  loomle [--project-root <ProjectRoot>] doctor");
    eprintln!("  loomle [--project-root <ProjectRoot>] list-tools");
    eprintln!("  loomle [--project-root <ProjectRoot>] call <tool-name> [--args <json-object>]");
    eprintln!("  loomle [--project-root <ProjectRoot>] server-path");
    eprintln!("  loomle [--project-root <ProjectRoot>] session");
    eprintln!();
    eprintln!("Commands:");
    eprintln!("  doctor      check that the project-local plugin and MCP server are installed");
    eprintln!("  list-tools  print the live tool contract from the installed server");
    eprintln!("  call        make one tool request and print the JSON result");
    eprintln!("  session     start a persistent stdin/stdout JSON session for repeated, high-volume, or high-concurrency requests");
    eprintln!("  install     install LOOMLE into a project from a release manifest");
    eprintln!("  update      check for a newer LOOMLE release or apply it with --apply");
    eprintln!("  server-path print the resolved project-local MCP server binary path");
    eprintln!();
    eprintln!("If --project-root is omitted, loomle searches upward from the current directory for a .uproject.");
}

#[cfg(test)]
mod tests {
    use super::{Cli, CommandKind};
    use std::ffi::OsString;
    use std::path::PathBuf;

    #[test]
    fn cli_defaults_to_doctor() {
        let cli = Cli::parse(vec![OsString::from("loomle")]).expect("cli");
        assert!(matches!(cli.command, CommandKind::Doctor));
        assert!(cli.project_root.is_none());
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
    fn cli_parses_install_command_with_manifest_source() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("--project-root"),
            OsString::from("/tmp/project"),
            OsString::from("install"),
            OsString::from("--version"),
            OsString::from("0.1.0"),
            OsString::from("--manifest-path"),
            OsString::from("/tmp/manifest.json"),
        ])
        .expect("cli");

        assert_eq!(cli.project_root, Some(PathBuf::from("/tmp/project")));
        match cli.command {
            CommandKind::Install {
                version,
                manifest_path,
                manifest_url,
            } => {
                assert_eq!(version.as_deref(), Some("0.1.0"));
                assert_eq!(manifest_path, Some(PathBuf::from("/tmp/manifest.json")));
                assert!(manifest_url.is_none());
            }
            _ => panic!("expected install"),
        }
    }

    #[test]
    fn cli_parses_update_command_defaults_to_check() {
        let cli = Cli::parse(vec![OsString::from("loomle"), OsString::from("update")])
            .expect("cli");

        match cli.command {
            CommandKind::Update {
                apply,
                version,
                manifest_path,
                manifest_url,
            } => {
                assert!(!apply);
                assert!(version.is_none());
                assert!(manifest_path.is_none());
                assert!(manifest_url.is_none());
            }
            _ => panic!("expected update"),
        }
    }

    #[test]
    fn cli_parses_session_command() {
        let cli =
            Cli::parse(vec![OsString::from("loomle"), OsString::from("session")]).expect("cli");

        assert!(matches!(cli.command, CommandKind::Session));
    }

    #[test]
    fn platform_and_binary_names_match_supported_layout() {
        assert!(matches!(
            loomle::platform_key(),
            "darwin" | "linux" | "windows"
        ));
        if cfg!(target_os = "windows") {
            assert_eq!(loomle::server_binary_name(), "loomle_mcp_server.exe");
        } else {
            assert_eq!(loomle::server_binary_name(), "loomle_mcp_server");
        }
    }
}
