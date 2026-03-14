$ErrorActionPreference = "Stop"

$BaseUrl = if ($env:LOOMLE_BOOTSTRAP_BASE_URL) { $env:LOOMLE_BOOTSTRAP_BASE_URL } else { "https://loomle.ai" }
$Version = if ($env:LOOMLE_BOOTSTRAP_VERSION) { $env:LOOMLE_BOOTSTRAP_VERSION } else { "latest" }
$InstallDir = if ($env:LOOMLE_INSTALL_DIR) { $env:LOOMLE_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA "Programs\\Loomle\\bin" }
$CliName = "loomle.exe"
$Platform = "windows"
$DownloadUrl = "$BaseUrl/downloads/bootstrap/$Version/$Platform/$CliName"
$TargetPath = Join-Path $InstallDir $CliName
$TempPath = Join-Path ([System.IO.Path]::GetTempPath()) ("loomle-bootstrap-" + [System.Guid]::NewGuid().ToString("N") + ".exe")

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Write-Host "[loomle-bootstrap] downloading $DownloadUrl"
Invoke-WebRequest -Uri $DownloadUrl -OutFile $TempPath
Move-Item -Force $TempPath $TargetPath

Write-Host "[loomle-bootstrap] installed $TargetPath"
Write-Host "[loomle-bootstrap] next step:"
Write-Host "  loomle install --project-root C:\\Path\\To\\MyProject"
