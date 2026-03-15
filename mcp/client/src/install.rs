use crate::{platform_key, validate_project_root};
use reqwest::blocking::Client;
use serde::Deserialize;
use serde_json::json;
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use tempfile::TempDir;
use zip::ZipArchive;

#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;

const DEFAULT_RELEASE_REPO: &str = "loomle/loomle";
const EDITOR_PERF_SECTION: &str = "[/Script/UnrealEd.EditorPerformanceSettings]";
const EDITOR_THROTTLE_SETTING: &str = "bThrottleCPUWhenNotForeground=False";
const WORKSPACE_SOURCE_ROOT: &str = "workspace/Loomle";
const PLUGIN_SOURCE_ROOT: &str = "plugin/LoomleBridge";

#[derive(Debug, Clone)]
pub struct InstallRequest {
    pub project_root: PathBuf,
    pub version: Option<String>,
    pub manifest_path: Option<PathBuf>,
    pub manifest_url: Option<String>,
    pub plugin_mode: PluginInstallMode,
}

#[derive(Debug, Clone)]
pub struct UpdateRequest {
    pub project_root: PathBuf,
    pub version: Option<String>,
    pub manifest_path: Option<PathBuf>,
    pub manifest_url: Option<String>,
    pub plugin_mode: Option<PluginInstallMode>,
    pub apply: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PluginInstallMode {
    Prebuilt,
    Source,
}

impl PluginInstallMode {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Prebuilt => "prebuilt",
            Self::Source => "source",
        }
    }

    pub fn parse(raw: &str) -> Result<Self, String> {
        match raw {
            "prebuilt" => Ok(Self::Prebuilt),
            "source" => Ok(Self::Source),
            other => Err(format!(
                "unsupported plugin mode `{other}` (expected prebuilt or source)"
            )),
        }
    }
}

#[derive(Debug, Deserialize)]
struct ReleaseManifest {
    latest: String,
    versions: HashMap<String, ReleaseVersion>,
}

#[derive(Debug, Deserialize)]
struct ReleaseVersion {
    packages: HashMap<String, ReleasePackage>,
}

#[derive(Debug, Deserialize)]
struct ReleasePackage {
    url: String,
    #[serde(default)]
    sha256: String,
    #[serde(default)]
    format: String,
    server_binary_relpath: String,
    client_binary_relpath: String,
    install: ReleaseInstall,
}

#[derive(Debug, Deserialize)]
struct ReleaseInstall {
    plugin: InstallCopySpec,
    workspace: InstallCopySpec,
}

#[derive(Debug, Deserialize)]
struct InstallCopySpec {
    source: String,
    destination: String,
}

#[derive(Debug, Deserialize)]
struct RuntimeInstallState {
    #[serde(rename = "installedVersion")]
    installed_version: String,
    #[serde(rename = "pluginMode")]
    plugin_mode: Option<String>,
}

pub fn install_release(request: InstallRequest) -> Result<serde_json::Value, String> {
    let project_root = validate_project_root(&request.project_root)?;
    let temp_dir = TempDir::new().map_err(|error| format!("failed to create temp dir: {error}"))?;
    let manifest = load_manifest(&request)?;
    let version = resolve_version(&manifest, request.version.as_deref())?;
    let package = manifest
        .versions
        .get(&version)
        .and_then(|entry| entry.packages.get(platform_key()))
        .ok_or_else(|| {
            format!(
                "manifest missing platform package: version={version} platform={}",
                platform_key()
            )
        })?;

    if package.format != "zip" {
        return Err(format!(
            "unsupported package format for version={version} platform={}: {}",
            platform_key(),
            package.format
        ));
    }

    let archive_path = temp_dir.path().join("loomle-release.zip");
    fetch_release_archive(package, &archive_path)?;
    verify_archive_sha(package, &archive_path)?;

    let bundle_root = temp_dir.path().join("bundle");
    extract_zip(&archive_path, &bundle_root)?;
    validate_bundle_paths(&bundle_root, package)?;
    let plugin_mode = request.plugin_mode;
    let install_state_path =
        copy_release_tree(&bundle_root, &project_root, &version, package, plugin_mode)?;

    Ok(json!({
        "installedVersion": version,
        "platform": platform_key(),
        "pluginMode": plugin_mode.as_str(),
        "bundleRoot": bundle_root.display().to_string(),
        "projectRoot": project_root.display().to_string(),
        "plugin": {
            "source": (bundle_root.join(&package.install.plugin.source)).display().to_string(),
            "destination": (project_root.join(&package.install.plugin.destination)).display().to_string(),
        },
        "workspace": {
            "source": (bundle_root.join(&package.install.workspace.source)).display().to_string(),
            "destination": (project_root.join(&package.install.workspace.destination)).display().to_string(),
        },
        "runtime": {
            "installState": install_state_path.display().to_string(),
        }
    }))
}

pub fn update_release(request: UpdateRequest) -> Result<serde_json::Value, String> {
    let project_root = validate_project_root(&request.project_root)?;
    let install_state = read_runtime_install_state(&project_root)?;
    let current_plugin_mode = install_state.plugin_mode();
    let target_plugin_mode = request.plugin_mode.unwrap_or(current_plugin_mode);
    let manifest_request = InstallRequest {
        project_root: project_root.clone(),
        version: request.version.clone(),
        manifest_path: request.manifest_path.clone(),
        manifest_url: request.manifest_url.clone(),
        plugin_mode: target_plugin_mode,
    };
    let manifest = load_manifest(&manifest_request)?;
    let latest_version = resolve_version(&manifest, None)?;
    let target_version = resolve_version(&manifest, request.version.as_deref())?;
    let version_changed = install_state.installed_version != target_version;
    let plugin_mode_changed = current_plugin_mode != target_plugin_mode;
    let would_change = version_changed || plugin_mode_changed;
    let update_available = install_state.installed_version != latest_version;

    if !request.apply {
        return Ok(json!({
            "installedVersion": install_state.installed_version,
            "latestVersion": latest_version,
            "targetVersion": target_version,
            "currentPluginMode": current_plugin_mode.as_str(),
            "targetPluginMode": target_plugin_mode.as_str(),
            "updateAvailable": update_available,
            "wouldChange": would_change,
            "recommendedCommand": if would_change {
                Some(recommended_update_command(version_changed, plugin_mode_changed, &target_version, target_plugin_mode))
            } else {
                None
            },
        }));
    }

    if !would_change {
        return Ok(json!({
            "installedVersion": install_state.installed_version,
            "latestVersion": latest_version,
            "targetVersion": target_version,
            "pluginMode": current_plugin_mode.as_str(),
            "updated": false,
            "reason": "already_up_to_date",
        }));
    }

    let install_result = install_release(InstallRequest {
        project_root,
        version: Some(target_version.clone()),
        manifest_path: request.manifest_path,
        manifest_url: request.manifest_url,
        plugin_mode: target_plugin_mode,
    })?;

    Ok(json!({
        "installedVersion": install_state.installed_version,
        "latestVersion": latest_version,
        "targetVersion": target_version,
        "previousPluginMode": current_plugin_mode.as_str(),
        "pluginMode": target_plugin_mode.as_str(),
        "updated": true,
        "install": install_result,
    }))
}

impl RuntimeInstallState {
    fn plugin_mode(&self) -> PluginInstallMode {
        self.plugin_mode
            .as_deref()
            .and_then(|raw| PluginInstallMode::parse(raw).ok())
            .unwrap_or(PluginInstallMode::Prebuilt)
    }
}

fn recommended_update_command(
    version_changed: bool,
    plugin_mode_changed: bool,
    target_version: &str,
    target_plugin_mode: PluginInstallMode,
) -> String {
    let mut command = String::from("loomle update --apply");
    if version_changed {
        command.push_str(&format!(" --version {target_version}"));
    }
    if plugin_mode_changed {
        command.push_str(&format!(" --plugin-mode {}", target_plugin_mode.as_str()));
    }
    command
}

fn read_runtime_install_state(project_root: &Path) -> Result<RuntimeInstallState, String> {
    let path = project_root.join("Loomle").join("runtime").join("install.json");
    let content = fs::read_to_string(&path)
        .map_err(|error| format!("failed to read install state {}: {error}", path.display()))?;
    let state: RuntimeInstallState = serde_json::from_str(&content)
        .map_err(|error| format!("invalid install state JSON {}: {error}", path.display()))?;
    if state.installed_version.trim().is_empty() {
        return Err(format!(
            "install state missing installedVersion: {}",
            path.display()
        ));
    }
    Ok(state)
}

fn load_manifest(request: &InstallRequest) -> Result<ReleaseManifest, String> {
    if let Some(path) = &request.manifest_path {
        let manifest_json = fs::read_to_string(path)
            .map_err(|error| format!("failed to read manifest {}: {error}", path.display()))?;
        return serde_json::from_str(&manifest_json)
            .map_err(|error| format!("invalid manifest JSON {}: {error}", path.display()));
    }

    let manifest_url = request
        .manifest_url
        .clone()
        .unwrap_or_else(|| default_manifest_url(request.version.as_deref()));
    let body = Client::new()
        .get(&manifest_url)
        .send()
        .and_then(|response| response.error_for_status())
        .map_err(|error| format!("failed to download manifest {manifest_url}: {error}"))?
        .text()
        .map_err(|error| format!("failed to read manifest {manifest_url}: {error}"))?;

    serde_json::from_str(&body)
        .map_err(|error| format!("invalid manifest JSON {manifest_url}: {error}"))
}

fn default_manifest_url(requested_version: Option<&str>) -> String {
    if let Ok(explicit_url) = std::env::var("LOOMLE_RELEASE_MANIFEST_URL") {
        if !explicit_url.trim().is_empty() {
            return explicit_url;
        }
    }

    let release_repo =
        std::env::var("LOOMLE_RELEASE_REPO").unwrap_or_else(|_| String::from(DEFAULT_RELEASE_REPO));
    let release_tag = match requested_version.map(str::trim).filter(|value| !value.is_empty()) {
        Some(version) if version.starts_with('v') => version.to_owned(),
        Some(version) => format!("v{version}"),
        None => String::from("loomle-latest"),
    };

    format!(
        "https://github.com/{release_repo}/releases/download/{release_tag}/loomle-manifest.json"
    )
}

fn resolve_version(
    manifest: &ReleaseManifest,
    requested_version: Option<&str>,
) -> Result<String, String> {
    let version = requested_version
        .filter(|value| !value.trim().is_empty())
        .map(str::to_owned)
        .unwrap_or_else(|| manifest.latest.clone());
    if version.is_empty() {
        return Err(String::from(
            "manifest latest version is missing and --version was not supplied",
        ));
    }
    if !manifest.versions.contains_key(&version) {
        return Err(format!("manifest missing version entry: {version}"));
    }
    Ok(version)
}

fn fetch_release_archive(package: &ReleasePackage, archive_path: &Path) -> Result<(), String> {
    if let Some(file_path) = file_path_from_uri(&package.url)? {
        fs::copy(&file_path, archive_path).map_err(|error| {
            format!(
                "failed to copy local package {} to {}: {error}",
                file_path.display(),
                archive_path.display()
            )
        })?;
        return Ok(());
    }

    let mut response = Client::new()
        .get(&package.url)
        .send()
        .and_then(|response| response.error_for_status())
        .map_err(|error| format!("failed to download package {}: {error}", package.url))?;

    let mut output = fs::File::create(archive_path).map_err(|error| {
        format!(
            "failed to create archive {}: {error}",
            archive_path.display()
        )
    })?;
    response.copy_to(&mut output).map_err(|error| {
        format!(
            "failed to write archive {}: {error}",
            archive_path.display()
        )
    })?;
    output.flush().map_err(|error| {
        format!(
            "failed to flush archive {}: {error}",
            archive_path.display()
        )
    })
}

fn file_path_from_uri(raw: &str) -> Result<Option<PathBuf>, String> {
    if raw.starts_with("file://") {
        let url =
            reqwest::Url::parse(raw).map_err(|error| format!("invalid file URL {raw}: {error}"))?;
        return url
            .to_file_path()
            .map(Some)
            .map_err(|_| format!("file URL is not a valid local path: {raw}"));
    }

    let candidate = PathBuf::from(raw);
    if candidate.exists() {
        return Ok(Some(candidate));
    }

    Ok(None)
}

fn verify_archive_sha(package: &ReleasePackage, archive_path: &Path) -> Result<(), String> {
    if package.sha256.trim().is_empty() {
        return Ok(());
    }

    let actual = sha256_file(archive_path)?;
    let expected = package.sha256.trim().to_ascii_lowercase();
    if actual != expected {
        return Err(format!(
            "package sha256 mismatch for {}: expected={} actual={}",
            archive_path.display(),
            expected,
            actual
        ));
    }
    Ok(())
}

fn sha256_file(path: &Path) -> Result<String, String> {
    let mut handle = fs::File::open(path)
        .map_err(|error| format!("failed to open {} for sha256: {error}", path.display()))?;
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 1024 * 1024];
    loop {
        let bytes_read = handle
            .read(&mut buffer)
            .map_err(|error| format!("failed to read {} for sha256: {error}", path.display()))?;
        if bytes_read == 0 {
            break;
        }
        hasher.update(&buffer[..bytes_read]);
    }
    Ok(hex::encode(hasher.finalize()))
}

fn extract_zip(archive_path: &Path, bundle_root: &Path) -> Result<(), String> {
    let file = fs::File::open(archive_path)
        .map_err(|error| format!("failed to open archive {}: {error}", archive_path.display()))?;
    let mut archive = ZipArchive::new(file).map_err(|error| {
        format!(
            "failed to read zip archive {}: {error}",
            archive_path.display()
        )
    })?;

    fs::create_dir_all(bundle_root).map_err(|error| {
        format!(
            "failed to create bundle dir {}: {error}",
            bundle_root.display()
        )
    })?;

    for index in 0..archive.len() {
        let mut entry = archive
            .by_index(index)
            .map_err(|error| format!("failed to read zip entry #{index}: {error}"))?;
        let Some(enclosed_name) = entry.enclosed_name() else {
            return Err(format!("zip entry contains unsafe path: {}", entry.name()));
        };
        let destination = bundle_root.join(enclosed_name);
        if entry.name().ends_with('/') {
            fs::create_dir_all(&destination).map_err(|error| {
                format!(
                    "failed to create extracted dir {}: {error}",
                    destination.display()
                )
            })?;
            continue;
        }

        if let Some(parent) = destination.parent() {
            fs::create_dir_all(parent).map_err(|error| {
                format!(
                    "failed to create extracted parent dir {}: {error}",
                    parent.display()
                )
            })?;
        }
        let mut output = fs::File::create(&destination).map_err(|error| {
            format!(
                "failed to create extracted file {}: {error}",
                destination.display()
            )
        })?;
        std::io::copy(&mut entry, &mut output)
            .map_err(|error| format!("failed to extract {}: {error}", destination.display()))?;
    }

    Ok(())
}

fn validate_bundle_paths(bundle_root: &Path, package: &ReleasePackage) -> Result<(), String> {
    let plugin_source = bundle_root.join(&package.install.plugin.source);
    let workspace_source = bundle_root.join(&package.install.workspace.source);
    let server_binary = bundle_root.join(&package.server_binary_relpath);
    let client_binary = bundle_root.join(&package.client_binary_relpath);

    if !plugin_source.is_dir() {
        return Err(format!(
            "install source not found: {}",
            plugin_source.display()
        ));
    }
    if !workspace_source.is_dir() {
        return Err(format!(
            "install source not found: {}",
            workspace_source.display()
        ));
    }
    if !server_binary.is_file() {
        return Err(format!(
            "server binary not found: {}",
            server_binary.display()
        ));
    }
    if !client_binary.is_file() {
        return Err(format!(
            "client binary not found: {}",
            client_binary.display()
        ));
    }
    if !package.install.plugin.destination.starts_with("Plugins/") {
        return Err(format!(
            "plugin destination must stay under Plugins/: {}",
            package.install.plugin.destination
        ));
    }
    if package.install.workspace.destination != "Loomle" {
        return Err(format!(
            "workspace destination must be Loomle: {}",
            package.install.workspace.destination
        ));
    }

    Ok(())
}

fn copy_release_tree(
    bundle_root: &Path,
    project_root: &Path,
    version: &str,
    package: &ReleasePackage,
    plugin_mode: PluginInstallMode,
) -> Result<PathBuf, String> {
    let plugin_source = bundle_root.join(&package.install.plugin.source);
    let plugin_destination = project_root.join(&package.install.plugin.destination);
    let workspace_source = bundle_root.join(&package.install.workspace.source);
    let workspace_destination = project_root.join(&package.install.workspace.destination);

    copy_tree(&plugin_source, &plugin_destination)?;
    copy_tree(&workspace_source, &workspace_destination)?;
    strip_plugin_source_for_precompiled_install(&plugin_destination, plugin_mode)?;
    ensure_installed_binaries_executable(project_root, package)?;
    ensure_editor_performance_setting(project_root)?;
    write_runtime_install_state(project_root, version, package, plugin_mode)
}

fn strip_plugin_source_for_precompiled_install(
    plugin_root: &Path,
    plugin_mode: PluginInstallMode,
) -> Result<(), String> {
    if plugin_mode != PluginInstallMode::Prebuilt {
        return Ok(());
    }
    let source_dir = plugin_root.join("Source");
    if !source_dir.is_dir() {
        return Ok(());
    }

    let binary_platform_dir = match plugin_binary_platform_dir() {
        Some(dir) => plugin_root.join("Binaries").join(dir),
        None => return Ok(()),
    };
    if !binary_platform_dir.is_dir() {
        return Ok(());
    }

    fs::remove_dir_all(&source_dir).map_err(|error| {
        format!(
            "failed to remove plugin source for precompiled install {}: {error}",
            source_dir.display()
        )
    })
}

fn plugin_binary_platform_dir() -> Option<&'static str> {
    match platform_key() {
        "darwin" => Some("Mac"),
        "linux" => Some("Linux"),
        "windows" => Some("Win64"),
        _ => None,
    }
}

fn ensure_editor_performance_setting(project_root: &Path) -> Result<(), String> {
    let config_dir = project_root.join("Config");
    fs::create_dir_all(&config_dir).map_err(|error| {
        format!(
            "failed to create project config directory {}: {error}",
            config_dir.display()
        )
    })?;

    let settings_path = config_dir.join("DefaultEditorSettings.ini");
    let existing = match fs::read_to_string(&settings_path) {
        Ok(content) => content,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => String::new(),
        Err(error) => {
            return Err(format!(
                "failed to read editor settings {}: {error}",
                settings_path.display()
            ))
        }
    };

    let updated = ensure_ini_section_setting(
        &existing,
        EDITOR_PERF_SECTION,
        EDITOR_THROTTLE_SETTING,
    );
    fs::write(&settings_path, updated).map_err(|error| {
        format!(
            "failed to write editor settings {}: {error}",
            settings_path.display()
        )
    })
}

fn ensure_ini_section_setting(content: &str, section: &str, setting: &str) -> String {
    let mut in_section = false;
    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            in_section = trimmed.eq_ignore_ascii_case(section);
            continue;
        }
        if in_section && trimmed.eq_ignore_ascii_case(setting) {
            return content.to_string();
        }
    }

    let mut updated = content.trim_end_matches(['\r', '\n']).to_string();
    if !updated.is_empty() {
        updated.push_str("\n\n");
    }
    updated.push_str(section);
    updated.push('\n');
    updated.push_str(setting);
    updated.push('\n');
    updated
}

fn ensure_installed_binaries_executable(
    project_root: &Path,
    package: &ReleasePackage,
) -> Result<(), String> {
    let client_path = installed_destination_path(
        project_root,
        WORKSPACE_SOURCE_ROOT,
        &package.install.workspace.destination,
        &package.client_binary_relpath,
    )?;
    let server_path = installed_destination_path(
        project_root,
        PLUGIN_SOURCE_ROOT,
        &package.install.plugin.destination,
        &package.server_binary_relpath,
    )?;
    ensure_executable_file(&client_path)?;
    ensure_executable_file(&server_path)
}

#[cfg(unix)]
fn ensure_executable_file(path: &Path) -> Result<(), String> {
    let metadata = fs::metadata(path)
        .map_err(|error| format!("failed to read metadata {}: {error}", path.display()))?;
    let mut permissions = metadata.permissions();
    permissions.set_mode(0o755);
    fs::set_permissions(path, permissions)
        .map_err(|error| format!("failed to mark {} executable: {error}", path.display()))
}

#[cfg(not(unix))]
fn ensure_executable_file(_path: &Path) -> Result<(), String> {
    Ok(())
}

fn write_runtime_install_state(
    project_root: &Path,
    version: &str,
    package: &ReleasePackage,
    plugin_mode: PluginInstallMode,
) -> Result<PathBuf, String> {
    let workspace_root = project_root.join(&package.install.workspace.destination);
    let runtime_dir = workspace_root.join("runtime");
    fs::create_dir_all(&runtime_dir).map_err(|error| {
        format!(
            "failed to create workspace runtime directory {}: {error}",
            runtime_dir.display()
        )
    })?;

    let install_state_path = runtime_dir.join("install.json");
    let client_path = installed_destination_path(
        project_root,
        WORKSPACE_SOURCE_ROOT,
        &package.install.workspace.destination,
        &package.client_binary_relpath,
    )?;
    let server_path = installed_destination_path(
        project_root,
        PLUGIN_SOURCE_ROOT,
        &package.install.plugin.destination,
        &package.server_binary_relpath,
    )?;
    let settings_path = project_root.join("Config/DefaultEditorSettings.ini");

    let install_state = json!({
        "schemaVersion": 1,
        "installedVersion": version,
        "platform": platform_key(),
        "pluginMode": plugin_mode.as_str(),
        "projectRoot": project_root.display().to_string(),
        "workspaceRoot": workspace_root.display().to_string(),
        "pluginRoot": project_root.join(&package.install.plugin.destination).display().to_string(),
        "clientPath": client_path.display().to_string(),
        "serverPath": server_path.display().to_string(),
        "editorPerformance": {
            "settingsFile": settings_path.display().to_string(),
            "throttleWhenNotForeground": false,
        }
    });

    let rendered = serde_json::to_string_pretty(&install_state)
        .map_err(|error| format!("failed to encode install state JSON: {error}"))?;
    fs::write(&install_state_path, format!("{rendered}\n")).map_err(|error| {
        format!(
            "failed to write install state {}: {error}",
            install_state_path.display()
        )
    })?;
    Ok(install_state_path)
}

fn installed_destination_path(
    project_root: &Path,
    source_root: &str,
    destination_root: &str,
    bundle_relative_path: &str,
) -> Result<PathBuf, String> {
    let relative_path = Path::new(bundle_relative_path)
        .strip_prefix(source_root)
        .map_err(|_| {
            format!(
                "bundle path {} does not live under expected source root {}",
                bundle_relative_path, source_root
            )
        })?;
    Ok(project_root.join(destination_root).join(relative_path))
}

fn copy_tree(source: &Path, destination: &Path) -> Result<(), String> {
    if destination.exists() {
        fs::remove_dir_all(destination).map_err(|error| {
            format!(
                "failed to remove existing install destination {}: {error}",
                destination.display()
            )
        })?;
    }

    fs::create_dir_all(destination).map_err(|error| {
        format!(
            "failed to create install destination {}: {error}",
            destination.display()
        )
    })?;

    copy_tree_contents(source, destination)
}

fn copy_tree_contents(source: &Path, destination: &Path) -> Result<(), String> {
    for entry in fs::read_dir(source)
        .map_err(|error| format!("failed to read directory {}: {error}", source.display()))?
    {
        let entry = entry.map_err(|error| format!("failed to read dir entry: {error}"))?;
        let source_path = entry.path();
        let destination_path = destination.join(entry.file_name());
        let metadata = entry.metadata().map_err(|error| {
            format!("failed to read metadata {}: {error}", source_path.display())
        })?;
        if metadata.is_dir() {
            fs::create_dir_all(&destination_path).map_err(|error| {
                format!(
                    "failed to create install directory {}: {error}",
                    destination_path.display()
                )
            })?;
            copy_tree_contents(&source_path, &destination_path)?;
            continue;
        }

        fs::copy(&source_path, &destination_path).map_err(|error| {
            format!(
                "failed to copy {} to {}: {error}",
                source_path.display(),
                destination_path.display()
            )
        })?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use zip::write::SimpleFileOptions;

    #[test]
    fn install_release_from_local_manifest_copies_plugin_and_workspace() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        let server_name = if platform_key() == "windows" {
            "loomle_mcp_server.exe"
        } else {
            "loomle_mcp_server"
        };
        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let bundle_root = build_root.join("bundle");
        fs::create_dir_all(
            bundle_root.join(format!("plugin/LoomleBridge/Tools/mcp/{}", platform_key())),
        )
        .expect("plugin");
        fs::create_dir_all(bundle_root.join("workspace/Loomle/client")).expect("workspace");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/LoomleBridge.uplugin"),
            "{}",
        )
        .expect("uplugin");
        fs::write(
            bundle_root.join(format!(
                "plugin/LoomleBridge/Tools/mcp/{}/{}",
                platform_key(),
                server_name
            )),
            "server",
        )
        .expect("server");
        fs::write(
            bundle_root.join(format!("workspace/Loomle/client/{}", client_name)),
            "client",
        )
        .expect("client");
        fs::write(bundle_root.join("workspace/Loomle/README.md"), "workspace").expect("readme");

        let archive_path = build_root.join("loomle.zip");
        write_zip(&bundle_root, &archive_path);
        let archive_sha = sha256_file(&archive_path).expect("sha");
        let manifest_path = build_root.join("manifest.json");
        let manifest = json!({
            "latest": "0.1.0",
            "versions": {
                "0.1.0": {
                    "packages": {
                        platform_key(): {
                            "url": archive_path.to_string_lossy(),
                            "sha256": archive_sha,
                            "format": "zip",
                            "server_binary_relpath": format!(
                                "plugin/LoomleBridge/Tools/mcp/{}/{server_name}",
                                platform_key()
                            ),
                            "client_binary_relpath": format!(
                                "workspace/Loomle/client/{client_name}"
                            ),
                            "install": {
                                "plugin": {
                                    "source": "plugin/LoomleBridge",
                                    "destination": "Plugins/LoomleBridge"
                                },
                                "workspace": {
                                    "source": "workspace/Loomle",
                                    "destination": "Loomle"
                                }
                            }
                        }
                    }
                }
            }
        });
        fs::write(
            &manifest_path,
            serde_json::to_string_pretty(&manifest).expect("serialize manifest"),
        )
        .expect("manifest");

        let result = install_release(InstallRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            plugin_mode: PluginInstallMode::Prebuilt,
        })
        .expect("install");

        assert_eq!(result["installedVersion"], "0.1.0");
        assert!(project_root
            .join("Plugins/LoomleBridge/LoomleBridge.uplugin")
            .is_file());
        assert!(project_root.join("Loomle/client").is_dir());
        let install_state = fs::read_to_string(project_root.join("Loomle/runtime/install.json"))
            .expect("install state");
        let install_state_json: serde_json::Value =
            serde_json::from_str(&install_state).expect("install state json");
        assert_eq!(install_state_json["installedVersion"], "0.1.0");
        assert_eq!(install_state_json["platform"], platform_key());
        assert_eq!(install_state_json["pluginMode"], "prebuilt");
        #[cfg(unix)]
        {
            let client_mode = fs::metadata(project_root.join("Loomle/client").join(client_name))
                .expect("client metadata")
                .permissions()
                .mode();
            let server_mode = fs::metadata(
                project_root
                    .join("Plugins/LoomleBridge/Tools/mcp")
                    .join(platform_key())
                    .join(server_name),
            )
            .expect("server metadata")
            .permissions()
            .mode();
            assert_ne!(client_mode & 0o111, 0, "client should be executable");
            assert_ne!(server_mode & 0o111, 0, "server should be executable");
        }
        let editor_settings = fs::read_to_string(
            project_root.join("Config/DefaultEditorSettings.ini"),
        )
        .expect("editor settings");
        assert!(editor_settings.contains(EDITOR_PERF_SECTION));
        assert!(editor_settings.contains(EDITOR_THROTTLE_SETTING));
    }

    #[test]
    fn default_manifest_url_uses_release_base_env() {
        std::env::set_var("LOOMLE_RELEASE_REPO", "example/test");
        assert_eq!(
            default_manifest_url(None),
            "https://github.com/example/test/releases/download/loomle-latest/loomle-manifest.json"
        );
        assert_eq!(
            default_manifest_url(Some("0.1.0")),
            "https://github.com/example/test/releases/download/v0.1.0/loomle-manifest.json"
        );
        std::env::remove_var("LOOMLE_RELEASE_REPO");
    }

    #[test]
    fn install_release_prefers_precompiled_plugin_layout() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        let server_name = if platform_key() == "windows" {
            "loomle_mcp_server.exe"
        } else {
            "loomle_mcp_server"
        };
        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };
        let plugin_binary_platform_dir =
            plugin_binary_platform_dir().expect("supported plugin binary platform");
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let bundle_root = build_root.join("bundle");
        fs::create_dir_all(
            bundle_root.join(format!("plugin/LoomleBridge/Tools/mcp/{}", platform_key())),
        )
        .expect("plugin server dir");
        fs::create_dir_all(
            bundle_root.join(format!("plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}")),
        )
        .expect("plugin binaries");
        fs::create_dir_all(bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge"))
            .expect("plugin source");
        fs::create_dir_all(bundle_root.join("workspace/Loomle/client")).expect("workspace");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/LoomleBridge.uplugin"),
            "{}",
        )
        .expect("uplugin");
        fs::write(
            bundle_root.join(format!(
                "plugin/LoomleBridge/Tools/mcp/{}/{}",
                platform_key(),
                server_name
            )),
            "server",
        )
        .expect("server");
        fs::write(
            bundle_root.join(format!(
                "plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}/placeholder.bin"
            )),
            "binary",
        )
        .expect("plugin binary");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs"),
            "source",
        )
        .expect("build cs");
        fs::write(
            bundle_root.join(format!("workspace/Loomle/client/{}", client_name)),
            "client",
        )
        .expect("client");
        fs::write(bundle_root.join("workspace/Loomle/README.md"), "workspace").expect("readme");

        let archive_path = build_root.join("loomle.zip");
        write_zip(&bundle_root, &archive_path);
        let archive_sha = sha256_file(&archive_path).expect("sha");
        let manifest_path = build_root.join("manifest.json");
        let manifest = json!({
            "latest": "0.1.0",
            "versions": {
                "0.1.0": {
                    "packages": {
                        platform_key(): {
                            "url": archive_path.to_string_lossy(),
                            "sha256": archive_sha,
                            "format": "zip",
                            "server_binary_relpath": format!(
                                "plugin/LoomleBridge/Tools/mcp/{}/{server_name}",
                                platform_key()
                            ),
                            "client_binary_relpath": format!(
                                "workspace/Loomle/client/{client_name}"
                            ),
                            "install": {
                                "plugin": {
                                    "source": "plugin/LoomleBridge",
                                    "destination": "Plugins/LoomleBridge"
                                },
                                "workspace": {
                                    "source": "workspace/Loomle",
                                    "destination": "Loomle"
                                }
                            }
                        }
                    }
                }
            }
        });
        fs::write(
            &manifest_path,
            serde_json::to_string_pretty(&manifest).expect("serialize manifest"),
        )
        .expect("manifest");

        install_release(InstallRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            plugin_mode: PluginInstallMode::Prebuilt,
        })
        .expect("install");

        assert!(project_root
            .join("Plugins/LoomleBridge/Binaries")
            .join(plugin_binary_platform_dir)
            .is_dir());
        assert!(
            !project_root.join("Plugins/LoomleBridge/Source").exists(),
            "precompiled installs should not keep plugin source"
        );
    }

    #[test]
    fn install_release_source_mode_keeps_plugin_source() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        let server_name = if platform_key() == "windows" {
            "loomle_mcp_server.exe"
        } else {
            "loomle_mcp_server"
        };
        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };
        let plugin_binary_platform_dir =
            plugin_binary_platform_dir().expect("supported plugin binary platform");
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let bundle_root = build_root.join("bundle");
        fs::create_dir_all(
            bundle_root.join(format!("plugin/LoomleBridge/Tools/mcp/{}", platform_key())),
        )
        .expect("plugin server dir");
        fs::create_dir_all(
            bundle_root.join(format!("plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}")),
        )
        .expect("plugin binaries");
        fs::create_dir_all(bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge"))
            .expect("plugin source");
        fs::create_dir_all(bundle_root.join("workspace/Loomle/client")).expect("workspace");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/LoomleBridge.uplugin"),
            "{}",
        )
        .expect("uplugin");
        fs::write(
            bundle_root.join(format!(
                "plugin/LoomleBridge/Tools/mcp/{}/{}",
                platform_key(),
                server_name
            )),
            "server",
        )
        .expect("server");
        fs::write(
            bundle_root.join(format!(
                "plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}/placeholder.bin"
            )),
            "binary",
        )
        .expect("plugin binary");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs"),
            "source",
        )
        .expect("build cs");
        fs::write(
            bundle_root.join(format!("workspace/Loomle/client/{}", client_name)),
            "client",
        )
        .expect("client");
        fs::write(bundle_root.join("workspace/Loomle/README.md"), "workspace").expect("readme");

        let archive_path = build_root.join("loomle.zip");
        write_zip(&bundle_root, &archive_path);
        let archive_sha = sha256_file(&archive_path).expect("sha");
        let manifest_path = build_root.join("manifest.json");
        let manifest = json!({
            "latest": "0.1.0",
            "versions": {
                "0.1.0": {
                    "packages": {
                        platform_key(): {
                            "url": archive_path.to_string_lossy(),
                            "sha256": archive_sha,
                            "format": "zip",
                            "server_binary_relpath": format!(
                                "plugin/LoomleBridge/Tools/mcp/{}/{server_name}",
                                platform_key()
                            ),
                            "client_binary_relpath": format!(
                                "workspace/Loomle/client/{client_name}"
                            ),
                            "install": {
                                "plugin": {
                                    "source": "plugin/LoomleBridge",
                                    "destination": "Plugins/LoomleBridge"
                                },
                                "workspace": {
                                    "source": "workspace/Loomle",
                                    "destination": "Loomle"
                                }
                            }
                        }
                    }
                }
            }
        });
        fs::write(
            &manifest_path,
            serde_json::to_string_pretty(&manifest).expect("serialize manifest"),
        )
        .expect("manifest");

        install_release(InstallRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            plugin_mode: PluginInstallMode::Source,
        })
        .expect("install");

        assert!(project_root
            .join("Plugins/LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs")
            .is_file());
        let install_state = fs::read_to_string(project_root.join("Loomle/runtime/install.json"))
            .expect("install state");
        let install_state_json: serde_json::Value =
            serde_json::from_str(&install_state).expect("install state json");
        assert_eq!(install_state_json["pluginMode"], "source");
    }

    #[test]
    fn update_release_reports_latest_and_inherits_current_plugin_mode() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let release_010 = build_release_archive(&build_root, "0.1.0");
        let release_020 = build_release_archive(&build_root, "0.2.0");
        let manifest_path = write_manifest(
            &build_root,
            "0.2.0",
            &[
                ("0.1.0", &release_010.0, &release_010.1),
                ("0.2.0", &release_020.0, &release_020.1),
            ],
        );

        install_release(InstallRequest {
            project_root: project_root.clone(),
            version: Some(String::from("0.1.0")),
            manifest_path: Some(manifest_path.clone()),
            manifest_url: None,
            plugin_mode: PluginInstallMode::Source,
        })
        .expect("install 0.1.0");

        let result = update_release(UpdateRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            plugin_mode: None,
            apply: false,
        })
        .expect("update check");

        assert_eq!(result["installedVersion"], "0.1.0");
        assert_eq!(result["latestVersion"], "0.2.0");
        assert_eq!(result["targetVersion"], "0.2.0");
        assert_eq!(result["currentPluginMode"], "source");
        assert_eq!(result["targetPluginMode"], "source");
        assert_eq!(result["updateAvailable"], true);
        assert_eq!(result["wouldChange"], true);
    }

    #[test]
    fn update_release_apply_upgrades_and_keeps_existing_plugin_mode() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let release_010 = build_release_archive(&build_root, "0.1.0");
        let release_020 = build_release_archive(&build_root, "0.2.0");
        let manifest_path = write_manifest(
            &build_root,
            "0.2.0",
            &[
                ("0.1.0", &release_010.0, &release_010.1),
                ("0.2.0", &release_020.0, &release_020.1),
            ],
        );

        install_release(InstallRequest {
            project_root: project_root.clone(),
            version: Some(String::from("0.1.0")),
            manifest_path: Some(manifest_path.clone()),
            manifest_url: None,
            plugin_mode: PluginInstallMode::Source,
        })
        .expect("install 0.1.0");

        let result = update_release(UpdateRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            plugin_mode: None,
            apply: true,
        })
        .expect("update apply");

        assert_eq!(result["updated"], true);
        assert_eq!(result["targetVersion"], "0.2.0");
        assert_eq!(result["pluginMode"], "source");

        let install_state = fs::read_to_string(project_root.join("Loomle/runtime/install.json"))
            .expect("install state");
        let install_state_json: serde_json::Value =
            serde_json::from_str(&install_state).expect("install state json");
        assert_eq!(install_state_json["installedVersion"], "0.2.0");
        assert_eq!(install_state_json["pluginMode"], "source");
        assert!(project_root
            .join("Plugins/LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs")
            .is_file());
    }

    fn build_release_archive(build_root: &Path, version: &str) -> (PathBuf, String) {
        let server_name = if platform_key() == "windows" {
            "loomle_mcp_server.exe"
        } else {
            "loomle_mcp_server"
        };
        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };
        let plugin_binary_platform_dir =
            plugin_binary_platform_dir().expect("supported plugin binary platform");
        let bundle_root = build_root.join(format!("bundle-{version}"));

        fs::create_dir_all(
            bundle_root.join(format!("plugin/LoomleBridge/Tools/mcp/{}", platform_key())),
        )
        .expect("plugin server dir");
        fs::create_dir_all(
            bundle_root.join(format!("plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}")),
        )
        .expect("plugin binaries");
        fs::create_dir_all(bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge"))
            .expect("plugin source");
        fs::create_dir_all(bundle_root.join("workspace/Loomle/client")).expect("workspace");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/LoomleBridge.uplugin"),
            "{}",
        )
        .expect("uplugin");
        fs::write(
            bundle_root.join(format!(
                "plugin/LoomleBridge/Tools/mcp/{}/{}",
                platform_key(),
                server_name
            )),
            format!("server-{version}"),
        )
        .expect("server");
        fs::write(
            bundle_root.join(format!(
                "plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}/placeholder.bin"
            )),
            format!("binary-{version}"),
        )
        .expect("plugin binary");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs"),
            format!("source-{version}"),
        )
        .expect("build cs");
        fs::write(
            bundle_root.join(format!("workspace/Loomle/client/{}", client_name)),
            format!("client-{version}"),
        )
        .expect("client");
        fs::write(
            bundle_root.join("workspace/Loomle/README.md"),
            format!("workspace-{version}"),
        )
        .expect("readme");

        let archive_path = build_root.join(format!("loomle-{version}.zip"));
        write_zip(&bundle_root, &archive_path);
        let archive_sha = sha256_file(&archive_path).expect("sha");
        (archive_path, archive_sha)
    }

    fn write_manifest(build_root: &Path, latest: &str, versions: &[(&str, &PathBuf, &String)]) -> PathBuf {
        let server_name = if platform_key() == "windows" {
            "loomle_mcp_server.exe"
        } else {
            "loomle_mcp_server"
        };
        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };
        let mut version_map = serde_json::Map::new();
        for (version, archive_path, archive_sha) in versions {
            version_map.insert(
                (*version).to_string(),
                json!({
                    "packages": {
                        platform_key(): {
                            "url": archive_path.to_string_lossy(),
                            "sha256": archive_sha,
                            "format": "zip",
                            "server_binary_relpath": format!(
                                "plugin/LoomleBridge/Tools/mcp/{}/{server_name}",
                                platform_key()
                            ),
                            "client_binary_relpath": format!(
                                "workspace/Loomle/client/{client_name}"
                            ),
                            "install": {
                                "plugin": {
                                    "source": "plugin/LoomleBridge",
                                    "destination": "Plugins/LoomleBridge"
                                },
                                "workspace": {
                                    "source": "workspace/Loomle",
                                    "destination": "Loomle"
                                }
                            }
                        }
                    }
                }),
            );
        }

        let manifest_path = build_root.join("manifest.json");
        fs::write(
            &manifest_path,
            serde_json::to_string_pretty(&json!({
                "latest": latest,
                "versions": version_map,
            }))
            .expect("serialize manifest"),
        )
        .expect("manifest");
        manifest_path
    }

    fn write_zip(bundle_root: &Path, archive_path: &Path) {
        let file = fs::File::create(archive_path).expect("archive");
        let mut zip = zip::ZipWriter::new(file);
        let options = SimpleFileOptions::default();
        write_zip_dir(&mut zip, bundle_root, bundle_root, options);
        zip.finish().expect("finish zip");
    }

    fn write_zip_dir(
        zip: &mut zip::ZipWriter<fs::File>,
        root: &Path,
        current: &Path,
        options: SimpleFileOptions,
    ) {
        for entry in fs::read_dir(current).expect("read dir") {
            let entry = entry.expect("entry");
            let path = entry.path();
            let relative = path.strip_prefix(root).expect("strip prefix");
            let relative_text = relative.to_string_lossy().replace('\\', "/");
            if path.is_dir() {
                zip.add_directory(format!("{relative_text}/"), options)
                    .expect("add dir");
                write_zip_dir(zip, root, &path, options);
            } else {
                zip.start_file(relative_text, options).expect("start file");
                let bytes = fs::read(&path).expect("read file");
                zip.write_all(&bytes).expect("write file");
            }
        }
    }
}
