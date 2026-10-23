param([switch]$PrepareOnly)

$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')
$validationRoot = Join-Path $engineRoot 'Generated/Refactoring/ParticleSystemRefactor/ProductValidation'
$gameRoot = Join-Path $validationRoot 'Sandbox'
$sceneDirectory = Join-Path $gameRoot 'GameAssets/Scenes/Validation'
$materialDirectory = Join-Path $gameRoot 'GameAssets/Materials/Config'
$null = New-Item -ItemType Directory -Force -Path $sceneDirectory, $materialDirectory
Copy-Item -LiteralPath (Join-Path $engineRoot 'Project/Sandbox/Sandbox.vcxproj') -Destination $gameRoot -Force
Get-ChildItem -LiteralPath (Join-Path $engineRoot 'Project/Sandbox/GameAssets/Scenes/Validation') -File |
    Copy-Item -Destination $sceneDirectory -Force
foreach ($suffix in @('', '.meta')) {
    Copy-Item -LiteralPath (Join-Path $engineRoot ("Project/Sandbox/GameAssets/Materials/Config/defaultMaterials.materialSettings.json" + $suffix)) `
        -Destination $materialDirectory -Force
}
@{ schemaVersion = 1; projectGuid = '4e454d41535345547061727470726f64'; name = 'Sandbox'
    assetsDirectory = 'GameAssets'; packagesDirectory = 'Packages'; projectSettingsDirectory = 'ProjectSettings' } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $gameRoot 'Sandbox.nemproject') -Encoding utf8

$probe = Join-Path $engineRoot 'Generated/Output/Debug/GameBuildServiceProbe/GameBuildServiceProbe.exe'
$previousEngineRoot = $env:NEMENGINE_ROOT
try {
    $env:NEMENGINE_ROOT = $engineRoot
    # 元のSandboxの無関係なAssetを収集へ持ち込まない
    foreach ($mode in @('collect', 'manifest')) {
        $destination = Join-Path $validationRoot ($mode + '.json')
        $result = Invoke-HarnessProcess -FilePath $probe -Arguments @($gameRoot,
            '4e454d41535345547061727473636e01', $destination, ('--' + $mode)) -WorkingDirectory $gameRoot -TimeoutSeconds 240
        $result.output | Set-Content -LiteralPath (Join-Path $validationRoot ($mode + '.log')) -Encoding utf8
        if ($result.exitCode -ne 0 -or $result.timedOut) { throw "製品入力の検証に失敗しました: $mode / $($result.output)" }
    }
    $manifestPath = Join-Path $validationRoot 'manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    # Assetだけを分離し、実行ファイルは実際のSandboxから構築する
    $manifest.projectPath = Join-Path $engineRoot 'Project/Sandbox/Sandbox.vcxproj'
    # WindowsPowerShellでも配置先が長くなりすぎない場所を使う
    $manifest.outputRoot = Join-Path $engineRoot 'Generated/PSP'
    $manifest.gameWidth = 640
    $manifest.gameHeight = 360
    $manifest | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    if ($PrepareOnly) { Write-Output "製品入力の検証完了: $manifestPath"; return }

    $result = Invoke-HarnessProcess -FilePath 'powershell.exe' -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $engineRoot 'Tools/BuildGame.ps1'), '-ManifestPath', $manifestPath) `
        -WorkingDirectory $engineRoot -TimeoutSeconds 1800
    $result.output | Set-Content -LiteralPath (Join-Path $validationRoot 'Build.log') -Encoding utf8
    if ($result.exitCode -ne 0 -or $result.timedOut) { throw "製品の構築とCookに失敗しました: $($result.output)" }
    $productRoot = Join-Path $engineRoot 'Generated/PSP/ProductProbe'
    $cook = Get-Content -LiteralPath (Join-Path $productRoot 'Cooked/Shaders/ShaderCookManifest.json') -Raw | ConvertFrom-Json
    foreach ($name in @('particleGPUSimulate', 'particleGPUView', 'particleGPURibbonSelect',
        'particleGPUTrailUpdate', 'particleGPUTrailPrepare', 'particleGPUTrailDraw', 'particleGPUDraw')) {
        $sourcePath = Join-Path $engineRoot ("Project/Engine/Assets/Shaders/Builtin/Particle/GPU/$name.shader.json")
        $source = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
        $meta = Get-Content -LiteralPath ($sourcePath + '.meta') -Raw | ConvertFrom-Json
        $shader = @($cook.shaders | Where-Object { $_.asset.guid -eq $meta.guid })
        if ($shader.Count -ne 1 -or $shader[0].stages.Count -ne $source.stages.Count) { throw "製品のShaderが不足しています: $name" }
        foreach ($stage in $shader[0].stages) {
            $bytecode = Join-Path $productRoot ('Cooked/Shaders/' + $stage.bytecode)
            if (!(Test-Path -LiteralPath $bytecode) -or (Get-Item -LiteralPath $bytecode).Length -eq 0) {
                throw "製品のShaderが空です: $bytecode"
            }
        }
    }
    Write-Output "ParticleSystem製品の構築とCook検証完了: $productRoot"
} finally {
    $env:NEMENGINE_ROOT = $previousEngineRoot
}
