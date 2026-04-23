$ErrorActionPreference = "Stop"

$ReleaseRepo = if ($env:LOOMLE_RELEASE_REPO) { $env:LOOMLE_RELEASE_REPO } else { "loomle/loomle" }
$RequestedVersion = if ($env:LOOMLE_BOOTSTRAP_VERSION) { $env:LOOMLE_BOOTSTRAP_VERSION } else { "latest" }

function Fail([string]$Message) {
  Write-Error $Message
  exit 1
}

function Download-File([string]$Uri, [string]$OutFile) {
  $attempts = 5
  for ($i = 1; $i -le $attempts; $i++) {
    try {
      Invoke-WebRequest -Uri $Uri -OutFile $OutFile -TimeoutSec 180
      return
    } catch {
      if ($i -eq $attempts) {
        throw
      }
      Start-Sleep -Seconds 2
    }
  }
}

function Resolve-ReleaseTag([string]$Version) {
  if ($Version.StartsWith("v")) { return $Version }
  return "v$Version"
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

function Copy-TreeReplace([string]$Source, [string]$Destination) {
  if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    Fail "install source not found: $Source"
  }
  Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
  Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

function Copy-FileReplace([string]$Source, [string]$Destination) {
  if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    Fail "install file not found: $Source"
  }
  Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
  Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Write-ActiveState(
  [string]$ActiveStatePath,
  [string]$Version,
  [string]$InstallRoot,
  [string]$LauncherPath,
  [string]$ActiveClientPath
) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ActiveStatePath) | Out-Null
  $payload = [ordered]@{
    schemaVersion = 2
    installedVersion = $Version
    activeVersion = $Version
    platform = "windows"
    installRoot = $InstallRoot
    launcherPath = $LauncherPath
    activeClientPath = $ActiveClientPath
    versionsRoot = (Join-Path $InstallRoot "versions")
    pluginCacheRoot = (Join-Path $InstallRoot "versions\$Version\plugin-cache")
  }
  $payload | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ActiveStatePath -Encoding UTF8
}

$ManifestUrl = ""
$AssetUrl = ""
$InstallRoot = if ($env:LOOMLE_INSTALL_ROOT) { $env:LOOMLE_INSTALL_ROOT } else { Join-Path $HOME ".loomle" }

for ($i = 0; $i -lt $args.Count; $i++) {
  switch ($args[$i]) {
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
    "--install-root" {
      $i++; if ($i -ge $args.Count) { Fail "missing value for --install-root" }
      $InstallRoot = $args[$i]
    }
    "--help" {
      Write-Host "Usage: install.ps1 [--version <Version>] [--manifest-url <URL>] [--asset-url <URL>] [--install-root <Path>]"
      exit 0
    }
    default { Fail "unknown argument: $($args[$i])" }
  }
}

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
$InstallRoot = (Resolve-Path $InstallRoot).Path

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
  Download-File -Uri $ManifestUrl -OutFile $ManifestPath
  $EffectiveVersion = Resolve-EffectiveVersion -ManifestPath $ManifestPath -Version $RequestedVersion

  Download-File -Uri $AssetUrl -OutFile $ArchivePath
  Expand-Archive -LiteralPath $ArchivePath -DestinationPath $BundleDir -Force

  $ClientName = "loomle.exe"
  $ClientSource = Join-Path $BundleDir $ClientName
  $PluginCacheSource = Join-Path $BundleDir "plugin-cache\LoomleBridge"
  $VersionRoot = Join-Path $InstallRoot "versions\$EffectiveVersion"
  $LauncherPath = Join-Path $InstallRoot "bin\$ClientName"
  $ActiveClientPath = Join-Path $VersionRoot $ClientName
  $ActiveStatePath = Join-Path $InstallRoot "install\active.json"

  if (-not (Test-Path -LiteralPath $ClientSource -PathType Leaf)) { Fail "bundle missing $ClientName" }
  if (-not (Test-Path -LiteralPath $PluginCacheSource -PathType Container)) { Fail "bundle missing plugin-cache/LoomleBridge" }

  foreach ($dir in @("bin", "install", "state\runtimes", "locks", "logs")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $InstallRoot $dir) | Out-Null
  }

  Copy-FileReplace -Source $ClientSource -Destination $ActiveClientPath
  Copy-FileReplace -Source $ClientSource -Destination $LauncherPath
  Copy-TreeReplace -Source $PluginCacheSource -Destination (Join-Path $VersionRoot "plugin-cache\LoomleBridge")
  Copy-FileReplace -Source $ManifestPath -Destination (Join-Path $VersionRoot "manifest.json")
  Write-ActiveState -ActiveStatePath $ActiveStatePath -Version $EffectiveVersion -InstallRoot $InstallRoot -LauncherPath $LauncherPath -ActiveClientPath $ActiveClientPath

  [pscustomobject]@{
    installedVersion = $EffectiveVersion
    activeVersion = $EffectiveVersion
    platform = "windows"
    installRoot = $InstallRoot
    launcherPath = $LauncherPath
    activeClientPath = $ActiveClientPath
    pluginCache = (Join-Path $VersionRoot "plugin-cache\LoomleBridge")
  } | ConvertTo-Json -Depth 5

  Write-Host ""
  Write-Host "Configure MCP hosts:"
  Write-Host "  Codex:  codex mcp add loomle -- $LauncherPath mcp"
  Write-Host "  Claude: claude mcp add loomle --scope user $LauncherPath mcp"
}
finally {
  Remove-Item -LiteralPath $TmpDir -Recurse -Force -ErrorAction SilentlyContinue
}
