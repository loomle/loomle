use reqwest::blocking::Client;
use reqwest::header::{ACCEPT, USER_AGENT};
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::collections::{HashMap, HashSet};
use std::env;
use std::fs;
use std::io::Write;
use std::path::{Component, Path, PathBuf};
use tempfile::TempDir;

#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;

const DEFAULT_SKILL_REGISTRY_URL: &str =
    "https://raw.githubusercontent.com/loomle/skills/main/registry/index.json";
const INSTALL_METADATA_FILENAME: &str = ".loomle-skill.json";
const HTTP_USER_AGENT: &str = "loomle-client/skill";

#[derive(Debug, Clone)]
pub struct SkillListRequest {
    pub installed_only: bool,
    pub skills_root: Option<PathBuf>,
    pub registry_url: Option<String>,
}

#[derive(Debug, Clone)]
pub struct SkillInstallRequest {
    pub skill_name: String,
    pub skills_root: Option<PathBuf>,
    pub registry_url: Option<String>,
}

#[derive(Debug, Clone)]
pub struct SkillRemoveRequest {
    pub skill_name: String,
    pub skills_root: Option<PathBuf>,
}

#[derive(Debug, Deserialize)]
struct SkillRegistry {
    #[serde(default)]
    schema_version: u64,
    repo: RegistryRepo,
    skills: Vec<RegistrySkill>,
}

#[derive(Debug, Deserialize)]
struct RegistryRepo {
    name: String,
    url: String,
    branch: String,
}

#[derive(Debug, Clone, Deserialize)]
struct RegistrySkill {
    name: String,
    description: String,
    display_name: String,
    short_description: String,
    default_prompt: String,
    path: String,
    source_url: String,
}

#[derive(Debug, Deserialize)]
struct GitTreeResponse {
    tree: Vec<GitTreeEntry>,
}

#[derive(Debug, Deserialize)]
struct GitTreeEntry {
    path: String,
    mode: String,
    #[serde(rename = "type")]
    entry_type: String,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
struct InstalledSkillMetadata {
    name: String,
    #[serde(rename = "displayName")]
    display_name: String,
    #[serde(rename = "sourceUrl")]
    source_url: String,
    repo: String,
    branch: String,
}

#[derive(Debug)]
struct InstalledSkillRecord {
    path: PathBuf,
    metadata: Option<InstalledSkillMetadata>,
}

pub fn list_skills(request: SkillListRequest) -> Result<serde_json::Value, String> {
    let skills_root = resolve_skills_root(request.skills_root.as_deref())?;
    let registry = load_registry(request.registry_url.as_deref())?;
    let installed = scan_installed_skills(&skills_root)?;
    let official_names = registry
        .skills
        .iter()
        .map(|skill| skill.name.clone())
        .collect::<HashSet<_>>();

    let mut skills = Vec::new();
    for skill in &registry.skills {
        let installed_record = installed.get(&skill.name);
        if request.installed_only && installed_record.is_none() {
            continue;
        }

        skills.push(json!({
            "name": skill.name,
            "displayName": skill.display_name,
            "shortDescription": skill.short_description,
            "description": skill.description,
            "defaultPrompt": skill.default_prompt,
            "sourceUrl": skill.source_url,
            "installed": installed_record.is_some(),
            "installedPath": installed_record.map(|record| record.path.display().to_string()),
            "installedMetadata": installed_record.and_then(|record| {
                record.metadata.as_ref().map(|metadata| json!({
                    "name": metadata.name,
                    "displayName": metadata.display_name,
                    "sourceUrl": metadata.source_url,
                    "repo": metadata.repo,
                    "branch": metadata.branch,
                }))
            }),
        }));
    }

    let unmanaged_skills = installed
        .iter()
        .filter(|(name, _)| !official_names.contains(*name))
        .map(|(name, record)| {
            json!({
                "name": name,
                "installedPath": record.path.display().to_string(),
            })
        })
        .collect::<Vec<_>>();

    Ok(json!({
        "registry": {
            "schemaVersion": registry.schema_version,
            "repo": registry.repo.name,
            "url": registry.repo.url,
            "branch": registry.repo.branch,
        },
        "skillsRoot": skills_root.display().to_string(),
        "installedOnly": request.installed_only,
        "skills": skills,
        "unmanagedSkills": unmanaged_skills,
    }))
}

pub fn install_skill(request: SkillInstallRequest) -> Result<serde_json::Value, String> {
    validate_skill_name(&request.skill_name)?;
    let skills_root = resolve_skills_root(request.skills_root.as_deref())?;
    fs::create_dir_all(&skills_root).map_err(|error| {
        format!(
            "failed to create skills root {}: {error}",
            skills_root.display()
        )
    })?;

    let registry = load_registry(request.registry_url.as_deref())?;
    let skill = registry
        .skills
        .iter()
        .find(|entry| entry.name == request.skill_name)
        .ok_or_else(|| {
            let known = registry
                .skills
                .iter()
                .map(|entry| entry.name.as_str())
                .collect::<Vec<_>>()
                .join(", ");
            format!(
                "unknown official skill `{}`. Available skills: {}",
                request.skill_name, known
            )
        })?;

    let destination = skills_root.join(&skill.name);
    if destination.exists() {
        return Err(format!(
            "skill already installed: {}. Remove it first with `loomle skill remove {}`.",
            destination.display(),
            skill.name
        ));
    }

    install_remote_skill(skill, &registry.repo, &destination)?;
    validate_installed_skill(&destination)?;
    let metadata = InstalledSkillMetadata {
        name: skill.name.clone(),
        display_name: skill.display_name.clone(),
        source_url: skill.source_url.clone(),
        repo: registry.repo.name.clone(),
        branch: registry.repo.branch.clone(),
    };
    write_install_metadata(&destination, &metadata)?;

    Ok(json!({
        "installed": true,
        "name": skill.name,
        "displayName": skill.display_name,
        "skillsRoot": skills_root.display().to_string(),
        "installedPath": destination.display().to_string(),
        "sourceUrl": skill.source_url,
    }))
}

pub fn remove_skill(request: SkillRemoveRequest) -> Result<serde_json::Value, String> {
    validate_skill_name(&request.skill_name)?;
    let skills_root = resolve_skills_root(request.skills_root.as_deref())?;
    let destination = skills_root.join(&request.skill_name);
    if !destination.exists() {
        return Err(format!("skill is not installed: {}", destination.display()));
    }
    validate_installed_skill(&destination)?;
    fs::remove_dir_all(&destination)
        .map_err(|error| format!("failed to remove {}: {error}", destination.display()))?;

    Ok(json!({
        "removed": true,
        "name": request.skill_name,
        "skillsRoot": skills_root.display().to_string(),
        "removedPath": destination.display().to_string(),
    }))
}

fn resolve_skills_root(explicit: Option<&Path>) -> Result<PathBuf, String> {
    if let Some(path) = explicit {
        return Ok(path.to_path_buf());
    }

    if let Ok(explicit_root) = env::var("LOOMLE_SKILLS_ROOT") {
        let trimmed = explicit_root.trim();
        if !trimmed.is_empty() {
            return Ok(PathBuf::from(trimmed));
        }
    }

    if let Ok(codex_home) = env::var("CODEX_HOME") {
        let trimmed = codex_home.trim();
        if !trimmed.is_empty() {
            return Ok(PathBuf::from(trimmed).join("skills"));
        }
    }

    let home_dir = env::var_os("HOME")
        .or_else(|| env::var_os("USERPROFILE"))
        .map(PathBuf::from)
        .ok_or_else(|| String::from("could not resolve HOME/USERPROFILE for skills root"))?;
    Ok(home_dir.join(".codex").join("skills"))
}

fn load_registry(explicit_registry_url: Option<&str>) -> Result<SkillRegistry, String> {
    let registry_url = explicit_registry_url
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
        .or_else(|| {
            env::var("LOOMLE_SKILL_REGISTRY_URL")
                .ok()
                .map(|value| value.trim().to_owned())
                .filter(|value| !value.is_empty())
        })
        .unwrap_or_else(|| String::from(DEFAULT_SKILL_REGISTRY_URL));

    let body = fetch_bytes(&registry_url)?;
    serde_json::from_slice(&body)
        .map_err(|error| format!("invalid skill registry JSON {registry_url}: {error}"))
}

fn scan_installed_skills(
    skills_root: &Path,
) -> Result<HashMap<String, InstalledSkillRecord>, String> {
    let mut installed = HashMap::new();
    if !skills_root.exists() {
        return Ok(installed);
    }

    for entry in fs::read_dir(skills_root).map_err(|error| {
        format!(
            "failed to read skills root {}: {error}",
            skills_root.display()
        )
    })? {
        let entry = entry.map_err(|error| {
            format!(
                "failed to read entry under skills root {}: {error}",
                skills_root.display()
            )
        })?;
        let path = entry.path();
        if !path.is_dir() || !path.join("SKILL.md").is_file() {
            continue;
        }
        let name = entry.file_name().to_string_lossy().to_string();
        let metadata = read_install_metadata(&path).ok();
        installed.insert(name, InstalledSkillRecord { path, metadata });
    }

    Ok(installed)
}

fn install_remote_skill(
    skill: &RegistrySkill,
    repo: &RegistryRepo,
    destination: &Path,
) -> Result<(), String> {
    let temp_dir = TempDir::new().map_err(|error| format!("failed to create temp dir: {error}"))?;
    let temp_root = temp_dir.path().join("skill");
    fs::create_dir_all(&temp_root).map_err(|error| {
        format!(
            "failed to create temp skill dir {}: {error}",
            temp_root.display()
        )
    })?;

    let entries = fetch_remote_tree(repo)?;
    let prefix = format!("{}/", skill.path.trim_end_matches('/'));
    let matching_entries = entries
        .into_iter()
        .filter(|entry| entry.path.starts_with(&prefix) && entry.entry_type == "blob")
        .collect::<Vec<_>>();
    if matching_entries.is_empty() {
        return Err(format!(
            "official skill `{}` has no downloadable files under {}",
            skill.name, skill.path
        ));
    }

    for entry in matching_entries {
        let relative = Path::new(&entry.path)
            .strip_prefix(&skill.path)
            .map_err(|error| format!("invalid skill entry path {}: {error}", entry.path))?;
        let target = temp_root.join(relative);
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent)
                .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
        }

        let raw_url = format!(
            "https://raw.githubusercontent.com/{}/{}/{}",
            repo.name, repo.branch, entry.path
        );
        let bytes = fetch_bytes(&raw_url)?;
        let mut file = fs::File::create(&target)
            .map_err(|error| format!("failed to create {}: {error}", target.display()))?;
        file.write_all(&bytes)
            .map_err(|error| format!("failed to write {}: {error}", target.display()))?;

        #[cfg(unix)]
        if entry.mode == "100755" {
            let mut permissions = file
                .metadata()
                .map_err(|error| format!("failed to stat {}: {error}", target.display()))?
                .permissions();
            permissions.set_mode(permissions.mode() | 0o111);
            fs::set_permissions(&target, permissions)
                .map_err(|error| format!("failed to chmod {}: {error}", target.display()))?;
        }
    }

    fs::rename(&temp_root, destination)
        .map_err(|error| format!("failed to install {}: {error}", destination.display()))
}

fn fetch_remote_tree(repo: &RegistryRepo) -> Result<Vec<GitTreeEntry>, String> {
    let api_url = format!(
        "https://api.github.com/repos/{}/git/trees/{}?recursive=1",
        repo.name, repo.branch
    );
    let body = fetch_bytes(&api_url)?;
    let response: GitTreeResponse = serde_json::from_slice(&body)
        .map_err(|error| format!("invalid Git tree JSON {api_url}: {error}"))?;
    Ok(response.tree)
}

fn fetch_bytes(url: &str) -> Result<Vec<u8>, String> {
    if let Some(path) = file_path_from_uri(url)? {
        return fs::read(&path)
            .map_err(|error| format!("failed to read local file {}: {error}", path.display()));
    }

    Client::new()
        .get(url)
        .header(USER_AGENT, HTTP_USER_AGENT)
        .header(ACCEPT, "application/json, text/plain, */*")
        .send()
        .and_then(|response| response.error_for_status())
        .map_err(|error| format!("failed to download {url}: {error}"))?
        .bytes()
        .map(|bytes| bytes.to_vec())
        .map_err(|error| format!("failed to read response body {url}: {error}"))
}

fn file_path_from_uri(raw: &str) -> Result<Option<PathBuf>, String> {
    if !raw.starts_with("file://") {
        return Ok(None);
    }
    let url =
        reqwest::Url::parse(raw).map_err(|error| format!("invalid file URL {raw}: {error}"))?;
    url.to_file_path()
        .map(Some)
        .map_err(|_| format!("could not convert file URL to path: {raw}"))
}

fn validate_installed_skill(path: &Path) -> Result<(), String> {
    if !path.is_dir() {
        return Err(format!("skill path is not a directory: {}", path.display()));
    }
    let skill_md = path.join("SKILL.md");
    if !skill_md.is_file() {
        return Err(format!(
            "installed skill is missing SKILL.md: {}",
            skill_md.display()
        ));
    }
    Ok(())
}

fn write_install_metadata(path: &Path, metadata: &InstalledSkillMetadata) -> Result<(), String> {
    let metadata_path = path.join(INSTALL_METADATA_FILENAME);
    let body = serde_json::to_string_pretty(metadata)
        .map_err(|error| format!("failed to encode install metadata: {error}"))?;
    fs::write(&metadata_path, body)
        .map_err(|error| format!("failed to write {}: {error}", metadata_path.display()))
}

fn read_install_metadata(path: &Path) -> Result<InstalledSkillMetadata, String> {
    let metadata_path = path.join(INSTALL_METADATA_FILENAME);
    let body = fs::read_to_string(&metadata_path)
        .map_err(|error| format!("failed to read {}: {error}", metadata_path.display()))?;
    serde_json::from_str(&body).map_err(|error| {
        format!(
            "invalid install metadata {}: {error}",
            metadata_path.display()
        )
    })
}

fn validate_skill_name(name: &str) -> Result<(), String> {
    if name.trim().is_empty() {
        return Err(String::from("skill name must not be empty"));
    }
    let path = Path::new(name);
    if path.components().any(|component| {
        matches!(
            component,
            Component::ParentDir | Component::RootDir | Component::Prefix(_)
        )
    }) || name.contains(std::path::MAIN_SEPARATOR)
    {
        return Err(format!("invalid skill name `{name}`"));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{
        list_skills, remove_skill, scan_installed_skills, SkillListRequest, SkillRemoveRequest,
        INSTALL_METADATA_FILENAME,
    };
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn list_installed_filters_official_skills() {
        let temp = TempDir::new().expect("temp");
        let root = temp.path().join("skills");
        fs::create_dir_all(root.join("material-weaver")).expect("create skill dir");
        fs::write(root.join("material-weaver").join("SKILL.md"), "# test").expect("write skill");
        fs::create_dir_all(root.join("notes")).expect("create unmanaged dir");

        let registry_path = temp.path().join("registry.json");
        fs::write(
            &registry_path,
            r#"{
  "schema_version": 1,
  "repo": {"name":"loomle/skills","url":"https://github.com/loomle/skills","branch":"main"},
  "skills": [
    {
      "name":"material-weaver",
      "description":"desc",
      "display_name":"Material Weaver",
      "short_description":"short",
      "default_prompt":"prompt",
      "path":"skills/material-weaver",
      "source_url":"https://github.com/loomle/skills/tree/main/skills/material-weaver"
    },
    {
      "name":"pcg-weaver",
      "description":"desc",
      "display_name":"PCG Weaver",
      "short_description":"short",
      "default_prompt":"prompt",
      "path":"skills/pcg-weaver",
      "source_url":"https://github.com/loomle/skills/tree/main/skills/pcg-weaver"
    }
  ]
}"#,
        )
        .expect("write registry");
        let registry_url = format!("file://{}", registry_path.display());

        let listed = list_skills(SkillListRequest {
            installed_only: true,
            skills_root: Some(root.clone()),
            registry_url: Some(registry_url),
        })
        .expect("list skills");

        let skills = listed["skills"].as_array().expect("skills array");
        assert_eq!(skills.len(), 1);
        assert_eq!(skills[0]["name"], "material-weaver");
        assert_eq!(
            listed["unmanagedSkills"]
                .as_array()
                .expect("unmanaged")
                .len(),
            0
        );
    }

    #[test]
    fn remove_skill_rejects_missing_skill_md() {
        let temp = TempDir::new().expect("temp");
        let root = temp.path().join("skills");
        let bad_skill = root.join("bad-skill");
        fs::create_dir_all(&bad_skill).expect("create dir");

        let error = remove_skill(SkillRemoveRequest {
            skill_name: String::from("bad-skill"),
            skills_root: Some(root),
        })
        .expect_err("remove should fail");
        assert!(error.contains("SKILL.md"));
    }

    #[test]
    fn scan_installed_skills_reads_metadata_when_present() {
        let temp = TempDir::new().expect("temp");
        let root = temp.path().join("skills");
        let skill = root.join("material-weaver");
        fs::create_dir_all(&skill).expect("create dir");
        fs::write(skill.join("SKILL.md"), "# test").expect("write skill");
        fs::write(
            skill.join(INSTALL_METADATA_FILENAME),
            r#"{
  "name":"material-weaver",
  "displayName":"Material Weaver",
  "sourceUrl":"https://github.com/loomle/skills/tree/main/skills/material-weaver",
  "repo":"loomle/skills",
  "branch":"main"
}"#,
        )
        .expect("write metadata");

        let installed = scan_installed_skills(&root).expect("scan");
        let record = installed.get("material-weaver").expect("record");
        assert_eq!(
            record.metadata.as_ref().expect("metadata").display_name,
            "Material Weaver"
        );
    }
}
