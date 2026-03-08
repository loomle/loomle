param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectRoot,

    [string]$ProjectName = "LoomleRunnerHost",
    [string]$EngineAssociation = "5.7"
)

$ErrorActionPreference = "Stop"

$projectDescription = "Dedicated isolated Unreal project for LOOMLE runner verification."
$uprojectPath = Join-Path $ProjectRoot "$ProjectName.uproject"
$configDir = Join-Path $ProjectRoot "Config"
$contentDir = Join-Path $ProjectRoot "Content"
$pluginsDir = Join-Path $ProjectRoot "Plugins"
$savedDir = Join-Path $ProjectRoot "Saved"
$intermediateDir = Join-Path $ProjectRoot "Intermediate"

New-Item -ItemType Directory -Force -Path $configDir, $contentDir, $pluginsDir, $savedDir, $intermediateDir | Out-Null

if (-not (Test-Path -LiteralPath $uprojectPath)) {
    $uproject = @"
{
  "FileVersion": 3,
  "EngineAssociation": "$EngineAssociation",
  "Category": "",
  "Description": "$projectDescription",
  "Plugins": [
    {
      "Name": "LoomleBridge",
      "Enabled": true,
      "TargetAllowList": [
        "Editor"
      ]
    }
  ]
}
"@
    Set-Content -LiteralPath $uprojectPath -Value $uproject -Encoding utf8NoBOM
}

$defaultEditorSettings = @"
[/Script/UnrealEd.EditorPerformanceSettings]
bThrottleCPUWhenNotForeground=False
"@
Set-Content -LiteralPath (Join-Path $configDir "DefaultEditorSettings.ini") -Value $defaultEditorSettings -Encoding utf8NoBOM

$defaultGame = @"
[/Script/EngineSettings.GeneralProjectSettings]
ProjectName=$ProjectName
ProjectDisplayedTitle=NSLOCTEXT("[/Script/EngineSettings]", "2B2B3FD947D842D78CFD60BF57A6B537", "$ProjectName")
ProjectDebugTitleInfo=NSLOCTEXT("[/Script/EngineSettings]", "D0092C914D6E40B4BBDB584F8098BA20", "$ProjectName")
ProjectID=8F90A338441B4774861DDEDE1005B8F9
Description=$projectDescription
"@
Set-Content -LiteralPath (Join-Path $configDir "DefaultGame.ini") -Value $defaultGame -Encoding utf8NoBOM

$defaultEngine = @"
[/Script/EngineSettings.GameMapsSettings]
EditorStartupMap=/Engine/Maps/Entry
LocalMapOptions=
TransitionMap=
bUseSplitscreen=True
GameInstanceClass=/Script/Engine.GameInstance
GameDefaultMap=/Engine/Maps/Entry
ServerDefaultMap=/Engine/Maps/Entry
GlobalDefaultGameMode=/Script/Engine.GameModeBase
GlobalDefaultServerGameMode=None

[/Script/HardwareTargeting.HardwareTargetingSettings]
TargetedHardwareClass=Desktop
AppliedTargetedHardwareClass=Desktop
DefaultGraphicsPerformance=Scalable
AppliedDefaultGraphicsPerformance=Scalable
"@
Set-Content -LiteralPath (Join-Path $configDir "DefaultEngine.ini") -Value $defaultEngine -Encoding utf8NoBOM

$defaultEditor = @"
[UnrealEd.SimpleMap]
SimpleMapName=/Engine/Maps/Entry
"@
Set-Content -LiteralPath (Join-Path $configDir "DefaultEditor.ini") -Value $defaultEditor -Encoding utf8NoBOM

Write-Output "READY:$ProjectRoot"
