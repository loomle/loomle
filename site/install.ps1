$ErrorActionPreference = "Stop"

$ReleaseRepo = if ($env:LOOMLE_RELEASE_REPO) { $env:LOOMLE_RELEASE_REPO } else { "loomle/loomle" }
$Version = if ($env:LOOMLE_BOOTSTRAP_VERSION) { $env:LOOMLE_BOOTSTRAP_VERSION } else { "latest" }
$InstallDir = if ($env:LOOMLE_INSTALL_DIR) { $env:LOOMLE_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA "Programs\\Loomle\\bin" }
$ReleaseTag = if ($Version -eq "latest") { "loomle-latest" } elseif ($Version.StartsWith("v")) { $Version } else { "v$Version" }
$CliName = "loomle.exe"
$Platform = "windows"
$DownloadUrl = "https://github.com/$ReleaseRepo/releases/download/$ReleaseTag/$CliName"
$TargetPath = Join-Path $InstallDir $CliName
$TempPath = Join-Path ([System.IO.Path]::GetTempPath()) ("loomle-bootstrap-" + [System.Guid]::NewGuid().ToString("N") + ".exe")

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Write-Host "[loomle-bootstrap] downloading $DownloadUrl"
Invoke-WebRequest -Uri $DownloadUrl -OutFile $TempPath
Move-Item -Force $TempPath $TargetPath

Write-Host "[loomle-bootstrap] installed $TargetPath"
Write-Host "[loomle-bootstrap] next step:"
Write-Host "  loomle install --project-root C:\\Path\\To\\MyProject"
Write-Host "[loomle-bootstrap] if the Unreal Engine for this project is source-built, use:"
Write-Host "  loomle install --project-root C:\\Path\\To\\MyProject --plugin-mode source"
