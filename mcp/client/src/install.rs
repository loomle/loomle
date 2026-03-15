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

const DEFAULT_RELEASE_REPO: &str = "loomle/loomle";

#[derive(Debug, Clone)]
pub struct InstallRequest {
    pub project_root: PathBuf,
    pub version: Option<String>,
    pub manifest_path: Option<PathBuf>,
    pub manifest_url: Option<String>,
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
    copy_release_tree(&bundle_root, &project_root, package)?;

    Ok(json!({
        "installedVersion": version,
        "platform": platform_key(),
        "bundleRoot": bundle_root.display().to_string(),
        "projectRoot": project_root.display().to_string(),
        "plugin": {
            "source": (bundle_root.join(&package.install.plugin.source)).display().to_string(),
            "destination": (project_root.join(&package.install.plugin.destination)).display().to_string(),
        },
        "workspace": {
            "source": (bundle_root.join(&package.install.workspace.source)).display().to_string(),
            "destination": (project_root.join(&package.install.workspace.destination)).display().to_string(),
        }
    }))
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
    package: &ReleasePackage,
) -> Result<(), String> {
    let plugin_source = bundle_root.join(&package.install.plugin.source);
    let plugin_destination = project_root.join(&package.install.plugin.destination);
    let workspace_source = bundle_root.join(&package.install.workspace.source);
    let workspace_destination = project_root.join(&package.install.workspace.destination);

    copy_tree(&plugin_source, &plugin_destination)?;
    copy_tree(&workspace_source, &workspace_destination)
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
        })
        .expect("install");

        assert_eq!(result["installedVersion"], "0.1.0");
        assert!(project_root
            .join("Plugins/LoomleBridge/LoomleBridge.uplugin")
            .is_file());
        assert!(project_root.join("Loomle/client").is_dir());
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
