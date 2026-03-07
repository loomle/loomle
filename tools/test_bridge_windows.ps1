param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectRoot,

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
    throw "Project root not found: $ProjectRoot"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$smoke = Join-Path $repoRoot "tools\test_bridge_smoke.py"
$regression = Join-Path $repoRoot "tools\test_bridge_regression.py"
$mcpCargo = Join-Path $repoRoot "mcp_server\Cargo.toml"

if (-not (Test-Path -LiteralPath $smoke)) {
    throw "Missing script: $smoke"
}
if (-not (Test-Path -LiteralPath $regression)) {
    throw "Missing script: $regression"
}
if (-not (Test-Path -LiteralPath $mcpCargo)) {
    throw "Missing mcp manifest: $mcpCargo"
}

Step "Run Rust tests"
Run-Cmd "cd /d \"$repoRoot\mcp_server\" && cargo test"

if (-not $SkipSmoke) {
    Step "Run bridge smoke test"
    Run-Cmd "$Python \"$smoke\" --project-root \"$ProjectRoot\" --mcp-manifest \"$mcpCargo\""
}

if (-not $SkipRegression) {
    Step "Run bridge regression test"
    Run-Cmd "$Python \"$regression\" --project-root \"$ProjectRoot\" --mcp-manifest \"$mcpCargo\""
}

Write-Host "[PASS] Windows bridge tests complete"
