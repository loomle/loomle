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
$clientOut = Join-Path $repoRoot "client\target\release\loomle.exe"
$pluginSrc = Join-Path $repoRoot "engine\LoomleBridge"
$pluginDst = Join-Path $ProjectRoot "Plugins\LoomleBridge"

if (-not (Test-Path -LiteralPath $smoke)) {
    throw "Missing script: $smoke"
}
if (-not (Test-Path -LiteralPath $regression)) {
    throw "Missing script: $regression"
}

Step "Run Rust tests"
Run-Cmd ('cd /d "{0}" && cargo test' -f (Join-Path $repoRoot "client"))

Step "Build LOOMLE client (release) and sync LoomleBridge into project"
Run-Cmd ('cd /d "{0}" && cargo build --release' -f (Join-Path $repoRoot "client"))
if (-not (Test-Path -LiteralPath $clientOut)) {
    throw "Missing built LOOMLE client binary: $clientOut"
}
if (-not (Test-Path -LiteralPath $pluginSrc)) {
    throw "Missing plugin source: $pluginSrc"
}
if (Test-Path -LiteralPath $pluginDst) {
    Remove-Item -LiteralPath $pluginDst -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $pluginDst) | Out-Null
Copy-Item -LiteralPath $pluginSrc -Destination $pluginDst -Recurse -Force

if (-not $SkipSmoke) {
    Step "Run bridge smoke test"
    Run-Cmd ('"{0}" "{1}" --project-root "{2}" --loomle-bin "{3}"' -f $Python, $smoke, $ProjectRoot, $clientOut)
}

if (-not $SkipRegression) {
    Step "Run bridge regression test"
    Run-Cmd ('"{0}" "{1}" --project-root "{2}" --loomle-bin "{3}"' -f $Python, $regression, $ProjectRoot, $clientOut)
}

Write-Host "[PASS] Windows bridge tests complete"
