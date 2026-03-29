$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
  Write-Error $Message
  exit 1
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

function Get-StableFnv1a64([byte[]]$Bytes) {
  [UInt64]$hash = 0xcbf29ce484222325
  foreach ($byte in $Bytes) {
    $hash = $hash -bxor [UInt64]$byte
    $hash = [UInt64](($hash * 0x100000001b3) -band 0xFFFFFFFFFFFFFFFF)
  }
  return $hash
}

function Get-RuntimePipeName([string]$ProjectRoot) {
  $normalized = $ProjectRoot.Replace('\', '/').ToLowerInvariant().TrimEnd('/')
  if ([string]::IsNullOrWhiteSpace($normalized)) {
    $normalized = "/"
  }
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($normalized)
  $hash = Get-StableFnv1a64 $bytes
  return ("\\.\pipe\loomle-{0:x16}" -f $hash)
}

$ProjectRoot = ""
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

for ($i = 0; $i -lt $args.Count; $i++) {
  switch ($args[$i]) {
    "--project-root" {
      $i++; if ($i -ge $args.Count) { Fail "missing value for --project-root" }
      $ProjectRoot = $args[$i]
    }
    "--help" { Write-Host "Usage: doctor.ps1 [--project-root <ProjectRoot>]"; exit 0 }
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

$PluginRoot = Join-Path $ProjectRoot "Plugins\LoomleBridge"
$ClientPath = Join-Path $ProjectRoot "Loomle\loomle.exe"
$InstallState = Join-Path $ProjectRoot "Loomle\install\active.json"
$PipeName = Get-RuntimePipeName $ProjectRoot

$InstallOk = (Test-Path -LiteralPath $PluginRoot) -and (Test-Path -LiteralPath $ClientPath) -and (Test-Path -LiteralPath $InstallState)
$RuntimeReady = $false

try {
  $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", $PipeName.Substring(9), [System.IO.Pipes.PipeDirection]::InOut)
  $pipe.Connect(250)
  $pipe.Dispose()
  $RuntimeReady = $true
} catch {
  $RuntimeReady = $false
}

[pscustomobject]@{
  projectRoot = $ProjectRoot
  pluginRoot = $PluginRoot
  clientPath = $ClientPath
  installState = $InstallState
  runtimeEndpoint = $PipeName
  installOk = $InstallOk
  runtimeReady = $RuntimeReady
} | ConvertTo-Json -Depth 4
