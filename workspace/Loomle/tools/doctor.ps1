$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir "..\\..")).Path
$ClientPath = Join-Path $ProjectRoot "Loomle\\client\\loomle.exe"

& $ClientPath doctor --project-root $ProjectRoot
