param(
    [ValidateSet('Debug', 'Develop', 'Release')][string]$Configuration = 'Develop',
    [ValidateSet('Editor', 'Tests', 'Build', 'Filters', 'Telemetry')][string]$Mode = 'Editor',
    [ValidateSet('Scene', 'Game', 'Both')][string]$View = 'Scene',
    [ValidateSet(1, 2)][int]$InitialFrameCount = 2,
    [ValidateRange(1, 8192)][int]$Width = 640,
    [ValidateRange(1, 8192)][int]$Height = 360,
    [ValidatePattern('^[a-zA-Z0-9_-]*$')][string]$RunLabel = '',
    [ValidateSet('Normal', 'MeshShaderOff', 'RayTracingOff', 'RayTracingOn', 'Transforms', 'SceneReload')][string]$Scenario = 'Normal',
    [ValidateSet('None', 'Final', 'Intermediate', 'HDR')][string]$CaptureMode = 'Final',
    [switch]$BuildDebugAssimp,
    [switch]$Performance,
    [switch]$FixedFrameCount,
    [switch]$CameraMotion
)

$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')
$validationRoot = Join-Path $engineRoot 'Generated/Refactoring/DoubleBuffering'
$null = New-Item -ItemType Directory -Path $validationRoot -Force
if ($Mode -eq 'Build') {
    if ($BuildDebugAssimp) {
        $external = Invoke-HarnessProcess -FilePath 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe' `
            -Arguments @('Generated/Externals/assimp/code/assimp.vcxproj', '-m:1', '-p:Configuration=Debug', '-p:Platform=x64',
                '-p:CL_MPCount=2', '-p:MultiProcMaxCount=2', '-p:PreferredToolArchitecture=x64', '-v:minimal', '-nologo') `
            -WorkingDirectory $engineRoot -TimeoutSeconds 1800
        $external.output | Set-Content -LiteralPath (Join-Path $validationRoot 'Build.Assimp.Debug.log') -Encoding utf8
        if ($external.exitCode -ne 0 -or $external.timedOut) { throw 'AssimpのDebugビルドに失敗しました' }
    }
    $start = @{
        FilePath = 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
        Arguments = @('Project/NEMEngine.slnx', '-t:NEMEditor;NEMTests', '-m:1', '-p:MultiProcMaxCount=2',
            '-p:CL_MPCount=2', '-p:PreferredToolArchitecture=x64', "-p:Configuration=$Configuration",
            '-p:Platform=x64', '-v:minimal', '-nologo')
        WorkingDirectory = $engineRoot
        TimeoutSeconds = 3600
    }
    $result = Invoke-HarnessProcess @start
    $result.output | Set-Content -LiteralPath (Join-Path $validationRoot "Build.$Configuration.log") -Encoding utf8
    if ($result.exitCode -ne 0 -or $result.timedOut) { throw "ビルドに失敗しました: $($result.exitCode)" }
    Write-Output "Build $Configuration OK"
    return
}
if ($Mode -eq 'Tests') {
    foreach ($argument in @('--foundation', '--editor', '--frame-buffering', '--frame-buffering-hardware',
        '--gpu-retirement-hardware', '--render-features', '--bloom', '--gpu-compatible-hardware', '--fbx-import',
        '--editor-asset-workflow')) {
        $start = @{
            FilePath = Join-Path $engineRoot "Generated/Output/$Configuration/NEMTests/NEMTests.exe"
            Arguments = @($argument)
            WorkingDirectory = Join-Path $engineRoot 'Project'
            TimeoutSeconds = 240
        }
        $result = Invoke-HarnessProcess @start
        $result.output | Set-Content -LiteralPath (Join-Path $validationRoot ($argument.TrimStart('-') + '.log')) -Encoding utf8
        if ($result.exitCode -ne 0 -or $result.timedOut) { throw "検証に失敗しました: $argument $($result.exitCode)" }
        Write-Output "$argument OK"
    }
    return
}
if ($Mode -eq 'Filters') {
    $previousReference = $env:NEM_GPU_FILTER_REFERENCE_ROOT
    $previousPerformance = $env:NEM_GPU_FILTER_PERFORMANCE
    try {
        $env:NEM_GPU_FILTER_REFERENCE_ROOT = Join-Path $engineRoot 'Generated/Refactoring/GPUOptimization/Reference'
        $env:NEM_GPU_FILTER_PERFORMANCE = if ($Performance) { '1' } else { $null }
        $start = @{
            FilePath = Join-Path $engineRoot "Generated/Output/$Configuration/NEMTests/NEMTests.exe"
            Arguments = @('--gpu-filter-equivalence')
            WorkingDirectory = Join-Path $engineRoot 'Project'
            TimeoutSeconds = 240
        }
        $result = Invoke-HarnessProcess @start
        $log = if ($Performance) { '../FilterPerformance.log' } else { '../FilterEquivalence.log' }
        $result.output | Set-Content -LiteralPath (Join-Path $env:NEM_GPU_FILTER_REFERENCE_ROOT $log) -Encoding utf8
        if ($result.exitCode -ne 0 -or $result.timedOut) { throw "Filterの比較に失敗しました: $($result.exitCode)" }
        Write-Output 'GPU filter equivalence OK'
    } finally {
        $env:NEM_GPU_FILTER_REFERENCE_ROOT = $previousReference
        $env:NEM_GPU_FILTER_PERFORMANCE = $previousPerformance
    }
    return
}
$outputRoot = Join-Path $validationRoot "EditorValidation/$View$InitialFrameCount"
if ($RunLabel) { $outputRoot = Join-Path $engineRoot "Generated/Refactoring/GPUOptimization/$RunLabel" }
if ($Mode -eq 'Telemetry') {
    $monitor = Get-Command nvidia-smi -ErrorAction Stop
    $csv = Join-Path $outputRoot 'GPU.csv'
    'timestamp,temperature.gpu,clocks.current.graphics [MHz],utilization.gpu [%],memory.used [MiB],power.draw [W]' |
        Set-Content -LiteralPath $csv -Encoding utf8
    while ($true) {
        # 毎回取得を完了し、終了直前の記録もファイルへ残す
        $sample = & $monitor.Source --query-gpu=timestamp,temperature.gpu,clocks.current.graphics,utilization.gpu,memory.used,power.draw --format=csv,noheader,nounits
        if ($LASTEXITCODE -eq 0) { $sample | Add-Content -LiteralPath $csv -Encoding utf8 }
        Start-Sleep -Milliseconds 1000
    }
}
$pruneScript = Join-Path $engineRoot 'Tools/PruneGeneratedArtifacts.ps1'
& $pruneScript -ValidationImages -ResetCaptureDirectory $outputRoot
if ($Performance -and !$PSBoundParameters.ContainsKey('CaptureMode')) { $CaptureMode = 'None' }
$projectRoot = Join-Path $engineRoot 'Project/Sandbox'
$descriptor = Get-Content -LiteralPath (Join-Path $projectRoot 'Sandbox.nemproject') -Raw | ConvertFrom-Json
$settingsRoot = Join-Path $outputRoot "UserSettings/$($descriptor.projectGuid)"
$null = New-Item -ItemType Directory -Path (Join-Path $settingsRoot 'Runtime') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $settingsRoot 'Editor') -Force
$environmentBackup = @{}
$telemetry = $null
foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
    if ($entry.Key -like 'NEM_RENDER_BENCHMARK*' -or $entry.Key -eq 'NEMENGINE_USER_SETTINGS_ROOT') {
        $environmentBackup[$entry.Key] = $entry.Value
    }
}
try {
    foreach ($name in $environmentBackup.Keys) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    # 実際のユーザー設定とSceneを変更せず同じ描画条件で比較する
    $original = Join-Path $env:LOCALAPPDATA "NEMEngine/Projects/$($descriptor.projectGuid)/Runtime/GraphicsFeatures.json"
    $preferences = if (Test-Path -LiteralPath $original) {
        Get-Content -LiteralPath $original -Raw | ConvertFrom-Json -AsHashtable
    } else { @{} }
    $preferences.frameContextCount = $InitialFrameCount
    if ($Scenario -eq 'MeshShaderOff') { $preferences.allowMeshShader = $false }
    if ($Scenario -eq 'RayTracingOn') {
        $preferences.allowInlineRayTracing = $true
        $preferences.allowDispatchRays = $true
    }
    if ($Scenario -eq 'RayTracingOff') {
        $preferences.allowInlineRayTracing = $false
        $preferences.allowDispatchRays = $false
    }
    $preferences | ConvertTo-Json -Depth 20 |
        Set-Content -LiteralPath (Join-Path $settingsRoot 'Runtime/GraphicsFeatures.json') -Encoding utf8
    @{ targetFps = 0; editorTargetFps = 0 } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $settingsRoot 'Runtime/FrameRate.json') -Encoding utf8
    $meta = Get-Content -LiteralPath (Join-Path $projectRoot 'GameAssets/Scenes/View/Sponza.scene.json.meta') -Raw | ConvertFrom-Json
    @{ activeScene = $meta.guid } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $settingsRoot 'Editor/ActiveScene.json') -Encoding utf8
    $catalog = Get-Content -LiteralPath (Join-Path $engineRoot 'Project/Engine/Assets/Config/editorLayouts.exeConfig.json') -Raw |
        ConvertFrom-Json -AsHashtable
    $layout = $catalog.layouts[0]
    $layout.visibility.sceneView = $View -ne 'Game'
    $layout.visibility.gameView = $View -ne 'Scene'
    if ($View -eq 'Both') {
        # 同じDockの裏側へ隠れない配置で二つの描画を確認する
        $layout.builtinDefault = $false
        $layout.imguiIniData = "[Window][SceneView]`nPos=10,80`nSize=900,500`nCollapsed=0`n`n[Window][GameView]`nPos=930,80`nSize=900,500`nCollapsed=0`n`n"
        foreach ($name in @('console', 'hierarchy', 'inspector', 'project', 'tool', 'toolbar')) {
            $layout.visibility[$name] = $false
        }
    }
    @{ schemaVersion = 1; activeLayoutID = $layout.layoutID; layout = $layout } |
        ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $settingsRoot 'Editor/LayoutSession.json') -Encoding utf8
    $sequence = @($InitialFrameCount, (3 - $InitialFrameCount), $InitialFrameCount)
    if ($FixedFrameCount) { $sequence = @($InitialFrameCount, $InitialFrameCount, $InitialFrameCount) }
    $sequence | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'FrameCounts.json') -Encoding utf8
    $env:NEMENGINE_USER_SETTINGS_ROOT = Join-Path $outputRoot 'UserSettings'
    $env:NEM_RENDER_BENCHMARK = Join-Path $outputRoot 'Report.json'
    $env:NEM_RENDER_BENCHMARK_FRAME_CONTEXTS = Join-Path $outputRoot 'FrameCounts.json'
    $env:NEM_RENDER_BENCHMARK_QUICK = if ($Performance) { '0' } else { '1' }
    $env:NEM_RENDER_BENCHMARK_FREEZE = '1'
    $env:NEM_RENDER_BENCHMARK_VISIBLE = '0'
    $env:NEM_RENDER_BENCHMARK_WIDTH = "$Width"
    $env:NEM_RENDER_BENCHMARK_HEIGHT = "$Height"
    $env:NEM_RENDER_BENCHMARK_CAPTURE = $CaptureMode
    if ($CameraMotion) { $env:NEM_RENDER_BENCHMARK_CAMERA_MOTION = '1' }
    if ($Scenario -eq 'SceneReload') {
        @('GameAssets/Scenes/View/Sponza.scene.json', 'GameAssets/Scenes/View/Sponza.scene.json',
            'GameAssets/Scenes/View/Sponza.scene.json') | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $outputRoot 'Scenes.json') -Encoding utf8
        $env:NEM_RENDER_BENCHMARK_SCENES = Join-Path $outputRoot 'Scenes.json'
    }
    if ($Scenario -eq 'Transforms') {
        $steps = foreach ($size in @(100, 250, 100)) {
            @{ motionFrames = 60; targets = @(@{ asset = 'GameAssets/Models/Common/cube.obj'; position = @(0, 100, 0)
                scale = @($size, $size, $size); rotation = @(0, 0.5, 0) }) }
        }
        $steps | ConvertTo-Json -Depth 10 |
            Set-Content -LiteralPath (Join-Path $outputRoot 'Transforms.json') -Encoding utf8
        $env:NEM_RENDER_BENCHMARK_TRANSFORM = Join-Path $outputRoot 'Transforms.json'
    }
    $started = [DateTime]::UtcNow
    $gpuMonitor = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if ($Performance -and $gpuMonitor) {
        # クロック差による計測の揺れも記録する
        $monitorArguments = @('-NoProfile', '-File', ('"' + $PSCommandPath + '"'),
            '-Mode', 'Telemetry', '-View', $View, '-InitialFrameCount', $InitialFrameCount)
        if ($RunLabel) { $monitorArguments += @('-RunLabel', $RunLabel) }
        $telemetry = Start-Process -FilePath 'pwsh' -ArgumentList $monitorArguments -WindowStyle Hidden -PassThru
    }
    $result = Invoke-HarnessProcess -FilePath (Join-Path $engineRoot "Generated/Output/$Configuration/NEMEditor/NEMEditor.exe") `
        -WorkingDirectory $projectRoot -TimeoutSeconds 600
    $result.output | Set-Content -LiteralPath (Join-Path $outputRoot 'Editor.log') -Encoding utf8
    if ($result.exitCode -ne 0 -or $result.timedOut) { throw "描画検証に失敗しました: $($result.exitCode)" }
    if ((Get-Item -LiteralPath $env:NEM_RENDER_BENCHMARK).LastWriteTimeUtc -lt $started) { throw '新しい描画結果がありません' }
    $report = Get-Content -LiteralPath $env:NEM_RENDER_BENCHMARK -Raw | ConvertFrom-Json
    if ($report.phases.Count -ne 3 -or ($report.phases.frameContextCount -join ',') -ne ($sequence -join ',')) {
        throw '描画フレーム数の切替結果が一致しません'
    }
    if ($report.captureMode -ne $CaptureMode) { throw '画像の保存範囲が一致しません' }
    $captures = @(Get-ChildItem -LiteralPath $outputRoot -File | Where-Object { $_.Extension -in '.png', '.dds' })
    if (($CaptureMode -eq 'None' -and $captures.Count) -or
        ($CaptureMode -ne 'HDR' -and ($captures | Where-Object Extension -eq '.dds'))) {
        throw '指定していない確認画像が生成されています'
    }
    if (($CaptureMode -ne 'None' -and !$captures.Count) -or
        ($CaptureMode -eq 'Final' -and ($captures | Where-Object { $_.Name -notmatch '^\d+-(Game|Scene)\.png$' }))) {
        throw '確認画像の保存結果が一致しません'
    }
    foreach ($phase in $report.phases) {
        if (!($phase.frames | Where-Object { $_.gpuValid -and $_.passes.Count -gt 0 })) {
            throw "GPU計測がありません: $($phase.phase)"
        }
        if ($View -eq 'Both') {
            $names = @($phase.frames | ForEach-Object { $_.passes.name })
            if (!($names -like 'Game/*') -or !($names -like 'Scene/*')) {
                throw "両Viewの描画を確認できません: $($phase.phase)"
            }
        }
    }
    Write-Output "描画フレーム数$($sequence -join '→')の検証完了: $env:NEM_RENDER_BENCHMARK"
} finally {
    if ($telemetry) {
        try {
            if (!$telemetry.HasExited) { $telemetry.Kill($true); $telemetry.WaitForExit() }
        } finally { $telemetry.Dispose() }
    }
    $current = [Environment]::GetEnvironmentVariables()
    foreach ($name in $current.Keys) {
        if ($name -like 'NEM_RENDER_BENCHMARK*' -or $name -eq 'NEMENGINE_USER_SETTINGS_ROOT') {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
    }
    foreach ($name in $environmentBackup.Keys) {
        [Environment]::SetEnvironmentVariable($name, $environmentBackup[$name], 'Process')
    }
    & $pruneScript -ValidationImages -PreserveCaptureDirectory $outputRoot
}
