$ErrorActionPreference = "Stop"

$ReleaseRepo = if ($env:LOOMLE_RELEASE_REPO) { $env:LOOMLE_RELEASE_REPO } else { "loomle/loomle" }
$RequestedVersion = if ($env:LOOMLE_BOOTSTRAP_VERSION) { $env:LOOMLE_BOOTSTRAP_VERSION } else { "latest" }
$EditorPerfSection = "[/Script/UnrealEd.EditorPerformanceSettings]"
$EditorThrottleSetting = "bThrottleCPUWhenNotForeground=False"

function Fail([string]$Message) {
  Write-Error $Message
  exit 1
}

function Resolve-ReleaseTag([string]$Version) {
  if ($Version.StartsWith("v")) { return $Version }
  return "v$Version"
}

function Find-ProjectRoot([string]$Start) {
  $dir = (Resolve-Path $Start).Path
  while ($true) {
    if (Get-ChildItem -LiteralPath $dir -Filter *.uproject -File -ErrorAction SilentlyContinue) {
      return $dir
    }
    $parent = Split-Path -Parent $dir
    if ($parent -eq $dir -or [string]::IsNullOrWhiteSpace($parent)) { return $null }
    $dir = $parent
  }
}

function Resolve-EffectiveVersion([string]$ManifestPath, [string]$Version) {
  if ($Version -eq "latest") {
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($manifest.latest)) {
      Fail "failed to resolve latest version from manifest"
    }
    return [string]$manifest.latest
  }
  return $Version.TrimStart('v')
}

function Get-VersionedClientName([string]$Version) {
  return "loomle-$Version.exe"
}

function Copy-TreeReplace([string]$Source, [string]$Destination) {
  if (-not (Test-Path -LiteralPath $Source)) {
    Fail "install source not found: $Source"
  }
  Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
  Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

function Sync-WorkspaceEntries([string]$SourceRoot, [string]$DestinationRoot, [string[]]$SkipNames = @()) {
  New-Item -ItemType Directory -Force -Path $DestinationRoot | Out-Null
  foreach ($entry in Get-ChildItem -LiteralPath $SourceRoot -Force) {
    if ($SkipNames -contains $entry.Name) {
      continue
    }
    $destination = Join-Path $DestinationRoot $entry.Name
    Remove-Item -LiteralPath $destination -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $entry.FullName -Destination $destination -Recurse -Force
  }
}

function Ensure-IniSetting([string]$Path, [string]$Section, [string]$Setting) {
  $existing = ""
  if (Test-Path -LiteralPath $Path) {
    $existing = Get-Content -LiteralPath $Path -Raw
    if ($existing -match [regex]::Escape($Setting)) {
      return
    }
  }

  $addition = if ([string]::IsNullOrWhiteSpace($existing)) {
    "$Section`r`n$Setting`r`n"
  } else {
    $existing.TrimEnd("`r", "`n") + "`r`n`r`n$Section`r`n$Setting`r`n"
  }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
  Set-Content -LiteralPath $Path -Value $addition -Encoding UTF8
}

function Ensure-WorkspaceLayout([string]$WorkspaceRoot) {
  @(
    "install\versions",
    "install\manifests",
    "install\pending",
    "state\diag",
    "state\captures"
  ) | ForEach-Object {
    New-Item -ItemType Directory -Force -Path (Join-Path $WorkspaceRoot $_) | Out-Null
  }
}

function Copy-VersionedPayload(
  [string]$WorkspaceSource,
  [string]$WorkspaceRoot,
  [string]$Version,
  [string]$SourceClientName,
  [string]$TargetClientName
) {
  $VersionRoot = Join-Path $WorkspaceRoot "install\versions\$Version"
  $KitRoot = Join-Path $VersionRoot "kit"
  New-Item -ItemType Directory -Force -Path $VersionRoot | Out-Null
  Copy-Item -LiteralPath (Join-Path $WorkspaceSource $SourceClientName) -Destination (Join-Path $VersionRoot $TargetClientName) -Force
  Remove-Item -LiteralPath $KitRoot -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $KitRoot | Out-Null
  foreach ($entryName in @("README.md", "blueprint", "material", "pcg", "workflows", "examples")) {
    $entryPath = Join-Path $WorkspaceSource $entryName
    if (Test-Path -LiteralPath $entryPath) {
      Copy-Item -LiteralPath $entryPath -Destination (Join-Path $KitRoot $entryName) -Recurse -Force
    }
  }
}

function Copy-ManifestRecord([string]$ManifestPath, [string]$WorkspaceRoot, [string]$Version) {
  Copy-Item -LiteralPath $ManifestPath -Destination (Join-Path $WorkspaceRoot "install\manifests\$Version.json") -Force
}

function Write-ActiveState(
  [string]$ActiveStatePath,
  [string]$Version,
  [string]$Platform,
  [string]$ProjectRoot,
  [string]$PluginRoot,
  [string]$WorkspaceRoot,
  [string]$LauncherPath,
  [string]$ActiveClientPath,
  [string]$SettingsPath
) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ActiveStatePath) | Out-Null
  $payload = [ordered]@{
    schemaVersion = 1
    installedVersion = $Version
    activeVersion = $Version
    platform = $Platform
    projectRoot = $ProjectRoot
    loomleRoot = $WorkspaceRoot
    pluginRoot = $PluginRoot
    launcherPath = $LauncherPath
    activeClientPath = $ActiveClientPath
    manifestsRoot = (Join-Path $WorkspaceRoot "install\manifests")
    versionsRoot = (Join-Path $WorkspaceRoot "install\versions")
    editorPerformance = [ordered]@{
      settingsFile = $SettingsPath
      throttleWhenNotForeground = $false
    }
  }
  $payload | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ActiveStatePath -Encoding UTF8
}

$ProjectRoot = ""
$ManifestUrl = ""
$AssetUrl = ""

for ($i = 0; $i -lt $args.Count; $i++) {
  switch ($args[$i]) {
    "--project-root" {
      $i++; if ($i -ge $args.Count) { Fail "missing value for --project-root" }
      $ProjectRoot = $args[$i]
    }
    "--version" {
      $i++; if ($i -ge $args.Count) { Fail "missing value for --version" }
      $RequestedVersion = $args[$i]
    }
    "--manifest-url" {
      $i++; if ($i -ge $args.Count) { Fail "missing value for --manifest-url" }
      $ManifestUrl = $args[$i]
    }
    "--asset-url" {
      $i++; if ($i -ge $args.Count) { Fail "missing value for --asset-url" }
      $AssetUrl = $args[$i]
    }
    "--help" { Write-Host "Usage: install.ps1 [--project-root <ProjectRoot>] [--version <Version>] [--manifest-url <URL>] [--asset-url <URL>]"; exit 0 }
    default { Fail "unknown argument: $($args[$i])" }
  }
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
  $ProjectRoot = Find-ProjectRoot (Get-Location).Path
}
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
  Fail "could not resolve Unreal project root; pass --project-root"
}
$ProjectRoot = (Resolve-Path $ProjectRoot).Path

if ([string]::IsNullOrWhiteSpace($ManifestUrl)) {
  if ($RequestedVersion -eq "latest") {
    $ManifestUrl = "https://github.com/$ReleaseRepo/releases/latest/download/loomle-manifest-windows.json"
  } else {
    $ReleaseTag = Resolve-ReleaseTag $RequestedVersion
    $ManifestUrl = "https://github.com/$ReleaseRepo/releases/download/$ReleaseTag/loomle-manifest-windows.json"
  }
}
if ([string]::IsNullOrWhiteSpace($AssetUrl)) {
  if ($RequestedVersion -eq "latest") {
    $AssetUrl = "https://github.com/$ReleaseRepo/releases/latest/download/loomle-windows.zip"
  } else {
    if (-not $ReleaseTag) { $ReleaseTag = Resolve-ReleaseTag $RequestedVersion }
    $AssetUrl = "https://github.com/$ReleaseRepo/releases/download/$ReleaseTag/loomle-windows.zip"
  }
}

$TmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("loomle-install-" + [System.Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $TmpDir -Force
$ManifestPath = Join-Path $TmpDir "manifest.json"
$ArchivePath = Join-Path $TmpDir "loomle-windows.zip"
$BundleDir = Join-Path $TmpDir "bundle"

try {
  Invoke-WebRequest -Uri $ManifestUrl -OutFile $ManifestPath
  $EffectiveVersion = Resolve-EffectiveVersion -ManifestPath $ManifestPath -Version $RequestedVersion
  $ActiveClientName = Get-VersionedClientName $EffectiveVersion

  Invoke-WebRequest -Uri $AssetUrl -OutFile $ArchivePath
  Expand-Archive -LiteralPath $ArchivePath -DestinationPath $BundleDir -Force

  $PluginSource = Join-Path $BundleDir "plugin\LoomleBridge"
  $WorkspaceSource = Join-Path $BundleDir "Loomle"
  $PluginDestination = Join-Path $ProjectRoot "Plugins\LoomleBridge"
  $WorkspaceDestination = Join-Path $ProjectRoot "Loomle"
  $LauncherPath = Join-Path $WorkspaceDestination "loomle.exe"
  $ActiveClientPath = Join-Path $WorkspaceDestination "install\versions\$EffectiveVersion\$ActiveClientName"
  $ActiveStatePath = Join-Path $WorkspaceDestination "install\active.json"
  $SettingsPath = Join-Path $ProjectRoot "Config\DefaultEditorSettings.ini"

  if (-not (Test-Path -LiteralPath $PluginSource)) { Fail "bundle missing plugin/LoomleBridge" }
  if (-not (Test-Path -LiteralPath $WorkspaceSource)) { Fail "bundle missing Loomle" }

  Copy-TreeReplace -Source $PluginSource -Destination $PluginDestination
  Sync-WorkspaceEntries -SourceRoot $WorkspaceSource -DestinationRoot $WorkspaceDestination
  Ensure-WorkspaceLayout -WorkspaceRoot $WorkspaceDestination
  Copy-VersionedPayload -WorkspaceSource $WorkspaceSource -WorkspaceRoot $WorkspaceDestination -Version $EffectiveVersion -SourceClientName "loomle.exe" -TargetClientName $ActiveClientName
  Copy-ManifestRecord -ManifestPath $ManifestPath -WorkspaceRoot $WorkspaceDestination -Version $EffectiveVersion

  if (-not (Test-Path -LiteralPath $LauncherPath)) { Fail "installed client missing: $LauncherPath" }

  Ensure-IniSetting -Path $SettingsPath -Section $EditorPerfSection -Setting $EditorThrottleSetting
  Write-ActiveState `
    -ActiveStatePath $ActiveStatePath `
    -Version $EffectiveVersion `
    -Platform "windows" `
    -ProjectRoot $ProjectRoot `
    -PluginRoot $PluginDestination `
    -WorkspaceRoot $WorkspaceDestination `
    -LauncherPath $LauncherPath `
    -ActiveClientPath $ActiveClientPath `
    -SettingsPath $SettingsPath

  [pscustomobject]@{
    installedVersion = $EffectiveVersion
    activeVersion = $EffectiveVersion
    platform = "windows"
    bundleRoot = $BundleDir
    projectRoot = $ProjectRoot
    plugin = [pscustomobject]@{
      source = $PluginSource
      destination = $PluginDestination
    }
    workspace = [pscustomobject]@{
      source = $WorkspaceSource
      destination = $WorkspaceDestination
    }
    install = [pscustomobject]@{
      activeState = $ActiveStatePath
      activeClientPath = $ActiveClientPath
    }
  } | ConvertTo-Json -Depth 5
}
finally {
  Remove-Item -LiteralPath $TmpDir -Recurse -Force -ErrorAction SilentlyContinue
}
