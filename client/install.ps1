$ErrorActionPreference = "Stop"

$ReleaseRepo = if ($env:LOOMLE_RELEASE_REPO) { $env:LOOMLE_RELEASE_REPO } else { "loomle/loomle" }
$RequestedVersion = if ($env:LOOMLE_BOOTSTRAP_VERSION) { $env:LOOMLE_BOOTSTRAP_VERSION } else { "latest" }

function Fail([string]$Message) {
  Write-Error $Message
  exit 1
}

function Resolve-ReleaseTag([string]$Version) {
  if ($Version -eq "latest") { return "loomle-latest" }
  if ($Version.StartsWith("v")) { return $Version }
  return "v$Version"
}

function Resolve-PythonCommand() {
  $py = Get-Command py -ErrorAction SilentlyContinue
  if ($py) { return @("py", "-3") }
  $python = Get-Command python -ErrorAction SilentlyContinue
  if ($python) { return @("python") }
  Fail "python is required"
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
    "--help" { Write-Host "Usage: install.ps1 [--project-root <ProjectRoot>] [--version <Version>]"; exit 0 }
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

$ReleaseTag = Resolve-ReleaseTag $RequestedVersion
if ([string]::IsNullOrWhiteSpace($ManifestUrl)) {
  $ManifestUrl = "https://github.com/$ReleaseRepo/releases/download/$ReleaseTag/loomle-manifest-windows.json"
}

$PythonCommand = Resolve-PythonCommand
$TmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("loomle-install-" + [System.Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $TmpDir -Force
$ManifestPath = Join-Path $TmpDir "manifest.json"
$ArchivePath = Join-Path $TmpDir "loomle-windows.zip"
$BundleDir = Join-Path $TmpDir "bundle"

try {
  Invoke-WebRequest -Uri $ManifestUrl -OutFile $ManifestPath

  if ([string]::IsNullOrWhiteSpace($AssetUrl)) {
    $AssetUrl = & $PythonCommand[0] @($PythonCommand[1..($PythonCommand.Length-1)]) -c @'
import json, sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
requested = sys.argv[2]
version = manifest.get("latest") if requested == "latest" else requested.removeprefix("v")
package = manifest.get("versions", {}).get(version, {}).get("packages", {}).get("windows")
if not isinstance(package, dict):
    raise SystemExit(1)
print(package["url"])
'@ $ManifestPath $RequestedVersion
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($AssetUrl)) {
      Fail "failed to resolve asset URL from manifest"
    }
  }

  Invoke-WebRequest -Uri $AssetUrl -OutFile $ArchivePath
  Expand-Archive -LiteralPath $ArchivePath -DestinationPath $BundleDir -Force

  $HelperPath = Join-Path $BundleDir "workspace\Loomle\runtime\install_release.py"
  if (-not (Test-Path -LiteralPath $HelperPath)) {
    Fail "bundle missing install helper: $HelperPath"
  }

  & $PythonCommand[0] @($PythonCommand[1..($PythonCommand.Length-1)]) $HelperPath `
    --bundle-root $BundleDir `
    --project-root $ProjectRoot `
    --manifest-path $ManifestPath `
    --platform windows `
    --version $RequestedVersion.TrimStart('v')
  exit $LASTEXITCODE
}
finally {
  Remove-Item -LiteralPath $TmpDir -Recurse -Force -ErrorAction SilentlyContinue
}
