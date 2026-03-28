use crate::{installer_binary_name, platform_key, validate_project_root};
use reqwest::blocking::Client;
use serde::Deserialize;
use serde_json::json;
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::thread;
use std::time::Duration;
use tempfile::TempDir;
use zip::ZipArchive;

#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;

const DEFAULT_RELEASE_REPO: &str = "loomle/loomle";
const INSTALLER_PATH_OVERRIDE_ENV: &str = "LOOMLE_INSTALLER_PATH";
const EDITOR_PERF_SECTION: &str = "[/Script/UnrealEd.EditorPerformanceSettings]";
const EDITOR_THROTTLE_SETTING: &str = "bThrottleCPUWhenNotForeground=False";
const WORKSPACE_SOURCE_ROOT: &str = "workspace/Loomle";
const RESTART_REASON_INSTALL: &str =
    "If Unreal Editor is already running, restart it to load the installed LoomleBridge plugin version.";
const RESTART_REASON_UPDATE: &str =
    "If Unreal Editor is already running, restart it to load the updated LoomleBridge plugin version.";
const INVALID_ARCHIVE_PATH_CHARS: [char; 7] = ['<', '>', ':', '"', '|', '?', '*'];
const DOWNLOAD_RETRY_ATTEMPTS: u32 = 4;
const DOWNLOAD_RETRY_BASE_DELAY_MS: u64 = 500;

fn print_install_phase(message: &str) {
    eprintln!("[loomle][INFO] {message}");
}

fn download_retry_delay(attempt: u32) -> Duration {
    let multiplier = 1_u64 << attempt.saturating_sub(1);
    Duration::from_millis(DOWNLOAD_RETRY_BASE_DELAY_MS.saturating_mul(multiplier))
}

fn with_download_retries<T, F>(label: &str, source: &str, mut action: F) -> Result<T, String>
where
    F: FnMut() -> Result<T, String>,
{
    let mut last_error = None::<String>;
    for attempt in 1..=DOWNLOAD_RETRY_ATTEMPTS {
        match action() {
            Ok(value) => return Ok(value),
            Err(error) => {
                last_error = Some(error.clone());
                if attempt >= DOWNLOAD_RETRY_ATTEMPTS {
                    break;
                }
                const MAX_ATTEMPT_LABEL: u32 = DOWNLOAD_RETRY_ATTEMPTS;
                print_install_phase(&format!(
                    "{label} failed for {source} (attempt {attempt}/{MAX_ATTEMPT_LABEL}): {error}; retrying"
                ));
                thread::sleep(download_retry_delay(attempt));
            }
        }
    }

    Err(last_error.unwrap_or_else(|| format!("{label} failed for {source}")))
}

fn local_manifest_repair_hint(project_root: &Path) -> String {
    format!(
        " If needed, repair with `loomle-installer install --project-root {} --manifest-path <manifest.json>` using a locally downloaded manifest/package, or extract the official release zip manually into `Plugins/LoomleBridge` and `Loomle`.",
        project_root.display()
    )
}

fn top_level_zip_component(path: &Path) -> Option<String> {
    path.components()
        .next()
        .map(|component| component.as_os_str().to_string_lossy().to_string())
}

fn normalize_zip_entry_name(entry_name: &str) -> Result<(PathBuf, bool), String> {
    if entry_name.starts_with('/') || entry_name.starts_with('\\') {
        return Err(format!("zip entry contains absolute path: {entry_name}"));
    }

    let is_directory = entry_name.ends_with('/') || entry_name.ends_with('\\');
    let mut normalized = PathBuf::new();

    for component in entry_name.split(['/', '\\']) {
        if component.is_empty() || component == "." {
            continue;
        }
        if component == ".." {
            return Err(format!("zip entry contains unsafe path traversal: {entry_name}"));
        }
        if component.chars().any(|c| c.is_control())
            || component.chars().any(|c| INVALID_ARCHIVE_PATH_CHARS.contains(&c))
        {
            return Err(format!(
                "zip entry contains invalid path component `{component}`: {entry_name}"
            ));
        }
        normalized.push(component);
    }

    if normalized.as_os_str().is_empty() {
        return Err(format!("zip entry has empty normalized path: {entry_name}"));
    }

    Ok((normalized, is_directory))
}

#[derive(Debug, Clone)]
pub struct InstallRequest {
    pub project_root: PathBuf,
    pub version: Option<String>,
    pub manifest_path: Option<PathBuf>,
    pub manifest_url: Option<String>,
}

#[derive(Debug, Clone)]
pub struct UpdateRequest {
    pub project_root: PathBuf,
    pub version: Option<String>,
    pub manifest_path: Option<PathBuf>,
    pub manifest_url: Option<String>,
    pub apply: bool,
}

#[derive(Debug, Clone)]
pub struct InstallerDownloadRequest {
    pub version: Option<String>,
    pub manifest_path: Option<PathBuf>,
    pub manifest_url: Option<String>,
    pub installer_path: Option<PathBuf>,
}

pub struct DownloadedInstaller {
    _temp_dir: TempDir,
    path: PathBuf,
}

impl DownloadedInstaller {
    pub fn path(&self) -> &Path {
        &self.path
    }
}

#[derive(Debug, Deserialize)]
struct ReleaseManifest {
    latest: String,
    #[serde(default)]
    installer: HashMap<String, ReleaseInstallerPackage>,
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
    client_binary_relpath: String,
    install: ReleaseInstall,
}

#[derive(Debug, Deserialize)]
struct ReleaseInstallerPackage {
    url: String,
    #[serde(default)]
    sha256: String,
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
}

pub fn install_release(request: InstallRequest) -> Result<serde_json::Value, String> {
    let project_root = validate_project_root(&request.project_root)?;
    print_install_phase(&format!("resolved project root {}", project_root.display()));
    let temp_dir = TempDir::new().map_err(|error| format!("failed to create temp dir: {error}"))?;
    print_install_phase("loading release manifest");
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

    print_install_phase(&format!(
        "selected version={version} platform={} packageUrl={}",
        platform_key(),
        package.url
    ));

    let archive_path = temp_dir.path().join("loomle-release.zip");
    print_install_phase(&format!(
        "downloading LOOMLE {version} package for {}",
        platform_key()
    ));
    fetch_release_archive(package, &archive_path)
        .map_err(|error| format!("{error}{}", local_manifest_repair_hint(&project_root)))?;

    if !package.sha256.trim().is_empty() {
        print_install_phase("verifying package sha256");
    }
    verify_archive_sha(package, &archive_path).map_err(|error| {
        format!(
            "{error}. The downloaded archive may be incomplete or corrupt.{}",
            local_manifest_repair_hint(&project_root)
        )
    })?;

    let bundle_root = temp_dir.path().join("bundle");
    print_install_phase("extracting release bundle");
    extract_zip(&archive_path, &bundle_root).map_err(|error| {
        format!(
            "{error}. The archive may be invalid or incomplete.{}",
            local_manifest_repair_hint(&project_root)
        )
    })?;
    print_install_phase("validating release bundle");
    validate_bundle_paths(&bundle_root, package).map_err(|error| {
        format!(
            "{error}. The extracted bundle appears incomplete.{}",
            local_manifest_repair_hint(&project_root)
        )
    })?;
    print_install_phase("copying plugin and workspace into the project");
    let install_state_path = copy_release_tree(&bundle_root, &project_root, &version, package)?;
    print_install_phase("verifying installed layout");
    validate_installed_paths(&project_root, package).map_err(|error| {
        format!(
            "{error}. The install may be incomplete.{}",
            local_manifest_repair_hint(&project_root)
        )
    })?;

    Ok(json!({
        "installedVersion": version,
        "platform": platform_key(),
        "restartRequired": true,
        "restartReason": RESTART_REASON_INSTALL,
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
    print_install_phase(&format!("resolved project root {}", project_root.display()));
    let manifest_request = InstallRequest {
        project_root: project_root.clone(),
        version: request.version.clone(),
        manifest_path: request.manifest_path.clone(),
        manifest_url: request.manifest_url.clone(),
    };
    let manifest = load_manifest(&manifest_request)?;
    let latest_version = resolve_version(&manifest, None)?;
    let target_version = resolve_version(&manifest, request.version.as_deref())?;
    let install_state = match read_runtime_install_state(&project_root) {
        Ok(state) => state,
        Err(error) => {
            if request.apply && error.contains("install state not found at ") {
                print_install_phase(
                    "install state missing during update --apply; repairing via install flow",
                );
                let install_result = install_release(InstallRequest {
                    project_root,
                    version: Some(target_version.clone()),
                    manifest_path: request.manifest_path,
                    manifest_url: request.manifest_url,
                })?;
                return Ok(json!({
                    "installedVersion": serde_json::Value::Null,
                    "latestVersion": latest_version,
                    "targetVersion": target_version,
                    "updated": true,
                    "repair": true,
                    "restartRequired": true,
                    "restartReason": RESTART_REASON_UPDATE,
                    "install": install_result,
                }));
            }
            return Err(error);
        }
    };
    let version_changed = install_state.installed_version != target_version;
    let would_change = version_changed;
    let update_available = install_state.installed_version != latest_version;

    print_install_phase(&format!(
        "installedVersion={} latestVersion={} targetVersion={}",
        install_state.installed_version, latest_version, target_version
    ));

    if !request.apply {
        return Ok(json!({
            "installedVersion": install_state.installed_version,
            "latestVersion": latest_version,
            "targetVersion": target_version,
            "updateAvailable": update_available,
            "wouldChange": would_change,
            "recommendedCommand": if would_change {
                Some(recommended_update_command(version_changed, &target_version))
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
            "updated": false,
            "reason": "already_up_to_date",
        }));
    }

    print_install_phase(&format!(
        "applying update {} -> {}",
        install_state.installed_version, target_version
    ));
    let install_result = install_release(InstallRequest {
        project_root,
        version: Some(target_version.clone()),
        manifest_path: request.manifest_path,
        manifest_url: request.manifest_url,
    })?;

    Ok(json!({
        "installedVersion": install_state.installed_version,
        "latestVersion": latest_version,
        "targetVersion": target_version,
        "updated": true,
        "restartRequired": true,
        "restartReason": RESTART_REASON_UPDATE,
        "install": install_result,
    }))
}

fn recommended_update_command(version_changed: bool, target_version: &str) -> String {
    let mut command = String::from("loomle update --apply");
    if version_changed {
        command.push_str(&format!(" --version {target_version}"));
    }
    command
}

fn read_runtime_install_state(project_root: &Path) -> Result<RuntimeInstallState, String> {
    let path = project_root.join("Loomle").join("runtime").join("install.json");
    let content = fs::read_to_string(&path).map_err(|error| {
        if error.kind() == std::io::ErrorKind::NotFound {
            format!(
                "install state not found at {}. This project may need repair; run `loomle-installer install --project-root {}` before using `loomle update`.",
                path.display(),
                project_root.display()
            )
        } else {
            format!("failed to read install state {}: {error}", path.display())
        }
    })?;
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
    load_manifest_from_sources(
        request.manifest_path.as_deref(),
        request.manifest_url.as_deref(),
        request.version.as_deref(),
    )
}

fn load_manifest_from_sources(
    manifest_path: Option<&Path>,
    manifest_url: Option<&str>,
    requested_version: Option<&str>,
) -> Result<ReleaseManifest, String> {
    if let Some(path) = manifest_path {
        print_install_phase(&format!("reading manifest from {}", path.display()));
        let manifest_json = fs::read_to_string(path)
            .map_err(|error| format!("failed to read manifest {}: {error}", path.display()))?;
        return serde_json::from_str(&manifest_json)
            .map_err(|error| format!("invalid manifest JSON {}: {error}", path.display()));
    }

    let manifest_source = manifest_url
        .map(str::to_owned)
        .unwrap_or_else(|| default_manifest_url(requested_version));
    if let Some(path) = file_path_from_uri(&manifest_source)? {
        print_install_phase(&format!("reading manifest from {}", path.display()));
        let manifest_json = fs::read_to_string(&path)
            .map_err(|error| format!("failed to read manifest {}: {error}", path.display()))?;
        return serde_json::from_str(&manifest_json)
            .map_err(|error| format!("invalid manifest JSON {}: {error}", path.display()));
    }

    print_install_phase(&format!("downloading manifest from {manifest_source}"));
    let body = with_download_retries("manifest download", &manifest_source, || {
        Client::new()
            .get(&manifest_source)
            .send()
            .and_then(|response| response.error_for_status())
            .map_err(|error| format!("failed to download manifest {manifest_source}: {error}"))?
            .text()
            .map_err(|error| format!("failed to read manifest {manifest_source}: {error}"))
    })?;

    serde_json::from_str(&body)
        .map_err(|error| format!("invalid manifest JSON {manifest_source}: {error}"))
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

fn release_tag_for_version(requested_version: Option<&str>) -> String {
    match requested_version.map(str::trim).filter(|value| !value.is_empty()) {
        Some(version) if version.starts_with('v') => version.to_owned(),
        Some(version) => format!("v{version}"),
        None => String::from("loomle-latest"),
    }
}

pub fn installer_download_url(requested_version: Option<&str>) -> Result<String, String> {
    let release_repo =
        std::env::var("LOOMLE_RELEASE_REPO").unwrap_or_else(|_| String::from(DEFAULT_RELEASE_REPO));
    match platform_key() {
        "windows" | "darwin" => Ok(format!(
            "https://github.com/{release_repo}/releases/download/{}/{}",
            release_tag_for_version(requested_version),
            installer_binary_name()
        )),
        unsupported => Err(format!(
            "installer bootstrap assets are not published for platform={unsupported}"
        )),
    }
}

pub fn download_temp_installer(request: &InstallerDownloadRequest) -> Result<DownloadedInstaller, String> {
    let temp_dir =
        TempDir::new().map_err(|error| format!("failed to create temp dir for installer: {error}"))?;
    let installer_path = temp_dir.path().join(installer_binary_name());
    if let Ok(override_path) = std::env::var(INSTALLER_PATH_OVERRIDE_ENV) {
        let source_path = PathBuf::from(override_path);
        print_install_phase(&format!(
            "copying temporary installer from {} via {}",
            source_path.display(),
            INSTALLER_PATH_OVERRIDE_ENV
        ));
        fs::copy(&source_path, &installer_path).map_err(|error| {
            format!(
                "failed to copy temporary installer {} to {}: {error}",
                source_path.display(),
                installer_path.display()
            )
        })?;
        ensure_executable_file(&installer_path)?;
        return Ok(DownloadedInstaller {
            _temp_dir: temp_dir,
            path: installer_path,
        });
    }
    if let Some(source_path) = &request.installer_path {
        print_install_phase(&format!(
            "copying temporary installer from explicit path {}",
            source_path.display()
        ));
        fs::copy(source_path, &installer_path).map_err(|error| {
            format!(
                "failed to copy temporary installer {} to {}: {error}",
                source_path.display(),
                installer_path.display()
            )
        })?;
        ensure_executable_file(&installer_path)?;
        return Ok(DownloadedInstaller {
            _temp_dir: temp_dir,
            path: installer_path,
        });
    }
    if request.manifest_path.is_some() || request.manifest_url.is_some() {
        let manifest = load_manifest_from_sources(
            request.manifest_path.as_deref(),
            request.manifest_url.as_deref(),
            request.version.as_deref(),
        )?;
        if let Some(installer) = manifest.installer.get(platform_key()) {
            print_install_phase(&format!(
                "acquiring temporary installer from manifest source {}",
                installer.url
            ));
            copy_or_download_to_path(&installer.url, &installer_path)
                .map_err(|error| format!("failed to acquire temporary installer {}: {error}", installer.url))?;
            verify_expected_sha("temporary installer", &installer.sha256, &installer_path)?;
            ensure_executable_file(&installer_path)?;
            return Ok(DownloadedInstaller {
                _temp_dir: temp_dir,
                path: installer_path,
            });
        }
        print_install_phase(
            "manifest did not declare a platform installer; falling back to published installer asset",
        );
    }
    let download_url = installer_download_url(request.version.as_deref())?;
    print_install_phase(&format!("downloading temporary installer from {download_url}"));
    download_url_to_path(&download_url, &installer_path)
        .map_err(|error| format!("failed to download temporary installer {download_url}: {error}"))?;
    ensure_executable_file(&installer_path)?;
    Ok(DownloadedInstaller {
        _temp_dir: temp_dir,
        path: installer_path,
    })
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
    let downloaded_bytes = copy_or_download_to_path(&package.url, archive_path)
        .map_err(|error| format!("failed to download package {}: {error}", package.url))?;
    print_install_phase(&format!(
        "package download complete: {downloaded_bytes} bytes"
    ));
    Ok(())
}

fn copy_or_download_to_path(source: &str, destination: &Path) -> Result<u64, String> {
    if let Some(file_path) = file_path_from_uri(source)? {
        let file_size = fs::metadata(&file_path)
            .map(|metadata| metadata.len())
            .unwrap_or(0);
        print_install_phase(&format!(
            "copying local file {} ({} bytes)",
            file_path.display(),
            file_size
        ));
        fs::copy(&file_path, destination).map_err(|error| {
            format!(
                "failed to copy local file {} to {}: {error}",
                file_path.display(),
                destination.display()
            )
        })?;
        return Ok(fs::metadata(destination)
            .map(|metadata| metadata.len())
            .unwrap_or(file_size));
    }

    download_url_to_path(source, destination)
}

fn download_url_to_path(url: &str, destination: &Path) -> Result<u64, String> {
    with_download_retries("download", url, || {
        let _ = fs::remove_file(destination);

        let mut response = Client::new()
            .get(url)
            .send()
            .and_then(|response| response.error_for_status())
            .map_err(|error| format!("download failed: {error}"))?;

        let mut output = fs::File::create(destination).map_err(|error| {
            format!(
                "failed to create download target {}: {error}",
                destination.display()
            )
        })?;
        let mut downloaded_bytes: u64 = 0;
        let mut buffer = [0_u8; 1024 * 256];
        loop {
            let bytes_read = response
                .read(&mut buffer)
                .map_err(|error| format!("failed to read response body: {error}"))?;
            if bytes_read == 0 {
                break;
            }
            output.write_all(&buffer[..bytes_read]).map_err(|error| {
                format!(
                    "failed to write download target {}: {error}",
                    destination.display()
                )
            })?;
            downloaded_bytes += bytes_read as u64;
        }
        output.flush().map_err(|error| {
            format!(
                "failed to flush download target {}: {error}",
                destination.display()
            )
        })?;
        Ok(downloaded_bytes)
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
    verify_expected_sha("package", &package.sha256, archive_path)
}

fn verify_expected_sha(label: &str, expected_sha: &str, path: &Path) -> Result<(), String> {
    if expected_sha.trim().is_empty() {
        return Ok(());
    }

    let actual = sha256_file(path)?;
    let expected = expected_sha.trim().to_ascii_lowercase();
    if actual != expected {
        return Err(format!(
            "{label} sha256 mismatch for {}: expected={} actual={}",
            path.display(),
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
            "invalid or incomplete zip archive {}: {error}",
            archive_path.display()
        )
    })?;

    fs::create_dir_all(bundle_root).map_err(|error| {
        format!(
            "failed to create bundle dir {}: {error}",
            bundle_root.display()
        )
    })?;

    let mut last_top_level_component = None::<String>;
    for index in 0..archive.len() {
        let mut entry = archive
            .by_index(index)
            .map_err(|error| {
                format!(
                    "invalid or incomplete zip archive {} while reading entry #{index}: {error}",
                    archive_path.display()
                )
            })?;
        let entry_name = entry.name().to_string();
        let (normalized_name, is_directory_entry) = normalize_zip_entry_name(&entry_name)?;
        if let Some(top_level_component) = top_level_zip_component(&normalized_name) {
            if last_top_level_component.as_deref() != Some(top_level_component.as_str()) {
                print_install_phase(&format!("extracting {top_level_component}/"));
                last_top_level_component = Some(top_level_component);
            }
        }
        let destination = bundle_root.join(&normalized_name);
        if entry.is_dir() || is_directory_entry {
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
) -> Result<PathBuf, String> {
    let plugin_source = bundle_root.join(&package.install.plugin.source);
    let plugin_destination = project_root.join(&package.install.plugin.destination);
    let workspace_source = bundle_root.join(&package.install.workspace.source);
    let workspace_destination = project_root.join(&package.install.workspace.destination);

    copy_tree(&plugin_source, &plugin_destination)?;
    print_install_phase(&format!("copied plugin to {}", plugin_destination.display()));
    copy_tree(&workspace_source, &workspace_destination)?;
    print_install_phase(&format!("copied workspace to {}", workspace_destination.display()));
    ensure_installed_binaries_executable(project_root, package)?;
    ensure_editor_performance_setting(project_root)?;
    write_runtime_install_state(project_root, version, package)
}

fn validate_installed_paths(project_root: &Path, package: &ReleasePackage) -> Result<(), String> {
    let plugin_destination = project_root.join(&package.install.plugin.destination);
    let workspace_destination = project_root.join(&package.install.workspace.destination);
    let client_path = installed_destination_path(
        project_root,
        WORKSPACE_SOURCE_ROOT,
        &package.install.workspace.destination,
        &package.client_binary_relpath,
    )?;
    let install_state_path = workspace_destination.join("runtime/install.json");

    if !plugin_destination.is_dir() {
        return Err(format!(
            "plugin destination missing after install: {}",
            plugin_destination.display()
        ));
    }
    if !workspace_destination.is_dir() {
        return Err(format!(
            "workspace destination missing after install: {}",
            workspace_destination.display()
        ));
    }
    if !client_path.is_file() {
        return Err(format!(
            "installed client binary missing: {}",
            client_path.display()
        ));
    }
    if !install_state_path.is_file() {
        return Err(format!(
            "install state missing after install: {}",
            install_state_path.display()
        ));
    }
    Ok(())
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
    ensure_executable_file(&client_path)?;
    Ok(())
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
    let settings_path = project_root.join("Config/DefaultEditorSettings.ini");

    let install_state = json!({
        "schemaVersion": 1,
        "installedVersion": version,
        "platform": platform_key(),
        "projectRoot": project_root.display().to_string(),
        "workspaceRoot": workspace_root.display().to_string(),
        "pluginRoot": project_root.join(&package.install.plugin.destination).display().to_string(),
        "clientPath": client_path.display().to_string(),
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
    use std::sync::{Mutex, OnceLock};

    fn env_lock() -> &'static Mutex<()> {
        static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
        LOCK.get_or_init(|| Mutex::new(()))
    }
    use zip::write::SimpleFileOptions;

    #[test]
    fn install_release_from_local_manifest_copies_plugin_and_workspace() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let bundle_root = build_root.join("bundle");
        fs::create_dir_all(bundle_root.join("plugin/LoomleBridge")).expect("plugin");
        fs::create_dir_all(bundle_root.join("workspace/Loomle")).expect("workspace");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/LoomleBridge.uplugin"),
            "{}",
        )
        .expect("uplugin");
        fs::write(
            bundle_root.join(format!("workspace/Loomle/{}", client_name)),
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
                            "client_binary_relpath": format!(
                                "workspace/Loomle/{client_name}"
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
        })
        .expect("install");

        assert_eq!(result["installedVersion"], "0.1.0");
        assert_eq!(result["restartRequired"], true);
        assert_eq!(result["restartReason"], RESTART_REASON_INSTALL);
        assert!(project_root
            .join("Plugins/LoomleBridge/LoomleBridge.uplugin")
            .is_file());
        assert!(project_root.join("Loomle").join(client_name).is_file());
        let install_state = fs::read_to_string(project_root.join("Loomle/runtime/install.json"))
            .expect("install state");
        let install_state_json: serde_json::Value =
            serde_json::from_str(&install_state).expect("install state json");
        assert_eq!(install_state_json["installedVersion"], "0.1.0");
        assert_eq!(install_state_json["platform"], platform_key());
        #[cfg(unix)]
        {
            let client_mode = fs::metadata(project_root.join("Loomle").join(client_name))
                .expect("client metadata")
                .permissions()
                .mode();
            assert_ne!(client_mode & 0o111, 0, "client should be executable");
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
        let _guard = env_lock().lock().expect("env lock");
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
    fn installer_download_url_uses_release_alias_and_installer_name() {
        let _guard = env_lock().lock().expect("env lock");
        std::env::set_var("LOOMLE_RELEASE_REPO", "example/test");
        let latest_url = installer_download_url(None).expect("latest installer url");
        let versioned_url =
            installer_download_url(Some("0.3.32")).expect("versioned installer url");
        if platform_key() == "windows" {
            assert_eq!(
                latest_url,
                "https://github.com/example/test/releases/download/loomle-latest/loomle-installer.exe"
            );
            assert_eq!(
                versioned_url,
                "https://github.com/example/test/releases/download/v0.3.32/loomle-installer.exe"
            );
        } else if platform_key() == "darwin" {
            assert_eq!(
                latest_url,
                "https://github.com/example/test/releases/download/loomle-latest/loomle-installer"
            );
            assert_eq!(
                versioned_url,
                "https://github.com/example/test/releases/download/v0.3.32/loomle-installer"
            );
        }
        std::env::remove_var("LOOMLE_RELEASE_REPO");
    }

    #[test]
    fn download_temp_installer_uses_local_override_path() {
        let _guard = env_lock().lock().expect("env lock");
        let temp_dir = TempDir::new().expect("temp dir");
        let source_path = temp_dir.path().join(installer_binary_name());
        fs::write(&source_path, b"installer-binary").expect("write source installer");
        std::env::set_var(INSTALLER_PATH_OVERRIDE_ENV, &source_path);

        let downloaded = download_temp_installer(&InstallerDownloadRequest {
            version: None,
            manifest_path: None,
            manifest_url: None,
            installer_path: None,
        })
        .expect("downloaded installer");
        let copied = fs::read(downloaded.path()).expect("read copied installer");
        assert_eq!(copied, b"installer-binary");

        std::env::remove_var(INSTALLER_PATH_OVERRIDE_ENV);
    }

    #[test]
    fn download_temp_installer_prefers_explicit_installer_path() {
        let _guard = env_lock().lock().expect("env lock");
        std::env::remove_var(INSTALLER_PATH_OVERRIDE_ENV);
        let temp_dir = TempDir::new().expect("temp dir");
        let source_path = temp_dir.path().join(installer_binary_name());
        fs::write(&source_path, b"explicit-installer").expect("write source installer");

        let downloaded = download_temp_installer(&InstallerDownloadRequest {
            version: None,
            manifest_path: None,
            manifest_url: None,
            installer_path: Some(source_path.clone()),
        })
        .expect("downloaded installer");
        let copied = fs::read(downloaded.path()).expect("read copied installer");
        assert_eq!(copied, b"explicit-installer");
    }

    #[test]
    fn download_temp_installer_uses_manifest_installer_url() {
        let _guard = env_lock().lock().expect("env lock");
        std::env::remove_var(INSTALLER_PATH_OVERRIDE_ENV);
        let temp_dir = TempDir::new().expect("temp dir");
        let source_path = temp_dir.path().join(installer_binary_name());
        fs::write(&source_path, b"manifest-installer").expect("write source installer");
        let source_sha = sha256_file(&source_path).expect("installer sha");
        let manifest_path = temp_dir.path().join("manifest.json");
        let manifest_json = json!({
            "latest": "0.3.32",
            "installer": {
                platform_key(): {
                    "url": source_path.display().to_string(),
                    "sha256": source_sha
                }
            },
            "versions": {
                "0.3.32": {
                    "packages": {}
                }
            }
        });
        fs::write(&manifest_path, serde_json::to_vec_pretty(&manifest_json).expect("manifest json"))
            .expect("write manifest");

        let downloaded = download_temp_installer(&InstallerDownloadRequest {
            version: Some(String::from("0.3.32")),
            manifest_path: Some(manifest_path),
            manifest_url: None,
            installer_path: None,
        })
        .expect("downloaded installer");
        let copied = fs::read(downloaded.path()).expect("read copied installer");
        assert_eq!(copied, b"manifest-installer");
    }

    #[test]
    fn install_release_keeps_prebuilt_binaries_and_plugin_source() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
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
            bundle_root.join(format!("plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}")),
        )
        .expect("plugin binaries");
        fs::create_dir_all(bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge"))
            .expect("plugin source");
        fs::create_dir_all(bundle_root.join("workspace/Loomle")).expect("workspace");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/LoomleBridge.uplugin"),
            "{}",
        )
        .expect("uplugin");
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
            bundle_root.join(format!("workspace/Loomle/{}", client_name)),
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
                            "client_binary_relpath": format!(
                                "workspace/Loomle/{client_name}"
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
        })
        .expect("install");

        assert!(project_root
            .join("Plugins/LoomleBridge/Binaries")
            .join(plugin_binary_platform_dir)
            .is_dir());
        assert!(project_root
            .join("Plugins/LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs")
            .is_file());
    }

    #[test]
    fn install_release_runtime_state_omits_plugin_mode() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
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
            bundle_root.join(format!("plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}")),
        )
        .expect("plugin binaries");
        fs::create_dir_all(bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge"))
            .expect("plugin source");
        fs::create_dir_all(bundle_root.join("workspace/Loomle")).expect("workspace");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/LoomleBridge.uplugin"),
            "{}",
        )
        .expect("uplugin");
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
            bundle_root.join(format!("workspace/Loomle/{}", client_name)),
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
                            "client_binary_relpath": format!(
                                "workspace/Loomle/{client_name}"
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
        })
        .expect("install");

        assert!(project_root
            .join("Plugins/LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs")
            .is_file());
        let install_state = fs::read_to_string(project_root.join("Loomle/runtime/install.json"))
            .expect("install state");
        let install_state_json: serde_json::Value =
            serde_json::from_str(&install_state).expect("install state json");
        assert_eq!(install_state_json["installedVersion"], "0.1.0");
        assert!(install_state_json.get("pluginMode").is_none());
    }

    #[test]
    fn update_release_reports_latest_available_version() {
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
        })
        .expect("install 0.1.0");

        let result = update_release(UpdateRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            apply: false,
        })
        .expect("update check");

        assert_eq!(result["installedVersion"], "0.1.0");
        assert_eq!(result["latestVersion"], "0.2.0");
        assert_eq!(result["targetVersion"], "0.2.0");
        assert_eq!(result["updateAvailable"], true);
        assert_eq!(result["wouldChange"], true);
    }

    #[test]
    fn update_release_apply_upgrades_and_keeps_plugin_source_available() {
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
        })
        .expect("install 0.1.0");

        let result = update_release(UpdateRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            apply: true,
        })
        .expect("update apply");

        assert_eq!(result["updated"], true);
        assert_eq!(result["targetVersion"], "0.2.0");
        assert_eq!(result["restartRequired"], true);
        assert_eq!(
            result["restartReason"],
            "If Unreal Editor is already running, restart it to load the updated LoomleBridge plugin version."
        );

        let install_state = fs::read_to_string(project_root.join("Loomle/runtime/install.json"))
            .expect("install state");
        let install_state_json: serde_json::Value =
            serde_json::from_str(&install_state).expect("install state json");
        assert_eq!(install_state_json["installedVersion"], "0.2.0");
        assert!(install_state_json.get("pluginMode").is_none());
        assert!(project_root
            .join("Plugins/LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs")
            .is_file());
    }

    #[test]
    fn update_release_missing_install_state_reports_repair_command() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let release_010 = build_release_archive(&build_root, "0.1.0");
        let manifest_path = write_manifest(&build_root, "0.1.0", &[("0.1.0", &release_010.0, &release_010.1)]);

        let error = update_release(UpdateRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            apply: false,
        })
        .expect_err("missing install state should fail");

        assert!(error.contains("install state not found"));
        assert!(error.contains("loomle-installer install --project-root"));
        assert!(error.contains(&project_root.display().to_string()));
    }

    #[test]
    fn update_release_apply_repairs_when_install_state_is_missing() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let release_010 = build_release_archive(&build_root, "0.1.0");
        let manifest_path = write_manifest(&build_root, "0.1.0", &[("0.1.0", &release_010.0, &release_010.1)]);

        let result = update_release(UpdateRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
            apply: true,
        })
        .expect("missing install state should repair via install");

        assert_eq!(result["updated"], true);
        assert_eq!(result["repair"], true);
        assert_eq!(result["targetVersion"], "0.1.0");
        assert!(project_root.join("Loomle/runtime/install.json").is_file());
    }

    fn build_release_archive(build_root: &Path, version: &str) -> (PathBuf, String) {
        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };
        let plugin_binary_platform_dir =
            plugin_binary_platform_dir().expect("supported plugin binary platform");
        let bundle_root = build_root.join(format!("bundle-{version}"));

        fs::create_dir_all(
            bundle_root.join(format!("plugin/LoomleBridge/Binaries/{plugin_binary_platform_dir}")),
        )
        .expect("plugin binaries");
        fs::create_dir_all(bundle_root.join("plugin/LoomleBridge/Source/LoomleBridge"))
            .expect("plugin source");
        fs::create_dir_all(bundle_root.join("workspace/Loomle")).expect("workspace");
        fs::write(
            bundle_root.join("plugin/LoomleBridge/LoomleBridge.uplugin"),
            "{}",
        )
        .expect("uplugin");
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
            bundle_root.join(format!("workspace/Loomle/{}", client_name)),
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
                            "client_binary_relpath": format!(
                                "workspace/Loomle/{client_name}"
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

    #[test]
    fn normalize_zip_entry_name_supports_windows_style_directory_entries() {
        let (normalized, is_directory) =
            normalize_zip_entry_name("mcp\\client\\").expect("normalize");
        assert_eq!(normalized, PathBuf::from("mcp").join("client"));
        assert!(is_directory);
    }

    #[test]
    fn normalize_zip_entry_name_rejects_invalid_windows_path_components() {
        let error =
            normalize_zip_entry_name("bundle\\mcp\\client\\:").expect_err("should reject");
        assert!(error.contains("invalid path component `:`"));
    }

    #[test]
    fn install_release_extracts_windows_directory_entries() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        let archive_path = build_root.join("loomle.zip");
        let manifest_path = build_root.join("manifest.json");
        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };

        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let file = fs::File::create(&archive_path).expect("archive");
        let mut zip = zip::ZipWriter::new(file);
        let options = SimpleFileOptions::default();
        zip.add_directory("plugin/LoomleBridge/", options)
            .expect("plugin dir");
        zip.add_directory("plugin/LoomleBridge/Tools/", options)
            .expect("tools dir");
        zip.add_directory("plugin/LoomleBridge/Tools/mcp/", options)
            .expect("mcp dir");
        zip.add_directory("workspace/Loomle/", options)
            .expect("workspace dir");
        zip.add_directory("mcp\\client\\", options)
            .expect("windows-style dir");
        zip.start_file("plugin/LoomleBridge/LoomleBridge.uplugin", options)
            .expect("uplugin");
        zip.write_all(b"{}").expect("uplugin bytes");
        zip.start_file(format!("workspace/Loomle/{client_name}"), options)
            .expect("client");
        zip.write_all(b"client").expect("client bytes");
        zip.finish().expect("finish zip");

        let archive_sha = sha256_file(&archive_path).expect("sha");
        let manifest = serde_json::json!({
            "latest": "0.1.0",
            "versions": {
                "0.1.0": {
                    "packages": {
                        platform_key(): {
                            "url": archive_path.to_string_lossy(),
                            "sha256": archive_sha,
                            "format": "zip",
                            "client_binary_relpath": format!("workspace/Loomle/{client_name}"),
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
        })
        .expect("install");

        assert!(project_root
            .join("Plugins/LoomleBridge/LoomleBridge.uplugin")
            .is_file());
        assert!(project_root.join("Loomle").join(client_name).is_file());
    }

    #[test]
    fn extract_zip_rejects_invalid_windows_path_component_entries() {
        let temp = TempDir::new().expect("temp");
        let archive_path = temp.path().join("loomle.zip");
        let bundle_root = temp.path().join("bundle");

        let file = fs::File::create(&archive_path).expect("archive");
        let mut zip = zip::ZipWriter::new(file);
        let options = SimpleFileOptions::default();
        zip.start_file("bundle\\mcp\\client\\:", options)
            .expect("invalid entry");
        zip.write_all(b"bad").expect("bad bytes");
        zip.finish().expect("finish zip");

        let error = extract_zip(&archive_path, &bundle_root).expect_err("extract should fail");
        assert!(error.contains("invalid path component `:`"));
    }

    #[test]
    fn install_release_reports_invalid_incomplete_zip_without_sha() {
        let temp = TempDir::new().expect("temp");
        let build_root = temp.path().join("build");
        let project_root = temp.path().join("Project");
        fs::create_dir_all(&build_root).expect("build root");
        fs::create_dir_all(&project_root).expect("project root");
        fs::write(project_root.join("Demo.uproject"), "{}").expect("uproject");

        let (archive_path, _archive_sha) = build_release_archive(&build_root, "0.1.0");
        let bytes = fs::read(&archive_path).expect("read archive");
        let truncated_len = bytes.len().saturating_sub(64).max(1);
        fs::write(&archive_path, &bytes[..truncated_len]).expect("truncate archive");

        let client_name = if platform_key() == "windows" {
            "loomle.exe"
        } else {
            "loomle"
        };
        let manifest_path = build_root.join("manifest-no-sha.json");
        let manifest = serde_json::json!({
            "latest": "0.1.0",
            "versions": {
                "0.1.0": {
                    "packages": {
                        platform_key(): {
                            "url": archive_path.to_string_lossy(),
                            "sha256": "",
                            "format": "zip",
                            "client_binary_relpath": format!("workspace/Loomle/{client_name}"),
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

        let error = install_release(InstallRequest {
            project_root: project_root.clone(),
            version: None,
            manifest_path: Some(manifest_path),
            manifest_url: None,
        })
        .expect_err("truncated zip should fail");

        assert!(error.contains("invalid or incomplete zip archive"));
        assert!(error.contains("archive may be invalid or incomplete"));
        assert!(error.contains("manifest-path"));
    }

    fn plugin_binary_platform_dir() -> Option<&'static str> {
        match platform_key() {
            "darwin" => Some("Mac"),
            "windows" => Some("Win64"),
            "linux" => Some("Linux"),
            _ => None,
        }
    }
}
