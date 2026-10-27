param([ValidateSet('Raster', 'DXR', 'Vertex')][string]$Mode = 'Raster', [switch]$Showcase)

$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')
. (Join-Path $PSScriptRoot 'HarnessFixtureProject.ps1')
$outputRoot = Join-Path $engineRoot "Generated/Refactoring/Decals/$Mode"
if ($Showcase) { $outputRoot += 'Showcase' }
$gameRoot = New-HarnessFixtureProject -Destination (Join-Path $outputRoot 'FixtureProject') -Fixture Decals
$fixtureRoot = Join-Path $gameRoot 'GameAssets/__DecalValidation'
if (Test-Path -LiteralPath $fixtureRoot) { throw "検証先が既に存在します: $fixtureRoot" }
$null = New-Item -ItemType Directory -Path $fixtureRoot
$null = New-Item -ItemType Directory -Path $outputRoot -Force
& (Join-Path $engineRoot 'Tools/PruneGeneratedArtifacts.ps1') -ValidationImages -ResetCaptureDirectory $outputRoot
$projectGUID = 'd0d59331ff0a4eb589ea7801cb52f208'
$settingsRoot = Join-Path $outputRoot "UserSettings/$projectGUID"
$null = New-Item -ItemType Directory -Path (Join-Path $settingsRoot 'Runtime') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $settingsRoot 'Editor') -Force
$previousEngineRoot = $env:NEMENGINE_ROOT
try {
    # 既存Sceneを保存し直さず個数別の検証Sceneを用意する
    $scenePath = Join-Path $gameRoot 'GameAssets/Scenes/DecalValidation.scene.json'
    $paths = @()
    foreach ($count in @(0, 32, 128)) {
        $scene = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json -AsHashtable
        $template = $scene.Entities | Where-Object { $_.Components.Name.name -eq 'ColorDecal' } | Select-Object -First 1
        foreach ($entity in $scene.Entities) {
            if ($entity.Components.ContainsKey('DecalProjector')) { $entity.Components.DecalProjector.enabled = $false }
        }
        for ($index = 0; $index -lt $count; ++$index) {
            $stamp = $template | ConvertTo-Json -Depth 50 | ConvertFrom-Json -AsHashtable
            $id = (0xdec1000000000000UL + [uint64]$index).ToString('x16')
            $stamp.LocalFileID = $id
            $stamp.Components.SceneObject.localFileId = $id
            $stamp.Components.Name.name = "BenchDecal$index"
            $stamp.Components.DecalProjector.enabled = $true
            $stamp.Components.DecalProjector.size = @{ x = 0.55; y = 0.65; z = 0.18 }
            $stamp.Components.Transform.localPos = @{x = -5.5 + ($index % 16) * 0.7; y = 0.3 + [Math]::Floor($index / 16) * 0.45; z = 4.75}
            $scene.Entities += $stamp
        }
        $logical = "GameAssets/__DecalValidation/Count$count.scene.json"
        $target = Join-Path $fixtureRoot "Count$count.scene.json"
        $scene | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $target -Encoding utf8
        $paths += $logical
    }
    if ($Showcase) { $paths = @('GameAssets/Scenes/DecalValidation.scene.json') }
    ConvertTo-Json -InputObject $paths | Set-Content -LiteralPath (Join-Path $outputRoot 'Scenes.json') -Encoding utf8
    @{ allowMeshShader = $Mode -ne 'Vertex'; allowInlineRayTracing = $Mode -eq 'DXR';
       allowDispatchRays = $Mode -eq 'DXR'; frameContextCount = 2; allowMeshLOD = $false } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $settingsRoot 'Runtime/GraphicsFeatures.json') -Encoding utf8
    @{ activeScene = 'd3ca1000000000000000000000000028' } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $settingsRoot 'Editor/ActiveScene.json') -Encoding utf8
    # 副Viewの非表示を含め描画条件を固定する
    $catalog = Get-Content -LiteralPath (Join-Path $engineRoot 'Project/Engine/Assets/Config/editorLayouts.exeConfig.json') -Raw | ConvertFrom-Json -AsHashtable
    $layout = $catalog.layouts[0]
    $layout.visibility.sceneView = $false
    @{ schemaVersion = 1; activeLayoutID = $layout.layoutID; layout = $layout } |
        ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $settingsRoot 'Editor/LayoutSession.json') -Encoding utf8
    $env:NEMENGINE_ROOT = $engineRoot
    $env:NEMENGINE_USER_SETTINGS_ROOT = Join-Path $outputRoot 'UserSettings'
    $env:NEM_RENDER_BENCHMARK = Join-Path $outputRoot 'Report.json'
    $env:NEM_RENDER_BENCHMARK_SCENES = Join-Path $outputRoot 'Scenes.json'
    $env:NEM_RENDER_BENCHMARK_QUICK = '1'
    $env:NEM_RENDER_BENCHMARK_FREEZE = '1'
    $env:NEM_RENDER_BENCHMARK_VISIBLE = '0'
    $env:NEM_RENDER_BENCHMARK_WIDTH = '1280'
    $env:NEM_RENDER_BENCHMARK_HEIGHT = '720'
    $env:NEM_RENDER_BENCHMARK_PHASE_COUNT = [string]$paths.Count
    $result = Invoke-HarnessProcess -FilePath (Join-Path $engineRoot 'Generated/Output/Develop/NEMEditor/NEMEditor.exe') `
        -WorkingDirectory $gameRoot -TimeoutSeconds 240
    $result.output | Set-Content -LiteralPath (Join-Path $outputRoot 'Editor.log') -Encoding utf8
    if ($result.exitCode -ne 0 -or $result.timedOut) { throw "描画検証に失敗しました: $($result.exitCode)" }
    if (!(Test-Path -LiteralPath $env:NEM_RENDER_BENCHMARK)) { throw '描画検証の結果がありません' }
    Write-Output "描画検証完了: $env:NEM_RENDER_BENCHMARK"
} finally {
    $env:NEMENGINE_ROOT = $previousEngineRoot
    # この検証が作ったフォルダーだけを削除する
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $expected = [IO.Path]::GetFullPath((Join-Path $gameRoot 'GameAssets/__DecalValidation'))
    if ($resolved -ne $expected -or !$resolved.StartsWith($engineRoot + [IO.Path]::DirectorySeparatorChar)) { throw '検証先の確認に失敗しました' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
    $sidecar = $resolved + '.meta'
    if (Test-Path -LiteralPath $sidecar) { Remove-Item -LiteralPath $sidecar -Force }
    foreach ($name in @('NEMENGINE_USER_SETTINGS_ROOT', 'NEM_RENDER_BENCHMARK', 'NEM_RENDER_BENCHMARK_SCENES',
        'NEM_RENDER_BENCHMARK_QUICK', 'NEM_RENDER_BENCHMARK_FREEZE', 'NEM_RENDER_BENCHMARK_VISIBLE',
        'NEM_RENDER_BENCHMARK_WIDTH', 'NEM_RENDER_BENCHMARK_HEIGHT', 'NEM_RENDER_BENCHMARK_PHASE_COUNT')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    & (Join-Path $engineRoot 'Tools/PruneGeneratedArtifacts.ps1') -ValidationImages -PreserveCaptureDirectory $outputRoot
}
