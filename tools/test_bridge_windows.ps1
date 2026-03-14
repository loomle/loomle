param(
    [string]$ProjectRoot = "",

    [string]$Python = "python",
    [switch]$SkipSmoke,
    [switch]$SkipRegression
)

$ErrorActionPreference = "Stop"

function Step([string]$Message) {
    Write-Host "[STEP] $Message"
}

function Run-Cmd([string]$CmdLine) {
    Write-Host "[RUN]  $CmdLine"
    & cmd.exe /c $CmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit=$LASTEXITCODE): $CmdLine"
    }
}

if (-not (Test-Path -LiteralPath $ProjectRoot)) {
    $devConfig = Join-Path $PSScriptRoot "dev.project-root.local.json"
    if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
        if (-not (Test-Path -LiteralPath $devConfig)) {
            throw "Missing -ProjectRoot and dev config not found: $devConfig"
        }
        $raw = Get-Content -LiteralPath $devConfig -Raw
        $json = $raw | ConvertFrom-Json
        $ProjectRoot = [string]$json.project_root
    }
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot) -or -not (Test-Path -LiteralPath $ProjectRoot)) {
    throw "Project root not found: $ProjectRoot"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$smoke = Join-Path $repoRoot "tools\test_bridge_smoke.py"
$regression = Join-Path $repoRoot "tools\test_bridge_regression.py"
$serverOut = Join-Path $repoRoot "mcp\server\target\release\loomle_mcp_server.exe"
$pluginServer = Join-Path $ProjectRoot "Plugins\LoomleBridge\Tools\mcp\windows\loomle_mcp_server.exe"

if (-not (Test-Path -LiteralPath $smoke)) {
    throw "Missing script: $smoke"
}
if (-not (Test-Path -LiteralPath $regression)) {
    throw "Missing script: $regression"
}

Step "Run Rust tests"
Run-Cmd "cd /d \"$repoRoot\mcp\server\" && cargo test"

Step "Build MCP server (release) and sync into plugin path"
Run-Cmd "cd /d \"$repoRoot\mcp\server\" && cargo build --release"
if (-not (Test-Path -LiteralPath $serverOut)) {
    throw "Missing built MCP server binary: $serverOut"
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $pluginServer) | Out-Null
Copy-Item -LiteralPath $serverOut -Destination $pluginServer -Force

if (-not $SkipSmoke) {
    Step "Run bridge smoke test"
    Run-Cmd "$Python \"$smoke\" --project-root \"$ProjectRoot\""
}

if (-not $SkipRegression) {
    Step "Run bridge regression test"
    Run-Cmd "$Python \"$regression\" --project-root \"$ProjectRoot\""
}

Write-Host "[PASS] Windows bridge tests complete"
