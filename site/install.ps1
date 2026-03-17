$ErrorActionPreference = "Stop"

$ReleaseRepo = if ($env:LOOMLE_RELEASE_REPO) { $env:LOOMLE_RELEASE_REPO } else { "loomle/loomle" }
$Version = if ($env:LOOMLE_BOOTSTRAP_VERSION) { $env:LOOMLE_BOOTSTRAP_VERSION } else { "latest" }
$ReleaseTag = if ($Version -eq "latest") { "loomle-latest" } elseif ($Version.StartsWith("v")) { $Version } else { "v$Version" }
$CliName = "loomle-installer.exe"
$Platform = "windows"
$DownloadUrl = "https://github.com/$ReleaseRepo/releases/download/$ReleaseTag/$CliName"
$TempPath = Join-Path ([System.IO.Path]::GetTempPath()) ("loomle-bootstrap-" + [System.Guid]::NewGuid().ToString("N") + ".exe")

Write-Host "[loomle-bootstrap] downloading $DownloadUrl"
Invoke-WebRequest -Uri $DownloadUrl -OutFile $TempPath
if ($args.Count -eq 0) {
  Write-Host "[loomle-bootstrap] downloaded temporary installer $TempPath"
  Write-Host "[loomle-bootstrap] usage example:"
  Write-Host '  & ([scriptblock]::Create((irm https://loomle.ai/install.ps1))) install --project-root C:\Path\To\MyProject'
  Write-Host "[loomle-bootstrap] no installer arguments supplied; deleting temporary installer"
  Remove-Item -LiteralPath $TempPath -Force -ErrorAction SilentlyContinue
  exit 2
}

try {
  & $TempPath @args
  $exitCode = $LASTEXITCODE
} finally {
  Remove-Item -LiteralPath $TempPath -Force -ErrorAction SilentlyContinue
}

exit $exitCode
