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
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
  if (Test-Path -LiteralPath $Destination) {
    $OldPath = "$Destination.old"
    Remove-Item -LiteralPath $OldPath -Force -ErrorAction SilentlyContinue
    Rename-Item -LiteralPath $Destination -NewName (Split-Path -Leaf $OldPath) -Force
  }
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

function ConvertTo-TomlBasicString([string]$Value) {
  return $Value.Replace('\', '\\').Replace('"', '\"')
}

function Set-CodexMcpConfig([string]$LauncherPath) {
  $manual = "codex mcp add loomle -- `"$LauncherPath`" mcp"
  $codexDir = Join-Path $HOME ".codex"
  $configPath = Join-Path $codexDir "config.toml"
  if (-not (Test-Path -LiteralPath $codexDir -PathType Container) -and -not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    return [pscustomobject]@{
      status = "not configured"
      detail = "Codex config directory not found"
      manual = $manual
    }
  }

  New-Item -ItemType Directory -Force -Path $codexDir | Out-Null
  $text = if (Test-Path -LiteralPath $configPath -PathType Leaf) {
    Get-Content -LiteralPath $configPath -Raw
  } else {
    ""
  }
  $text = [regex]::Replace($text, '(?ms)^\[mcp_servers\.loomle\]\r?\n.*?(?=^\[|\z)', '')
  $escapedLauncher = ConvertTo-TomlBasicString $LauncherPath
  $section = "`n[mcp_servers.loomle]`ncommand = `"$escapedLauncher`"`nargs = [`"mcp`"]`n"
  try {
    ($text.TrimEnd() + $section) | Set-Content -LiteralPath $configPath -Encoding UTF8
    return [pscustomobject]@{
      status = "configured"
      detail = $configPath
      manual = $manual
    }
  } catch {
    return [pscustomobject]@{
      status = "warning"
      detail = "failed to write ${configPath}: $($_.Exception.Message)"
      manual = $manual
    }
  }
}

function Set-ClaudeMcpConfig([string]$LauncherPath) {
  $manual = "claude mcp add --scope user loomle -- `"$LauncherPath`" mcp"
  $claude = Get-Command claude -ErrorAction SilentlyContinue
  if (-not $claude) {
    return [pscustomobject]@{
      status = "not configured"
      detail = "Claude CLI not found"
      manual = $manual
    }
  }

  try {
    & $claude.Source mcp remove --scope user loomle 2>$null | Out-Null
    & $claude.Source mcp add --scope user loomle -- $LauncherPath mcp | Out-Null
    if ($LASTEXITCODE -ne 0) {
      throw "claude mcp add exited with code $LASTEXITCODE"
    }
    return [pscustomobject]@{
      status = "configured"
      detail = "Claude user MCP config"
      manual = $manual
    }
  } catch {
    return [pscustomobject]@{
      status = "warning"
      detail = "failed to update Claude MCP config: $($_.Exception.Message)"
      manual = $manual
    }
  }
}

function Write-InstallSummary(
  [string]$EffectiveVersion,
  [string]$InstallRoot,
  [string]$LauncherPath,
  [string]$ActiveClientPath,
  [string]$PluginCache,
  $CodexMcp,
  $ClaudeMcp
) {
  $anyConfigured = $CodexMcp.status -eq "configured" -or $ClaudeMcp.status -eq "configured"
  $anyNotConfigured = $CodexMcp.status -eq "not configured" -or $ClaudeMcp.status -eq "not configured"
  $anyWarning = $CodexMcp.status -eq "warning" -or $ClaudeMcp.status -eq "warning"

  Write-Host ""
  Write-Host "LOOMLE installed successfully."
  Write-Host ""
  Write-Host "Installed:"
  Write-Host "  - LOOMLE client: $LauncherPath"
  Write-Host "  - Active client payload: $ActiveClientPath"
  Write-Host "  - LoomleBridge plugin cache: $PluginCache"
  Write-Host "  - Install root: $InstallRoot"
  Write-Host "  - Version: $EffectiveVersion (windows)"
  Write-Host ""
  Write-Host "MCP configuration:"
  Write-Host "  - Codex: $($CodexMcp.status) ($($CodexMcp.detail))"
  Write-Host "  - Claude: $($ClaudeMcp.status) ($($ClaudeMcp.detail))"
  Write-Host ""
  Write-Host "Next steps:"
  if ($anyConfigured) {
    Write-Host "  - Restart your Codex or Claude agent session so the updated MCP configuration is loaded."
  }
  if ($anyNotConfigured) {
    if ($CodexMcp.status -eq "not configured") {
      Write-Host "  - To configure Codex manually: $($CodexMcp.manual)"
    }
    if ($ClaudeMcp.status -eq "not configured") {
      Write-Host "  - To configure Claude manually: $($ClaudeMcp.manual)"
    }
  }
  if ($anyWarning) {
    Write-Host "  - Review the MCP configuration warning above and rerun the installer after fixing it."
  }
}

function Split-PathEntries([string]$PathValue) {
  if ([string]::IsNullOrWhiteSpace($PathValue)) { return @() }
  return $PathValue -split [System.IO.Path]::PathSeparator | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
}

function Test-PathEntryExists([string[]]$Entries, [string]$Target) {
  foreach ($entry in $Entries) {
    if ([string]::Equals($entry.TrimEnd('\'), $Target.TrimEnd('\'), [System.StringComparison]::OrdinalIgnoreCase)) {
      return $true
    }
  }
  return $false
}

function Ensure-PathEntry([string]$BinDir) {
  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $userEntries = @(Split-PathEntries $userPath)
  if (Test-PathEntryExists -Entries $userEntries -Target $BinDir) {
    Write-Host "[loomle-install] User PATH already includes $BinDir"
  } else {
    $newUserPath = if ([string]::IsNullOrWhiteSpace($userPath)) {
      $BinDir
    } else {
      "$userPath$([System.IO.Path]::PathSeparator)$BinDir"
    }
    [Environment]::SetEnvironmentVariable("Path", $newUserPath, "User")
    Write-Host "[loomle-install] added $BinDir to User PATH"
  }

  $processEntries = @(Split-PathEntries $env:Path)
  if (-not (Test-PathEntryExists -Entries $processEntries -Target $BinDir)) {
    $env:Path = "$BinDir$([System.IO.Path]::PathSeparator)$env:Path"
  }
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

  Remove-Item -LiteralPath "$LauncherPath.old" -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath "$ActiveClientPath.old" -Force -ErrorAction SilentlyContinue

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
  Ensure-PathEntry -BinDir (Join-Path $InstallRoot "bin")
  $CodexMcp = Set-CodexMcpConfig -LauncherPath $LauncherPath
  $ClaudeMcp = Set-ClaudeMcpConfig -LauncherPath $LauncherPath

  Write-InstallSummary `
    -EffectiveVersion $EffectiveVersion `
    -InstallRoot $InstallRoot `
    -LauncherPath $LauncherPath `
    -ActiveClientPath $ActiveClientPath `
    -PluginCache (Join-Path $VersionRoot "plugin-cache\LoomleBridge") `
    -CodexMcp $CodexMcp `
    -ClaudeMcp $ClaudeMcp
}
finally {
  Remove-Item -LiteralPath $TmpDir -Recurse -Force -ErrorAction SilentlyContinue
}
