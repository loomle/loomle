[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipLaunch,
    [switch]$SkipVerify,
    [switch]$ForceBuild,
    [string]$EngineRoot = 'D:\UE_5.7'
)

$ErrorActionPreference = 'Stop'

function Log([string]$Message) { Write-Host "[INFO] $Message" }
function Pass([string]$Message) { Write-Host "[PASS] $Message" }
function Fail([string]$Message) { throw "[FAIL] $Message" }

function Invoke-Cmd {
    param(
        [Parameter(Mandatory = $true)][string]$CommandLine,
        [string]$WorkingDirectory
    )

    $saved = Get-Location
    try {
        if ($WorkingDirectory) {
            Set-Location -Path $WorkingDirectory
        }
        & cmd.exe /c $CommandLine
        if ($LASTEXITCODE -ne 0) {
            Fail "Command failed with exit code ${LASTEXITCODE}: $CommandLine"
        }
    }
    finally {
        Set-Location -Path $saved
    }
}

function Convert-ToBool([object]$Value) {
    if ($null -eq $Value) { return $false }
    if ($Value -is [bool]) { return $Value }
    return [bool]$Value
}

function Read-JsonFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }
    return $raw | ConvertFrom-Json
}

function Write-JsonFile([string]$Path, [object]$Object) {
    $json = $Object | ConvertTo-Json -Depth 100
    [System.IO.File]::WriteAllText($Path, ($json + "`n"), [System.Text.Encoding]::UTF8)
}

function Resolve-PluginDir {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [Parameter(Mandatory = $true)][object]$UprojectData
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    if ($UprojectData.PSObject.Properties.Name -contains 'AdditionalPluginDirectories' -and $UprojectData.AdditionalPluginDirectories -is [System.Collections.IEnumerable]) {
        foreach ($entry in $UprojectData.AdditionalPluginDirectories) {
            if ($entry -is [string] -and -not [string]::IsNullOrWhiteSpace($entry)) {
                $candidates.Add($entry)
            }
        }
    }
    $candidates.Add('./Plugins')
    $candidates.Add('./Loomle/Plugins')

    $seen = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $candidates) {
        if (-not $seen.Add($entry)) {
            continue
        }

        $base = $entry
        if (-not [System.IO.Path]::IsPathRooted($base)) {
            $base = Join-Path $ProjectRoot $base
        }
        $base = [System.IO.Path]::GetFullPath($base)
        $pluginDir = Join-Path $base 'LoomleBridge'
        $uplugin = Join-Path $pluginDir 'LoomleBridge.uplugin'
        if (Test-Path -LiteralPath $uplugin) {
            return $pluginDir
        }
    }

    return [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot 'Loomle\Plugins\LoomleBridge'))
}

function Assert-RootAgentsHint {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [Parameter(Mandatory = $true)][string]$Hint
    )

    $agentsPath = Join-Path $ProjectRoot 'AGENTS.md'
    $state = 'unchanged'

    if (Test-Path -LiteralPath $agentsPath) {
        $existing = Get-Content -LiteralPath $agentsPath -Raw -Encoding UTF8
        $lines = @()
        if ($existing.Length -gt 0) {
            $lines = $existing -split "`r?`n"
        }
        if (-not ($lines -contains $Hint)) {
            if ($existing.Length -gt 0 -and -not $existing.EndsWith("`n")) {
                $existing += "`n"
            }
            if ($existing.Length -gt 0 -and -not $existing.EndsWith("`n`n")) {
                $existing += "`n"
            }
            $existing += ($Hint + "`n")
            [System.IO.File]::WriteAllText($agentsPath, $existing, [System.Text.Encoding]::UTF8)
            $state = 'updated'
        }
    }
    else {
        [System.IO.File]::WriteAllText($agentsPath, ($Hint + "`n"), [System.Text.Encoding]::UTF8)
        $state = 'created'
    }

    return @{ State = $state; Path = $agentsPath }
}

function Test-LocalPrebuiltCompatibility {
    param(
        [Parameter(Mandatory = $true)][string]$PluginBin,
        [Parameter(Mandatory = $true)][string]$PluginModules,
        [Parameter(Mandatory = $true)][string]$EngineVersionFile
    )

    if (-not (Test-Path -LiteralPath $PluginBin)) { return $false }
    if (-not (Test-Path -LiteralPath $PluginModules)) { return $false }
    if (-not (Test-Path -LiteralPath $EngineVersionFile)) { return $false }

    $moduleObj = Read-JsonFile -Path $PluginModules
    $engineObj = Read-JsonFile -Path $EngineVersionFile
    if ($null -eq $moduleObj -or $null -eq $engineObj) { return $false }

    $pluginBuildId = [string]$moduleObj.BuildId
    $engineBuildId = [string]$engineObj.CompatibleChangelist
    if ([string]::IsNullOrWhiteSpace($pluginBuildId) -or [string]::IsNullOrWhiteSpace($engineBuildId)) {
        return $false
    }

    return $pluginBuildId -eq $engineBuildId
}

function Sync-BuiltPluginArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$RootBin,
        [Parameter(Mandatory = $true)][string]$RootModules,
        [Parameter(Mandatory = $true)][string]$PluginBin,
        [Parameter(Mandatory = $true)][string]$PluginModules
    )

    if (-not (Test-Path -LiteralPath $RootBin)) {
        Fail "Built plugin binary not found at expected path: $RootBin"
    }

    $pluginBinDir = Split-Path -Parent $PluginBin
    New-Item -ItemType Directory -Path $pluginBinDir -Force | Out-Null
    Copy-Item -LiteralPath $RootBin -Destination $PluginBin -Force
    Pass 'Synchronized built plugin binary to plugin directory'

    if (Test-Path -LiteralPath $RootModules) {
        $rootObj = Read-JsonFile -Path $RootModules
        if ($null -eq $rootObj) {
            Fail "Failed reading built modules metadata: $RootModules"
        }

        $pluginObj = Read-JsonFile -Path $PluginModules
        if ($null -eq $pluginObj) {
            $pluginObj = [ordered]@{}
        }

        $pluginObj.BuildId = $rootObj.BuildId

        $modules = $pluginObj.Modules
        if ($modules -isnot [System.Collections.IDictionary]) {
            $modules = @{}
        }
        $modules['LoomleBridge'] = 'UnrealEditor-LoomleBridge.dll'
        $pluginObj.Modules = $modules

        $pluginModulesDir = Split-Path -Parent $PluginModules
        New-Item -ItemType Directory -Path $pluginModulesDir -Force | Out-Null
        Write-JsonFile -Path $PluginModules -Object $pluginObj
        Pass 'Synchronized plugin modules BuildId metadata'
    }
}

function Test-ShouldSyncRootArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$RootBin,
        [Parameter(Mandatory = $true)][string]$RootModules,
        [Parameter(Mandatory = $true)][string]$PluginBin,
        [Parameter(Mandatory = $true)][string]$PluginModules
    )

    if (-not (Test-Path -LiteralPath $RootBin)) {
        return $false
    }

    if (-not (Test-Path -LiteralPath $PluginBin)) {
        return $true
    }

    $rootItem = Get-Item -LiteralPath $RootBin
    $pluginItem = Get-Item -LiteralPath $PluginBin
    if ($rootItem.LastWriteTimeUtc -gt $pluginItem.LastWriteTimeUtc) {
        return $true
    }
    if ($rootItem.Length -ne $pluginItem.Length) {
        return $true
    }

    if (Test-Path -LiteralPath $RootModules) {
        if (-not (Test-Path -LiteralPath $PluginModules)) {
            return $true
        }

        $rootObj = Read-JsonFile -Path $RootModules
        $pluginObj = Read-JsonFile -Path $PluginModules
        if ($null -eq $rootObj -or $null -eq $pluginObj) {
            return $true
        }

        $rootBuildId = [string]$rootObj.BuildId
        $pluginBuildId = [string]$pluginObj.BuildId
        if (-not [string]::Equals($rootBuildId, $pluginBuildId, [System.StringComparison]::Ordinal)) {
            return $true
        }
    }

    return $false
}

function Send-BridgeRequest {
    param(
        [Parameter(Mandatory = $true)][System.IO.StreamWriter]$Writer,
        [Parameter(Mandatory = $true)][System.IO.StreamReader]$Reader,
        [Parameter(Mandatory = $true)][int]$Id,
        [Parameter(Mandatory = $true)][string]$Method,
        [Parameter(Mandatory = $true)][object]$Params
    )

    $payload = [ordered]@{
        jsonrpc = '2.0'
        id = $Id
        method = $Method
        params = $Params
    }

    $line = $payload | ConvertTo-Json -Depth 100 -Compress
    $Writer.WriteLine($line)

    while ($true) {
        $responseLine = $Reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($responseLine)) {
            Fail "Empty JSON-RPC response for $Method"
        }

        $response = $responseLine | ConvertFrom-Json
        if ($response.PSObject.Properties.Name -contains 'id' -and $response.id -ne $Id) {
            # Bridge may push notifications on the same connection.
            continue
        }

        if ($response.PSObject.Properties.Name -contains 'error' -and $null -ne $response.error) {
            Fail "JSON-RPC error for ${Method}: $($response.error | ConvertTo-Json -Compress)"
        }

        return $response
    }
}

function Parse-ToolPayload {
    param(
        [Parameter(Mandatory = $true)][object]$Response,
        [Parameter(Mandatory = $true)][string]$MethodName
    )

    if ($null -eq $Response.result) {
        Fail "Invalid $MethodName response: missing result"
    }

    $content = $Response.result.content
    if ($content -isnot [System.Collections.IEnumerable]) {
        Fail "Invalid $MethodName response: missing content"
    }

    $first = $null
    foreach ($item in $content) {
        $first = $item
        break
    }
    if ($null -eq $first -or -not ($first.PSObject.Properties.Name -contains 'text')) {
        Fail "Invalid $MethodName response: missing text payload"
    }

    return $first.text | ConvertFrom-Json
}

try {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $loomleDir = [System.IO.Path]::GetFullPath((Join-Path $scriptDir '..'))
    $projectRoot = [System.IO.Path]::GetFullPath((Join-Path $loomleDir '..'))

    $uprojects = Get-ChildItem -LiteralPath $projectRoot -Filter '*.uproject' -File
    if ($uprojects.Count -eq 0) {
        Fail "No .uproject found in $projectRoot"
    }
    $uprojectPath = $uprojects[0].FullName
    $projectName = [System.IO.Path]::GetFileNameWithoutExtension($uprojectPath)
    $targetName = "${projectName}Editor"

    $hintLine = '- Always read ./Loomle/AGENTS.md before starting work in this project.'

    $engineBinaries = Join-Path $EngineRoot 'Engine\Binaries\Win64'
    $engineBuildTools = Join-Path $EngineRoot 'Engine\Build\BatchFiles'
    $unrealEditorExe = Join-Path $engineBinaries 'UnrealEditor.exe'
    $engineVersionFile = Join-Path $engineBinaries 'UnrealEditor.version'
    $genScript = Join-Path $engineBuildTools 'GenerateProjectFiles.bat'
    $buildScript = Join-Path $engineBuildTools 'Build.bat'

    $rootBin = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-LoomleBridge.dll'
    $rootModules = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor.modules'

    if (-not (Test-Path -LiteralPath $unrealEditorExe)) {
        Fail "UnrealEditor.exe not found: $unrealEditorExe"
    }

    $uprojectData = Read-JsonFile -Path $uprojectPath
    if ($null -eq $uprojectData) {
        Fail "Failed to parse uproject: $uprojectPath"
    }

    Log 'Ensuring .uproject wiring for AdditionalPluginDirectories + LoomleBridge'
    $changed = $false

    if (-not ($uprojectData.PSObject.Properties.Name -contains 'AdditionalPluginDirectories') -or $uprojectData.AdditionalPluginDirectories -isnot [System.Collections.IList]) {
        $uprojectData | Add-Member -NotePropertyName AdditionalPluginDirectories -NotePropertyValue @() -Force
        $changed = $true
    }
    if (-not ($uprojectData.AdditionalPluginDirectories -contains './Loomle/Plugins')) {
        $uprojectData.AdditionalPluginDirectories += './Loomle/Plugins'
        $changed = $true
    }

    if (-not ($uprojectData.PSObject.Properties.Name -contains 'Plugins') -or $uprojectData.Plugins -isnot [System.Collections.IList]) {
        $uprojectData | Add-Member -NotePropertyName Plugins -NotePropertyValue @() -Force
        $changed = $true
    }

    $bridge = $null
    foreach ($plugin in $uprojectData.Plugins) {
        if ($plugin -and $plugin.PSObject.Properties.Name -contains 'Name' -and $plugin.Name -eq 'LoomleBridge') {
            $bridge = $plugin
            break
        }
    }
    if ($null -eq $bridge) {
        $bridge = [pscustomobject]@{
            Name = 'LoomleBridge'
            Enabled = $true
            TargetAllowList = @('Editor')
        }
        $uprojectData.Plugins += $bridge
        $changed = $true
    }

    if (-not (Convert-ToBool $bridge.Enabled)) {
        $bridge.Enabled = $true
        $changed = $true
    }

    $targetAllowListIsEditor = $false
    if ($bridge.PSObject.Properties.Name -contains 'TargetAllowList' -and $bridge.TargetAllowList -is [System.Collections.IEnumerable]) {
        $vals = @($bridge.TargetAllowList)
        if ($vals.Count -eq 1 -and $vals[0] -eq 'Editor') {
            $targetAllowListIsEditor = $true
        }
    }
    if (-not $targetAllowListIsEditor) {
        $bridge.TargetAllowList = @('Editor')
        $changed = $true
    }

    if ($changed) {
        Write-JsonFile -Path $uprojectPath -Object $uprojectData
    }
    Pass '.uproject wiring is correct'

    $agentsResult = Assert-RootAgentsHint -ProjectRoot $projectRoot -Hint $hintLine
    if ($agentsResult.State -eq 'created') {
        Pass "Created root AGENTS guidance at $($agentsResult.Path)"
    }
    elseif ($agentsResult.State -eq 'updated') {
        Pass "Updated root AGENTS guidance at $($agentsResult.Path)"
    }
    else {
        Pass "Root AGENTS guidance already up to date at $($agentsResult.Path)"
    }

    $pluginDir = Resolve-PluginDir -ProjectRoot $projectRoot -UprojectData $uprojectData
    if (-not (Test-Path -LiteralPath $pluginDir)) {
        Fail "Plugin not found from project config/fallback: $pluginDir"
    }
    Pass "Plugin directory resolved: $pluginDir"

    $pluginBin = Join-Path $pluginDir 'Binaries\Win64\UnrealEditor-LoomleBridge.dll'
    $pluginModules = Join-Path $pluginDir 'Binaries\Win64\UnrealEditor.modules'

    if (-not $SkipBuild) {
        if (-not (Test-Path -LiteralPath $buildScript)) {
            Fail "Build script missing: $buildScript"
        }
        $canGenerateProjectFiles = Test-Path -LiteralPath $genScript
        if (-not $canGenerateProjectFiles) {
            Log "GenerateProjectFiles script missing; continuing without project file generation: $genScript"
        }

        $buildRequired = $true
        if ($ForceBuild) {
            Log 'Forced by --force-build'
            $buildRequired = $true
        }
        elseif (Test-LocalPrebuiltCompatibility -PluginBin $pluginBin -PluginModules $pluginModules -EngineVersionFile $engineVersionFile) {
            $buildRequired = $false
            Pass 'Using compatible local prebuilt plugin'
        }

        if ($buildRequired) {
            if ($canGenerateProjectFiles) {
                Log 'Generating project files'
                $genCmd = ('"{0}" -project="{1}" -game' -f $genScript, $uprojectPath)
                Invoke-Cmd -CommandLine $genCmd -WorkingDirectory $projectRoot
                Pass 'Project files generated'
            }
            else {
                Log 'Skipping project file generation; Build.bat path will be used directly'
            }

            Log "Building target: $targetName (Win64 Development)"
            $buildCmd = ('"{0}" "{1}" Win64 Development "{2}" -WaitMutex' -f $buildScript, $targetName, $uprojectPath)
            Invoke-Cmd -CommandLine $buildCmd -WorkingDirectory $projectRoot
            Pass 'Editor target built'

            Sync-BuiltPluginArtifacts -RootBin $rootBin -RootModules $rootModules -PluginBin $pluginBin -PluginModules $pluginModules
        }
        else {
            Log 'Skipping build; compatible prebuilt plugin is available'
        }
    }
    else {
        Log 'Skipping build (--skip-build)'
    }

    if (Test-ShouldSyncRootArtifacts -RootBin $rootBin -RootModules $rootModules -PluginBin $pluginBin -PluginModules $pluginModules) {
        Log 'Synchronizing root build artifacts to plugin directory'
        Sync-BuiltPluginArtifacts -RootBin $rootBin -RootModules $rootModules -PluginBin $pluginBin -PluginModules $pluginModules
    }
    else {
        Log 'Plugin binary artifacts already synchronized'
    }

    if (-not $SkipLaunch) {
        Log 'Launching Unreal Editor'
        Start-Process -FilePath $unrealEditorExe -ArgumentList @($uprojectPath) | Out-Null
        Pass 'Launch command sent'
    }
    else {
        Log 'Skipping launch (--skip-launch)'
    }

    if (-not $SkipVerify) {
        $pipeName = 'loomle'
        Log "Waiting for bridge named pipe: \\.\pipe\$pipeName"

        $connected = $false
        for ($i = 0; $i -lt 60; $i++) {
            $probe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
            try {
                $probe.Connect(200)
                $connected = $true
                $probe.Dispose()
                break
            }
            catch {
                $probe.Dispose()
                Start-Sleep -Seconds 1
            }
        }

        if (-not $connected) {
            Fail "bridge named pipe not ready: \\.\pipe\$pipeName"
        }
        Pass 'bridge named pipe is ready'

        Log 'Running bridge protocol checks'
        $pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
        $pipe.Connect(3000)
        $reader = New-Object System.IO.StreamReader($pipe)
        $writer = New-Object System.IO.StreamWriter($pipe)
        $writer.AutoFlush = $true

        try {
            $initResp = Send-BridgeRequest -Writer $writer -Reader $reader -Id 1 -Method 'initialize' -Params @{}
            $protocolVersion = $initResp.result.protocolVersion
            if ([string]::IsNullOrWhiteSpace([string]$protocolVersion)) {
                Fail 'initialize did not return protocolVersion'
            }
            Pass "initialize protocol=$protocolVersion"

            $toolsResp = Send-BridgeRequest -Writer $writer -Reader $reader -Id 2 -Method 'tools/list' -Params @{}
            $toolNames = @{}
            foreach ($tool in $toolsResp.result.tools) {
                if ($tool -and $tool.PSObject.Properties.Name -contains 'name' -and -not [string]::IsNullOrWhiteSpace([string]$tool.name)) {
                    $toolNames[[string]$tool.name] = $true
                }
            }
            $requiredTools = @('loomle', 'graph', 'graph.query', 'graph.mutate', 'context', 'execute')
            $missing = @()
            foreach ($required in $requiredTools) {
                if (-not $toolNames.ContainsKey($required)) {
                    $missing += $required
                }
            }
            if ($missing.Count -gt 0) {
                Fail ('tools/list missing required tools: ' + ($missing -join ', '))
            }
            Pass "tools/list includes required baseline tools ($($requiredTools.Count))"

            $execResp = Send-BridgeRequest -Writer $writer -Reader $reader -Id 3 -Method 'tools/call' -Params @{
                name = 'execute'
                arguments = @{
                    mode = 'exec'
                    code = "import unreal`nassert hasattr(unreal, 'LoomleBlueprintAdapter')"
                }
            }
            $execPayload = Parse-ToolPayload -Response $execResp -MethodName 'tools/call.execute'
            if ($execPayload.PSObject.Properties.Name -contains 'isError' -and (Convert-ToBool $execPayload.isError)) {
                Fail ("execute failed: " + [string]$execPayload.message)
            }
            Pass 'unreal.LoomleBlueprintAdapter is available'
        }
        finally {
            if ($null -ne $writer) { $writer.Dispose() }
            if ($null -ne $reader) { $reader.Dispose() }
            if ($null -ne $pipe) { $pipe.Dispose() }
        }

        Pass 'Bridge verification complete'
    }
    else {
        Log 'Skipping bridge verification (--skip-verify)'
    }

    Pass "Loomle install flow completed for $projectName"
}
catch {
    Write-Error $_
    exit 1
}
