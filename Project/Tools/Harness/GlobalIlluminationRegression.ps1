param([string]$Configuration = 'Debug', [string]$Scene = 'Sponza.scene.json', [switch]$Quick,
    [ValidateRange(0.0, 1000.0)][float]$ProbeSpacing = 0.0,
    [ValidateRange(0.0, 100000.0)][float]$MaxRayDistance = 0.0,
    [float]$SceneCameraOffsetX = 0.0)
$ErrorActionPreference = 'Stop'
$engineRoot = (Resolve-Path "$PSScriptRoot/../../..").Path
$gameRoot = Join-Path $engineRoot 'Project/Sandbox'
$project = Get-Content -LiteralPath "$gameRoot/Sandbox.nemproject" -Raw | ConvertFrom-Json
$resultRoot = Join-Path $engineRoot ('Generated/Refactoring/GIRegression-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$settingsRoot = Join-Path $resultRoot 'UserSettings'
$destination = Join-Path $settingsRoot $project.projectGuid
New-Item -ItemType Directory -Path $destination -Force | Out-Null

# 通常のEditor設定を変更せず同じSceneで計測する
$source = Join-Path $env:LOCALAPPDATA "NEMEngine/Projects/$($project.projectGuid)"
foreach ($category in @('Editor', 'Runtime')) {
    if (Test-Path -LiteralPath "$source/$category") {
        Copy-Item -LiteralPath "$source/$category" -Destination $destination -Recurse
    }
}
New-Item -ItemType Directory -Path "$destination/Editor" -Force | Out-Null
$sceneMeta = Get-ChildItem -LiteralPath "$gameRoot/GameAssets" -Filter "$Scene.meta" -Recurse
if (@($sceneMeta).Count -ne 1) { throw "Sceneを特定できません: $Scene" }
$sceneID = (Get-Content -LiteralPath $sceneMeta.FullName -Raw | ConvertFrom-Json).guid
@{ activeScene = $sceneID } | ConvertTo-Json | Set-Content -LiteralPath "$destination/Editor/ActiveScene.json" -Encoding utf8

# 隔離したSceneViewの位置だけを比較用に変更する
$cameraPath = Join-Path $destination 'Editor/SceneViewCamera.json'
if ($SceneCameraOffsetX -ne 0.0) {
    if (!(Test-Path -LiteralPath $cameraPath)) { throw '比較元のSceneView設定がありません' }
    $camera = Get-Content -LiteralPath $cameraPath -Raw | ConvertFrom-Json
    $camera.'transform3D.pos'.x += $SceneCameraOffsetX
    $camera | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $cameraPath -Encoding utf8
}

# Project共通のGI設定は検証終了後に復元する
$giPath = Join-Path $gameRoot 'ProjectSettings/Runtime/GlobalIllumination.json'
$giExists = Test-Path -LiteralPath $giPath
$giOriginal = if ($giExists) { [IO.File]::ReadAllBytes($giPath) } else { $null }
$oldSettings = $env:NEMENGINE_USER_SETTINGS_ROOT
$oldBenchmark = $env:NEM_RENDER_BENCHMARK
$oldFreeze = $env:NEM_RENDER_BENCHMARK_FREEZE
$oldGI = $env:NEM_RENDER_BENCHMARK_GI
$oldQuick = $env:NEM_RENDER_BENCHMARK_QUICK
$process = $null
try {
    # 比較用の設定だけ変更し、通常の設定はfinallyで復元する
    if ($ProbeSpacing -gt 0.0 -or $MaxRayDistance -gt 0.0) {
        if (!$giExists) { throw '比較元のGI設定がありません' }
        $giSettings = Get-Content -LiteralPath $giPath -Raw | ConvertFrom-Json
        if ($ProbeSpacing -gt 0.0) { $giSettings.probeSpacing = $ProbeSpacing }
        if ($MaxRayDistance -gt 0.0) { $giSettings.maxRayDistance = $MaxRayDistance }
        $giSettings | ConvertTo-Json | Set-Content -LiteralPath $giPath -Encoding utf8
    }
    # SceneとProbe設定を数値結果へ対応付ける
    $recordedGI = if (Test-Path -LiteralPath $giPath) { Get-Content -LiteralPath $giPath -Raw | ConvertFrom-Json } else { $null }
    @{ scene = $Scene; globalIllumination = $recordedGI; sceneCameraOffsetX = $SceneCameraOffsetX } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$resultRoot/Conditions.json" -Encoding utf8
    $env:NEMENGINE_USER_SETTINGS_ROOT = $settingsRoot
    $env:NEM_RENDER_BENCHMARK = Join-Path $resultRoot 'Results.json'
    $env:NEM_RENDER_BENCHMARK_FREEZE = '1'
    $env:NEM_RENDER_BENCHMARK_GI = '1'
    $env:NEM_RENDER_BENCHMARK_QUICK = if ($Quick) { '1' } else { '0' }
    $process = Start-Process -FilePath "$engineRoot/Generated/Output/$Configuration/NEMEditor/NEMEditor.exe" `
        -WorkingDirectory $gameRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "$resultRoot/Editor.stdout.log" -RedirectStandardError "$resultRoot/Editor.stderr.log"
    Write-Output "GI比較 PID=$($process.Id) result=$resultRoot"
    if (!$process.WaitForExit(600000)) {
        $process.Kill($true)
        throw 'GI描画比較が10分以内に終了しませんでした'
    }
    if ($process.ExitCode -ne 0) { throw "GI描画比較に失敗しました: $($process.ExitCode)" }
    # GPU時間があっても入力未接続の結果は採用しない
    $diagnostics = Select-String -LiteralPath "$resultRoot/Editor.stdout.log", "$resultRoot/Editor.stderr.log" `
        -Pattern '\[Assert::Call\]|\[ShaderCompileError\]|GIのBufferが見つかりません|GI頂点のBufferが見つかりません|D3D12 ERROR|D3D12 CORRUPTION'
    if ($diagnostics) { throw "GI描画にエラーがあります: $($diagnostics[0].Line)" }
    $report = Get-Content -LiteralPath $env:NEM_RENDER_BENCHMARK -Raw | ConvertFrom-Json
    if (@($report.phases).Count -ne 3 -or !$report.phases[1].globalIllumination) {
        throw '対応GPUでGIを有効化できませんでした'
    }
    $passes = @($report.phases[1].frames | ForEach-Object { $_.passes } | Where-Object { $_.name -eq 'GI/ProbeUpdate' })
    if (!$passes.Count -or !($passes | Where-Object { $_.ms -gt 0 })) { throw 'GI更新のGPU計測がありません' }
    if ($report.quickCheck) { Write-Output '短い描画確認のため性能比較には使用しません' }
    foreach ($phase in $report.phases) {
        $mean = ($phase.frames | Measure-Object -Property gpuMs -Average).Average
        Write-Output "phase=$($phase.phase) GI=$($phase.globalIllumination) GPU=$([Math]::Round($mean, 3)) ms"
    }
    Write-Output "GI描画比較が完了しました: $resultRoot"
} finally {
    if ($process -and !$process.HasExited) { $process.Kill($true) }
    if ($process) { $process.Dispose() }
    if ($giExists) { [IO.File]::WriteAllBytes($giPath, $giOriginal) }
    elseif (Test-Path -LiteralPath $giPath) { Remove-Item -LiteralPath $giPath }
    $env:NEMENGINE_USER_SETTINGS_ROOT = $oldSettings
    $env:NEM_RENDER_BENCHMARK = $oldBenchmark
    $env:NEM_RENDER_BENCHMARK_FREEZE = $oldFreeze
    $env:NEM_RENDER_BENCHMARK_GI = $oldGI
    $env:NEM_RENDER_BENCHMARK_QUICK = $oldQuick
}
