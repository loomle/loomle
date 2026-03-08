#!/usr/bin/env bash
set -euo pipefail

project_root="${1:-}"
if [[ -z "$project_root" ]]; then
  echo "usage: $0 <project-root>" >&2
  exit 1
fi

project_name="LoomleRunnerHost"
project_description="Dedicated isolated Unreal project for Loomle runner verification."
uproject_path="$project_root/${project_name}.uproject"
config_dir="$project_root/Config"
content_dir="$project_root/Content"
plugins_dir="$project_root/Plugins"
saved_dir="$project_root/Saved"
intermediate_dir="$project_root/Intermediate"

mkdir -p "$config_dir" "$content_dir" "$plugins_dir" "$saved_dir" "$intermediate_dir"

if [[ ! -f "$uproject_path" ]]; then
  cat > "$uproject_path" <<JSON
{
  "FileVersion": 3,
  "EngineAssociation": "5.7",
  "Category": "",
  "Description": "$project_description",
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
JSON
fi

cat > "$config_dir/DefaultEditorSettings.ini" <<'EOF_EDITOR_SETTINGS'
[/Script/UnrealEd.EditorPerformanceSettings]
bThrottleCPUWhenNotForeground=False
EOF_EDITOR_SETTINGS

cat > "$config_dir/DefaultGame.ini" <<EOF_GAME
[/Script/EngineSettings.GeneralProjectSettings]
ProjectName=$project_name
ProjectDisplayedTitle=NSLOCTEXT("[/Script/EngineSettings]", "2B2B3FD947D842D78CFD60BF57A6B537", "$project_name")
ProjectDebugTitleInfo=NSLOCTEXT("[/Script/EngineSettings]", "D0092C914D6E40B4BBDB584F8098BA20", "$project_name")
ProjectID=8F90A338441B4774861DDEDE1005B8F9
Description=$project_description
EOF_GAME

cat > "$config_dir/DefaultEngine.ini" <<'EOF_ENGINE'
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

[/Script/MacTargetPlatform.MacTargetSettings]
-TargetedRHIs=SF_METAL_SM5
+TargetedRHIs=SF_METAL_SM6
EOF_ENGINE

cat > "$config_dir/DefaultEditor.ini" <<'EOF_EDITOR'
[UnrealEd.SimpleMap]
SimpleMapName=/Engine/Maps/Entry
EOF_EDITOR

echo "READY:$project_root"
