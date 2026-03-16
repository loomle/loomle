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

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not (Test-Path -LiteralPath $ProjectRoot)) {
    $devConfig = Join-Path $repoRoot "tools\dev.project-root.local.json"
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

$smoke = Join-Path $repoRoot "tests\e2e\test_bridge_smoke.py"
$regression = Join-Path $repoRoot "tests\e2e\test_bridge_regression.py"
$serverOut = Join-Path $repoRoot "mcp\server\target\release\loomle_mcp_server.exe"
$clientOut = Join-Path $repoRoot "mcp\client\target\release\loomle.exe"
$pluginServer = Join-Path $ProjectRoot "Plugins\LoomleBridge\Tools\mcp\windows\loomle_mcp_server.exe"
$workspaceSrc = Join-Path $repoRoot "workspace\Loomle"
$workspaceDst = Join-Path $ProjectRoot "Loomle"

if (-not (Test-Path -LiteralPath $smoke)) {
    throw "Missing script: $smoke"
}
if (-not (Test-Path -LiteralPath $regression)) {
    throw "Missing script: $regression"
}

Step "Run Rust tests"
Run-Cmd ('cd /d "{0}" && cargo test' -f (Join-Path $repoRoot "mcp\server"))
Run-Cmd ('cd /d "{0}" && cargo test' -f (Join-Path $repoRoot "mcp\client"))

Step "Build MCP binaries (release) and sync into project-local paths"
Run-Cmd ('cd /d "{0}" && cargo build --release' -f (Join-Path $repoRoot "mcp\server"))
Run-Cmd ('cd /d "{0}" && cargo build --release' -f (Join-Path $repoRoot "mcp\client"))
if (-not (Test-Path -LiteralPath $serverOut)) {
    throw "Missing built MCP server binary: $serverOut"
}
if (-not (Test-Path -LiteralPath $clientOut)) {
    throw "Missing built LOOMLE client binary: $clientOut"
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $pluginServer) | Out-Null
Copy-Item -LiteralPath $serverOut -Destination $pluginServer -Force
if (Test-Path -LiteralPath $workspaceDst) {
    Remove-Item -LiteralPath $workspaceDst -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $workspaceDst | Out-Null
Copy-Item -Path (Join-Path $workspaceSrc "*") -Destination $workspaceDst -Recurse -Force
Copy-Item -LiteralPath $clientOut -Destination (Join-Path $workspaceDst "loomle.exe") -Force

if (-not $SkipSmoke) {
    Step "Run bridge smoke test"
    Run-Cmd ('"{0}" "{1}" --project-root "{2}"' -f $Python, $smoke, $ProjectRoot)
}

if (-not $SkipRegression) {
    Step "Run bridge regression test"
    Run-Cmd ('"{0}" "{1}" --project-root "{2}"' -f $Python, $regression, $ProjectRoot)
}

Write-Host "[PASS] Windows bridge tests complete"
