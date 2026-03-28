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
  if ($Version -eq "latest") { return "loomle-latest" }
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

function Install-Directory([string]$Source, [string]$Destination) {
  if (-not (Test-Path -LiteralPath $Source)) {
    Fail "install source not found: $Source"
  }
  Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
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

function Write-InstallState(
  [string]$InstallStatePath,
  [string]$Version,
  [string]$Platform,
  [string]$ProjectRoot,
  [string]$PluginRoot,
  [string]$WorkspaceRoot,
  [string]$ClientPath,
  [string]$SettingsPath
) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $InstallStatePath) | Out-Null
  $payload = [ordered]@{
    schemaVersion = 1
    installedVersion = $Version
    platform = $Platform
    projectRoot = $ProjectRoot
    workspaceRoot = $WorkspaceRoot
    pluginRoot = $PluginRoot
    clientPath = $ClientPath
    editorPerformance = [ordered]@{
      settingsFile = $SettingsPath
      throttleWhenNotForeground = $false
    }
  }
  $payload | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $InstallStatePath -Encoding UTF8
}

$ProjectRoot = ""
$ManifestUrl = ""
$AssetUrl = ""
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

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
    "--help" { Write-Host "Usage: update.ps1 [--project-root <ProjectRoot>] [--version <Version>] [--manifest-url <URL>] [--asset-url <URL>]"; exit 0 }
    default { Fail "unknown argument: $($args[$i])" }
  }
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
  $ProjectRoot = Find-ProjectRoot (Join-Path $ScriptDir "..")
}
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
  $ProjectRoot = Find-ProjectRoot (Get-Location).Path
}
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
  Fail "could not resolve Unreal project root; pass --project-root"
}
$ProjectRoot = (Resolve-Path $ProjectRoot).Path

$ReleaseTag = Resolve-ReleaseTag $RequestedVersion
if ([string]::IsNullOrWhiteSpace($ManifestUrl)) {
  $ManifestUrl = "https://github.com/$ReleaseRepo/releases/download/$ReleaseTag/loomle-manifest-windows.json"
}
if ([string]::IsNullOrWhiteSpace($AssetUrl)) {
  $AssetUrl = "https://github.com/$ReleaseRepo/releases/download/$ReleaseTag/loomle-windows.zip"
}

$TmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("loomle-update-" + [System.Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $TmpDir -Force
$ManifestPath = Join-Path $TmpDir "manifest.json"
$ArchivePath = Join-Path $TmpDir "loomle-windows.zip"
$BundleDir = Join-Path $TmpDir "bundle"

try {
  Invoke-WebRequest -Uri $ManifestUrl -OutFile $ManifestPath
  $EffectiveVersion = Resolve-EffectiveVersion -ManifestPath $ManifestPath -Version $RequestedVersion

  Invoke-WebRequest -Uri $AssetUrl -OutFile $ArchivePath
  Expand-Archive -LiteralPath $ArchivePath -DestinationPath $BundleDir -Force

  $PluginSource = Join-Path $BundleDir "plugin\LoomleBridge"
  $WorkspaceSource = Join-Path $BundleDir "Loomle"
  $PluginDestination = Join-Path $ProjectRoot "Plugins\LoomleBridge"
  $WorkspaceDestination = Join-Path $ProjectRoot "Loomle"
  $ClientPath = Join-Path $WorkspaceDestination "loomle.exe"
  $InstallStatePath = Join-Path $WorkspaceDestination "runtime\install.json"
  $SettingsPath = Join-Path $ProjectRoot "Config\DefaultEditorSettings.ini"

  if (-not (Test-Path -LiteralPath $PluginSource)) { Fail "bundle missing plugin/LoomleBridge" }
  if (-not (Test-Path -LiteralPath $WorkspaceSource)) { Fail "bundle missing Loomle" }

  Install-Directory -Source $PluginSource -Destination $PluginDestination
  Install-Directory -Source $WorkspaceSource -Destination $WorkspaceDestination

  if (-not (Test-Path -LiteralPath $ClientPath)) { Fail "installed client missing: $ClientPath" }

  Ensure-IniSetting -Path $SettingsPath -Section $EditorPerfSection -Setting $EditorThrottleSetting
  Write-InstallState `
    -InstallStatePath $InstallStatePath `
    -Version $EffectiveVersion `
    -Platform "windows" `
    -ProjectRoot $ProjectRoot `
    -PluginRoot $PluginDestination `
    -WorkspaceRoot $WorkspaceDestination `
    -ClientPath $ClientPath `
    -SettingsPath $SettingsPath

  [pscustomobject]@{
    installedVersion = $EffectiveVersion
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
    runtime = [pscustomobject]@{
      installState = $InstallStatePath
    }
  } | ConvertTo-Json -Depth 4
}
finally {
  Remove-Item -LiteralPath $TmpDir -Recurse -Force -ErrorAction SilentlyContinue
}
