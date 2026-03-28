use loomle::{
    connect_client,
    install::{
        download_temp_installer, install_release, update_release, InstallRequest,
        InstallerDownloadRequest, UpdateRequest,
    },
    is_installer_binary_path, parse_json_object, render_json_pretty, resolve_project_root,
    runtime_endpoint_path, runtime_server_binary_required,
    skill::{
        install_skill, list_skills, remove_skill, SkillInstallRequest, SkillListRequest,
        SkillRemoveRequest,
    },
    Environment,
};
use rmcp::model::{CallToolRequestParams, Meta};
use serde::Deserialize;
use serde_json::{json, Value};
use std::env;
use std::ffi::OsString;
use std::fs;
use std::path::PathBuf;
use std::process::{Command as StdCommand, ExitCode};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::sync::mpsc;

#[cfg(target_os = "windows")]
use windows_sys::Win32::Foundation::{CloseHandle, WAIT_FAILED, WAIT_OBJECT_0, WAIT_TIMEOUT};
#[cfg(target_os = "windows")]
use windows_sys::Win32::System::Threading::{OpenProcess, WaitForSingleObject};

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

    if let Some(parent_pid) = cli.wait_for_parent_pid {
        if let Err(message) = wait_for_parent_exit(parent_pid) {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(1);
        }
    }

    match cli.command {
        CommandKind::Install {
            version,
            manifest_path,
            manifest_url,
            installer_path,
        } => {
            let project_root = match resolve_project_root(cli.project_root.as_deref()) {
                Ok(path) => path,
                Err(message) => {
                    eprintln!("[loomle][ERROR] {message}");
                    return ExitCode::from(2);
                }
            };

            if !current_process_is_installer() {
                return handoff_install_to_installer(
                    project_root,
                    version,
                    manifest_path,
                    manifest_url,
                    installer_path,
                );
            }

            run_blocking_json(move || {
                install_release(InstallRequest {
                    project_root,
                    version,
                    manifest_path,
                    manifest_url,
                })
            })
            .await
        }
        CommandKind::Update {
            version,
            manifest_path,
            manifest_url,
            installer_path,
            apply,
        } => {
            let project_root = match resolve_project_root(cli.project_root.as_deref()) {
                Ok(path) => path,
                Err(message) => {
                    eprintln!("[loomle][ERROR] {message}");
                    return ExitCode::from(2);
                }
            };

            if apply && !current_process_is_installer() {
                return handoff_update_to_installer(
                    project_root,
                    version,
                    manifest_path,
                    manifest_url,
                    installer_path,
                );
            }

            run_blocking_json(move || {
                update_release(UpdateRequest {
                    project_root,
                    version,
                    manifest_path,
                    manifest_url,
                    apply,
                })
            })
            .await
        }
        CommandKind::Skill(skill_command) => match skill_command {
            SkillCommand::List {
                installed_only,
                skills_root,
                registry_url,
            } => {
                run_blocking_json(move || {
                    list_skills(SkillListRequest {
                        installed_only,
                        skills_root,
                        registry_url,
                    })
                })
                .await
            }
            SkillCommand::Install {
                skill_name,
                skills_root,
                registry_url,
            } => {
                run_blocking_json(move || {
                    install_skill(SkillInstallRequest {
                        skill_name,
                        skills_root,
                        registry_url,
                    })
                })
                .await
            }
            SkillCommand::Remove {
                skill_name,
                skills_root,
            } => {
                run_blocking_json(move || {
                    remove_skill(SkillRemoveRequest {
                        skill_name,
                        skills_root,
                    })
                })
                .await
            }
        },
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
                    println!("{}", runtime_endpoint_path(&env_info).display());
                    ExitCode::SUCCESS
                }
                CommandKind::Session => run_session(&env_info).await,
                CommandKind::ListTools => run_list_tools(&env_info).await,
                CommandKind::Call {
                    tool_name,
                    arguments_json,
                    arguments_file,
                } => {
                    run_call(
                        &env_info,
                        &tool_name,
                        arguments_json.as_deref(),
                        arguments_file.as_deref(),
                    )
                    .await
                }
                CommandKind::Install { .. }
                | CommandKind::Update { .. }
                | CommandKind::Skill(_) => {
                    unreachable!("install/update handled above")
                }
            }
        }
    }
}

fn current_process_is_installer() -> bool {
    env::current_exe()
        .ok()
        .as_deref()
        .map(is_installer_binary_path)
        .unwrap_or(false)
}

fn handoff_update_to_installer(
    project_root: PathBuf,
    version: Option<String>,
    manifest_path: Option<PathBuf>,
    manifest_url: Option<String>,
    installer_path: Option<PathBuf>,
) -> ExitCode {
    handoff_to_installer(
        "update",
        project_root,
        version,
        manifest_path,
        manifest_url,
        installer_path,
        true,
    )
}

fn handoff_install_to_installer(
    project_root: PathBuf,
    version: Option<String>,
    manifest_path: Option<PathBuf>,
    manifest_url: Option<String>,
    installer_path: Option<PathBuf>,
) -> ExitCode {
    handoff_to_installer(
        "install",
        project_root,
        version,
        manifest_path,
        manifest_url,
        installer_path,
        false,
    )
}

fn handoff_to_installer(
    subcommand: &str,
    project_root: PathBuf,
    version: Option<String>,
    manifest_path: Option<PathBuf>,
    manifest_url: Option<String>,
    installer_path: Option<PathBuf>,
    apply: bool,
) -> ExitCode {
    let installer = match download_temp_installer(&InstallerDownloadRequest {
        version: version.clone(),
        manifest_path: manifest_path.clone(),
        manifest_url: manifest_url.clone(),
        installer_path: installer_path.clone(),
    }) {
        Ok(installer) => installer,
        Err(message) => {
            eprintln!("[loomle][ERROR] {message}");
            return ExitCode::from(1);
        }
    };

    let mut command = StdCommand::new(installer.path());
    command
        .arg("--project-root")
        .arg(&project_root)
        .arg(subcommand);
    if apply {
        command.arg("--apply");
    }
    if let Some(version) = &version {
        command.arg("--version").arg(version);
    }
    if let Some(manifest_path) = &manifest_path {
        command.arg("--manifest-path").arg(manifest_path);
    }
    if let Some(manifest_url) = &manifest_url {
        command.arg("--manifest-url").arg(manifest_url);
    }
    if let Some(installer_path) = &installer_path {
        command.arg("--installer-path").arg(installer_path);
    }

    #[cfg(target_os = "windows")]
    if apply && subcommand == "update" {
        command
            .arg("--wait-for-parent-pid")
            .arg(std::process::id().to_string());
        eprintln!(
            "[loomle][INFO] handing off {}{} to temporary installer {} and exiting so Windows can release the current workspace binary",
            subcommand,
            if apply { " --apply" } else { "" },
            installer.path().display()
        );
        return match command.spawn() {
            Ok(_) => ExitCode::SUCCESS,
            Err(error) => {
                eprintln!(
                    "[loomle][ERROR] failed to launch temporary installer {}: {}",
                    installer.path().display(),
                    error
                );
                ExitCode::from(1)
            }
        };
    }

    eprintln!(
        "[loomle][INFO] handing off {}{} to temporary installer {}",
        subcommand,
        if apply { " --apply" } else { "" },
        installer.path().display()
    );
    match command.status() {
        Ok(status) if status.success() => ExitCode::SUCCESS,
        Ok(status) => match status.code() {
            Some(code) if (0..=255).contains(&code) => ExitCode::from(code as u8),
            _ => ExitCode::from(1),
        },
        Err(error) => {
            eprintln!(
                "[loomle][ERROR] failed to launch temporary installer {}: {}",
                installer.path().display(),
                error
            );
            ExitCode::from(1)
        }
    }
}

#[derive(Debug)]
struct Cli {
    project_root: Option<PathBuf>,
    wait_for_parent_pid: Option<u32>,
    command: CommandKind,
}

#[derive(Debug)]
enum CommandKind {
    Install {
        version: Option<String>,
        manifest_path: Option<PathBuf>,
        manifest_url: Option<String>,
        installer_path: Option<PathBuf>,
    },
    Update {
        version: Option<String>,
        manifest_path: Option<PathBuf>,
        manifest_url: Option<String>,
        installer_path: Option<PathBuf>,
        apply: bool,
    },
    Skill(SkillCommand),
    Doctor,
    ServerPath,
    Session,
    ListTools,
    Call {
        tool_name: String,
        arguments_json: Option<String>,
        arguments_file: Option<PathBuf>,
    },
}

#[derive(Debug)]
enum SkillCommand {
    List {
        installed_only: bool,
        skills_root: Option<PathBuf>,
        registry_url: Option<String>,
    },
    Install {
        skill_name: String,
        skills_root: Option<PathBuf>,
        registry_url: Option<String>,
    },
    Remove {
        skill_name: String,
        skills_root: Option<PathBuf>,
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
        let mut wait_for_parent_pid = None;
        let mut command = None;
        let mut call_tool_name = None;
        let mut call_arguments_json = None;
        let mut call_arguments_file = None;
        let mut install_version = None;
        let mut install_manifest_path = None;
        let mut install_manifest_url = None;
        let mut update_installer_path = None;
        let mut update_apply = false;
        let mut in_skill_namespace = false;
        let mut skill_subcommand = None::<String>;
        let mut skill_name = None;
        let mut skill_installed_only = false;
        let mut skill_skills_root = None;
        let mut skill_registry_url = None;

        while let Some(arg) = args.next() {
            match arg.to_str() {
                Some("--project-root") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --project-root"))?;
                    project_root = Some(PathBuf::from(value));
                }
                Some("--wait-for-parent-pid") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --wait-for-parent-pid"))?;
                    wait_for_parent_pid = Some(
                        value
                            .to_string_lossy()
                            .parse::<u32>()
                            .map_err(|_| String::from("--wait-for-parent-pid must be a valid pid"))?,
                    );
                }
                Some("--version") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --version"))?;
                    if !matches!(
                        command,
                        Some(CommandKind::Install { .. } | CommandKind::Update { .. })
                    ) {
                        return Err(String::from(
                            "--version is only valid with install or update",
                        ));
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
                Some("--installer-path") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --installer-path"))?;
                    if !matches!(
                        command,
                        Some(CommandKind::Install { .. } | CommandKind::Update { .. })
                    ) {
                        return Err(String::from(
                            "--installer-path is only valid with install or update",
                        ));
                    }
                    update_installer_path = Some(PathBuf::from(value));
                }
                Some("--apply") => {
                    if !matches!(command, Some(CommandKind::Update { .. })) {
                        return Err(String::from("--apply is only valid with update"));
                    }
                    update_apply = true;
                }
                Some("--installed") => {
                    if !(in_skill_namespace && skill_subcommand.as_deref() == Some("list")) {
                        return Err(String::from("--installed is only valid with `skill list`"));
                    }
                    skill_installed_only = true;
                }
                Some("--skills-root") => {
                    if !in_skill_namespace {
                        return Err(String::from("--skills-root is only valid with `skill ...`"));
                    }
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --skills-root"))?;
                    skill_skills_root = Some(PathBuf::from(value));
                }
                Some("--registry-url") => {
                    if !in_skill_namespace {
                        return Err(String::from(
                            "--registry-url is only valid with `skill ...`",
                        ));
                    }
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --registry-url"))?;
                    skill_registry_url = Some(
                        value
                            .into_string()
                            .map_err(|_| String::from("--registry-url must be valid UTF-8"))?,
                    );
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
                Some("--args-file") => {
                    let value = args
                        .next()
                        .ok_or_else(|| String::from("missing value for --args-file"))?;
                    if !matches!(command, Some(CommandKind::Call { .. })) {
                        return Err(String::from("--args-file is only valid with call"));
                    }
                    call_arguments_file = Some(PathBuf::from(value));
                }
                Some("skill") => {
                    if command.is_some() || in_skill_namespace {
                        return Err(String::from("skill must be the only command"));
                    }
                    in_skill_namespace = true;
                }
                Some("list") if in_skill_namespace && skill_subcommand.is_none() => {
                    skill_subcommand = Some(String::from("list"));
                }
                Some("install") if in_skill_namespace && skill_subcommand.is_none() => {
                    skill_subcommand = Some(String::from("install"));
                }
                Some("remove") if in_skill_namespace && skill_subcommand.is_none() => {
                    skill_subcommand = Some(String::from("remove"));
                }
                Some("install") => {
                    command = Some(CommandKind::Install {
                        version: None,
                        manifest_path: None,
                        manifest_url: None,
                        installer_path: None,
                    });
                }
                Some("update") => {
                    command = Some(CommandKind::Update {
                        version: None,
                        manifest_path: None,
                        manifest_url: None,
                        installer_path: None,
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
                        arguments_file: None,
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
                    _ if in_skill_namespace => match skill_subcommand.as_deref() {
                        Some("install" | "remove") => {
                            if skill_name.is_none() {
                                skill_name = Some(other.to_owned());
                            } else {
                                return Err(format!(
                                    "unexpected extra argument for `skill {}`: {other}",
                                    skill_subcommand.as_deref().unwrap_or_default()
                                ));
                            }
                        }
                        Some(other_command) => {
                            return Err(format!(
                                "unexpected extra argument for `skill {other_command}`: {other}"
                            ));
                        }
                        None => {
                            return Err(format!(
                                "unknown `skill` subcommand: {other}. Expected one of: list, install, remove"
                            ));
                        }
                    },
                    _ => {
                        return Err(format!("unknown argument or command: {other}"));
                    }
                },
            }
        }

        let command = if in_skill_namespace {
            match skill_subcommand.as_deref() {
                Some("list") => CommandKind::Skill(SkillCommand::List {
                    installed_only: skill_installed_only,
                    skills_root: skill_skills_root,
                    registry_url: skill_registry_url,
                }),
                Some("install") => CommandKind::Skill(SkillCommand::Install {
                    skill_name: skill_name
                        .ok_or_else(|| String::from("skill install requires a skill name"))?,
                    skills_root: skill_skills_root,
                    registry_url: skill_registry_url,
                }),
                Some("remove") => CommandKind::Skill(SkillCommand::Remove {
                    skill_name: skill_name
                        .ok_or_else(|| String::from("skill remove requires a skill name"))?,
                    skills_root: skill_skills_root,
                }),
                Some(other) => return Err(format!("unsupported skill subcommand: {other}")),
                None => {
                    return Err(String::from(
                        "skill requires a subcommand: list, install, or remove",
                    ))
                }
            }
        } else {
            match command {
                Some(CommandKind::Install { .. }) => CommandKind::Install {
                    version: install_version,
                    manifest_path: install_manifest_path,
                    manifest_url: install_manifest_url,
                    installer_path: update_installer_path.clone(),
                },
                Some(CommandKind::Update { .. }) => CommandKind::Update {
                    version: install_version,
                    manifest_path: install_manifest_path,
                    manifest_url: install_manifest_url,
                    installer_path: update_installer_path,
                    apply: update_apply,
                },
                Some(CommandKind::Call { .. }) => CommandKind::Call {
                    tool_name: call_tool_name
                        .ok_or_else(|| String::from("call requires a tool name"))?,
                    arguments_json: call_arguments_json,
                    arguments_file: call_arguments_file,
                },
                Some(other) => other,
                None => CommandKind::Doctor,
            }
        };

        Ok(Self {
            project_root,
            wait_for_parent_pid,
            command,
        })
    }
}

#[cfg(target_os = "windows")]
fn wait_for_parent_exit(pid: u32) -> Result<(), String> {
    const PROCESS_SYNCHRONIZE_ACCESS: u32 = 0x0010_0000;
    unsafe {
        let handle = OpenProcess(PROCESS_SYNCHRONIZE_ACCESS, 0, pid);
        if handle.is_null() {
            return Ok(());
        }
        let wait_result = WaitForSingleObject(handle, 30000);
        CloseHandle(handle);
        match wait_result {
            WAIT_OBJECT_0 => Ok(()),
            WAIT_TIMEOUT => Err(format!(
                "temporary installer timed out waiting for parent process {pid} to exit"
            )),
            WAIT_FAILED => Err(format!(
                "temporary installer failed while waiting for parent process {pid}"
            )),
            other => Err(format!(
                "temporary installer received unexpected wait result {other} while waiting for parent process {pid}"
            )),
        }
    }
}

#[cfg(not(target_os = "windows"))]
fn wait_for_parent_exit(_pid: u32) -> Result<(), String> {
    Ok(())
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
    println!("runtime_endpoint_path={}", runtime_endpoint_path(env_info).display());
    println!("runtime_socket_path={}", env_info.runtime_socket_path.display());

    if !env_info.plugin_root.is_dir() {
        ok = false;
        eprintln!(
            "[loomle][ERROR] plugin root not found: {}",
            env_info.plugin_root.display()
        );
    }
    if runtime_server_binary_required() && !env_info.server_path.is_file() {
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
            runtime_endpoint_path(env_info).display(),
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
    arguments_file: Option<&std::path::Path>,
) -> ExitCode {
    let raw_arguments = match (arguments_json, arguments_file) {
        (Some(_), Some(_)) => {
            eprintln!("[loomle][ERROR] --args and --args-file are mutually exclusive");
            return ExitCode::from(2);
        }
        (Some(raw), None) => Some(raw.to_owned()),
        (None, Some(path)) => match fs::read_to_string(path) {
            Ok(raw) => Some(raw),
            Err(error) => {
                eprintln!(
                    "[loomle][ERROR] failed to read --args-file {}: {error}",
                    path.display()
                );
                return ExitCode::from(2);
            }
        },
        (None, None) => None,
    };

    let arguments = match parse_json_object(raw_arguments.as_deref(), "--args") {
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

    let deadline_ms = job_wait_ms_from_arguments(&arguments)
        .or_else(|| execute_timeout_ms_from_arguments(tool_name, &arguments));
    let mut request = CallToolRequestParams::new(tool_name.to_owned()).with_arguments(arguments);
    if let Some(deadline_ms) = deadline_ms {
        request.meta = Some(Meta(serde_json::Map::from_iter([(
            String::from("deadlineMs"),
            json!(deadline_ms),
        )])));
    }
    let result = client.peer().call_tool(request).await.map_err(|error| {
        format!(
            "failed to call tool `{}` via {}: {}",
            tool_name,
            runtime_endpoint_path(env_info).display(),
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

fn execute_timeout_ms_from_arguments(
    tool_name: &str,
    arguments: &serde_json::Map<String, Value>,
) -> Option<u64> {
    if tool_name != "execute" {
        return None;
    }

    arguments
        .get("timeoutMs")
        .and_then(Value::as_u64)
        .filter(|timeout_ms| *timeout_ms > 0)
}

fn job_wait_ms_from_arguments(arguments: &serde_json::Map<String, Value>) -> Option<u64> {
    arguments
        .get("execution")
        .and_then(Value::as_object)
        .filter(|execution| {
            execution
                .get("mode")
                .and_then(Value::as_str)
                .map(|mode| mode == "job")
                .unwrap_or(false)
        })
        .and_then(|execution| execution.get("waitMs"))
        .and_then(Value::as_u64)
        .filter(|wait_ms| *wait_ms > 0)
}

fn print_json<T: serde::Serialize>(value: &T) -> Result<(), String> {
    let rendered = render_json_pretty(value)?;
    println!("{rendered}");
    Ok(())
}

async fn run_blocking_json<F>(task: F) -> ExitCode
where
    F: FnOnce() -> Result<Value, String> + Send + 'static,
{
    match tokio::task::spawn_blocking(task).await {
        Ok(Ok(result)) => match print_json(&result).and_then(|_| emit_restart_notice(&result)) {
            Ok(()) => ExitCode::SUCCESS,
            Err(message) => {
                eprintln!("[loomle][ERROR] {message}");
                ExitCode::from(1)
            }
        },
        Ok(Err(message)) => {
            eprintln!("[loomle][ERROR] {message}");
            ExitCode::from(1)
        }
        Err(error) => {
            eprintln!("[loomle][ERROR] blocking task failed: {error}");
            ExitCode::from(1)
        }
    }
}

fn emit_restart_notice(result: &Value) -> Result<(), String> {
    let restart_required = result
        .get("restartRequired")
        .and_then(Value::as_bool)
        .unwrap_or(false);
    if !restart_required {
        return Ok(());
    }

    let reason = result
        .get("restartReason")
        .and_then(Value::as_str)
        .unwrap_or(
            "If Unreal Editor is already running, restart it so LOOMLE changes take effect.",
        );
    eprintln!("[loomle][NOTE] {reason}");
    Ok(())
}

fn print_usage() {
    eprintln!("Usage:");
    eprintln!("  loomle [--project-root <ProjectRoot>] install [--version <Version>] [--manifest-path <ManifestPath> | --manifest-url <ManifestUrl>] [--installer-path <InstallerPath>]");
    eprintln!("  loomle [--project-root <ProjectRoot>] update [--apply] [--version <Version>] [--manifest-path <ManifestPath> | --manifest-url <ManifestUrl>] [--installer-path <InstallerPath>]");
    eprintln!("  loomle [--project-root <ProjectRoot>] doctor");
    eprintln!("  loomle skill list [--installed] [--skills-root <SkillsRoot>] [--registry-url <RegistryUrl>]");
    eprintln!("  loomle skill install <skill-name> [--skills-root <SkillsRoot>] [--registry-url <RegistryUrl>]");
    eprintln!("  loomle skill remove <skill-name> [--skills-root <SkillsRoot>]");
    eprintln!("  loomle [--project-root <ProjectRoot>] list-tools");
    eprintln!("  loomle [--project-root <ProjectRoot>] call <tool-name> [--args <json-object> | --args-file <json-file>]");
    eprintln!("  loomle [--project-root <ProjectRoot>] server-path");
    eprintln!("  loomle [--project-root <ProjectRoot>] session");
    eprintln!();
    eprintln!("Commands:");
    eprintln!("  doctor      check that the project-local plugin and runtime endpoint are installed");
    eprintln!("  skill       list, install, or remove official LOOMLE skills");
    eprintln!("  list-tools  print the live tool contract from the installed server");
    eprintln!("  call        make one tool request and print the JSON result");
    eprintln!("  session     start a persistent stdin/stdout JSON session for repeated, high-volume, or high-concurrency requests");
    eprintln!("  install     install LOOMLE into a project from a release manifest");
    eprintln!("  update      check for a newer LOOMLE release or apply it with --apply");
    eprintln!("  server-path print the resolved project-local runtime endpoint path");
    eprintln!();
    eprintln!("If --project-root is omitted, loomle searches upward from the current directory for a .uproject.");
}

#[cfg(test)]
mod tests {
    use super::{execute_timeout_ms_from_arguments, job_wait_ms_from_arguments, Cli, CommandKind, SkillCommand};
    use serde_json::json;
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
                arguments_file,
            } => {
                assert_eq!(tool_name, "graph.query");
                assert_eq!(
                    arguments_json.as_deref(),
                    Some("{\"assetPath\":\"/Game/Test\"}")
                );
                assert!(arguments_file.is_none());
            }
            _ => panic!("expected call"),
        }
    }

    #[test]
    fn cli_parses_call_command_with_args_file() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("call"),
            OsString::from("graph.list"),
            OsString::from("--args-file"),
            OsString::from("args.json"),
        ])
        .expect("cli");

        match cli.command {
            CommandKind::Call {
                tool_name,
                arguments_json,
                arguments_file,
            } => {
                assert_eq!(tool_name, "graph.list");
                assert!(arguments_json.is_none());
                assert_eq!(arguments_file, Some(PathBuf::from("args.json")));
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
    fn cli_parses_internal_wait_for_parent_pid() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("--wait-for-parent-pid"),
            OsString::from("4242"),
            OsString::from("doctor"),
        ])
        .expect("cli");

        assert_eq!(cli.wait_for_parent_pid, Some(4242));
        assert!(matches!(cli.command, CommandKind::Doctor));
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
                installer_path,
            } => {
                assert_eq!(version.as_deref(), Some("0.1.0"));
                assert_eq!(manifest_path, Some(PathBuf::from("/tmp/manifest.json")));
                assert!(manifest_url.is_none());
                assert!(installer_path.is_none());
            }
            _ => panic!("expected install"),
        }
    }

    #[test]
    fn cli_parses_install_command_with_installer_path() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("install"),
            OsString::from("--installer-path"),
            OsString::from("/tmp/loomle-installer"),
        ])
        .expect("cli");

        match cli.command {
            CommandKind::Install { installer_path, .. } => {
                assert_eq!(installer_path, Some(PathBuf::from("/tmp/loomle-installer")));
            }
            _ => panic!("expected install"),
        }
    }

    #[test]
    fn cli_parses_update_command_defaults_to_check() {
        let cli =
            Cli::parse(vec![OsString::from("loomle"), OsString::from("update")]).expect("cli");

        match cli.command {
            CommandKind::Update {
                apply,
                version,
                manifest_path,
                manifest_url,
                installer_path,
            } => {
                assert!(!apply);
                assert!(version.is_none());
                assert!(manifest_path.is_none());
                assert!(manifest_url.is_none());
                assert!(installer_path.is_none());
            }
            _ => panic!("expected update"),
        }
    }

    #[test]
    fn cli_parses_update_command_with_installer_path() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("update"),
            OsString::from("--apply"),
            OsString::from("--installer-path"),
            OsString::from("/tmp/loomle-installer"),
        ])
        .expect("cli");

        match cli.command {
            CommandKind::Update {
                apply,
                installer_path,
                ..
            } => {
                assert!(apply);
                assert_eq!(installer_path, Some(PathBuf::from("/tmp/loomle-installer")));
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
    fn cli_parses_skill_list_installed() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("skill"),
            OsString::from("list"),
            OsString::from("--installed"),
            OsString::from("--skills-root"),
            OsString::from("/tmp/skills"),
        ])
        .expect("cli");

        match cli.command {
            CommandKind::Skill(SkillCommand::List {
                installed_only,
                skills_root,
                registry_url,
            }) => {
                assert!(installed_only);
                assert_eq!(skills_root, Some(PathBuf::from("/tmp/skills")));
                assert!(registry_url.is_none());
            }
            _ => panic!("expected skill list"),
        }
    }

    #[test]
    fn cli_parses_skill_install_command() {
        let cli = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("skill"),
            OsString::from("install"),
            OsString::from("material-weaver"),
            OsString::from("--registry-url"),
            OsString::from("https://example.com/index.json"),
        ])
        .expect("cli");

        match cli.command {
            CommandKind::Skill(SkillCommand::Install {
                skill_name,
                skills_root,
                registry_url,
            }) => {
                assert_eq!(skill_name, "material-weaver");
                assert!(skills_root.is_none());
                assert_eq!(
                    registry_url.as_deref(),
                    Some("https://example.com/index.json")
                );
            }
            _ => panic!("expected skill install"),
        }
    }

    #[test]
    fn cli_rejects_skill_list_with_extra_argument() {
        let error = Cli::parse(vec![
            OsString::from("loomle"),
            OsString::from("skill"),
            OsString::from("list"),
            OsString::from("material-weaver"),
        ])
        .expect_err("expected parse failure");

        assert!(error.contains("skill list"));
    }

    #[test]
    fn platform_and_binary_names_match_supported_layout() {
        assert!(matches!(
            loomle::platform_key(),
            "darwin" | "linux" | "windows"
        ));
        if cfg!(target_os = "windows") {
            assert_eq!(loomle::server_binary_name(), "loomle_mcp_server.exe");
            assert_eq!(loomle::project_client_binary_name(), "loomle.exe");
            assert_eq!(loomle::installer_binary_name(), "loomle-installer.exe");
        } else {
            assert_eq!(loomle::server_binary_name(), "loomle_mcp_server");
            assert_eq!(loomle::project_client_binary_name(), "loomle");
            assert_eq!(loomle::installer_binary_name(), "loomle-installer");
        }
        assert!(loomle::is_installer_binary_path(std::path::Path::new(
            loomle::installer_binary_name()
        )));
        assert!(!loomle::is_installer_binary_path(std::path::Path::new(
            loomle::project_client_binary_name()
        )));
    }

    #[test]
    fn execute_timeout_ms_is_read_from_execute_arguments() {
        let arguments = serde_json::Map::from_iter([
            (String::from("code"), json!("print('hello')")),
            (String::from("timeoutMs"), json!(45_000_u64)),
        ]);

        assert_eq!(
            execute_timeout_ms_from_arguments("execute", &arguments),
            Some(45_000)
        );
    }

    #[test]
    fn execute_timeout_ms_is_ignored_for_other_tools() {
        let arguments =
            serde_json::Map::from_iter([(String::from("timeoutMs"), json!(45_000_u64))]);

        assert_eq!(
            execute_timeout_ms_from_arguments("graph.query", &arguments),
            None
        );
    }

    #[test]
    fn job_wait_ms_is_read_from_execution_arguments() {
        let arguments = serde_json::Map::from_iter([(
            String::from("execution"),
            json!({
                "mode": "job",
                "idempotencyKey": "job-1",
                "waitMs": 2_500_u64
            }),
        )]);

        assert_eq!(job_wait_ms_from_arguments(&arguments), Some(2_500));
    }
}
