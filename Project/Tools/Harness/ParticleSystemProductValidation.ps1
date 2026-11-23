param([switch]$PrepareOnly, [switch]$UsePrebuiltRuntime, [string]$Probe)

$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')
. (Join-Path $PSScriptRoot 'HarnessFixtureProject.ps1')
$validationRoot = Join-Path $engineRoot 'LocalWork/Verification/Refactoring/ParticleSystemRefactor/ProductValidation'
$gameRoot = New-HarnessFixtureProject -Destination (Join-Path $validationRoot 'Sandbox') -Fixture Particles
Copy-Item -LiteralPath (Join-Path $engineRoot 'Project/Sandbox/Sandbox.vcxproj') -Destination $gameRoot -Force
@{ schemaVersion = 1; projectGuid = '4e454d41535345547061727470726f64'; name = 'Sandbox'
    assetsDirectory = 'GameAssets'; packagesDirectory = 'Packages'; projectSettingsDirectory = 'ProjectSettings' } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $gameRoot 'Sandbox.nemproject') -Encoding utf8

if (!$Probe) { $Probe = Join-Path $engineRoot 'Generated/Output/Debug/GameBuildServiceProbe/GameBuildServiceProbe.exe' }
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
    $manifest.outputRoot = Join-Path $engineRoot 'LocalWork/Tests/PSP'
    if ($UsePrebuiltRuntime) {
        $manifest | Add-Member -NotePropertyName prebuiltRuntime -NotePropertyValue $true -Force
        $manifest.projectPath = Join-Path $gameRoot 'Sandbox.vcxproj'
        $scripts = Join-Path $gameRoot 'Scripts'
        $null = New-Item -ItemType Directory -Path $scripts -Force
        $scriptProject = '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><OutputPath>../Managed/Release/</OutputPath><AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath></PropertyGroup></Project>'
        [IO.File]::WriteAllText((Join-Path $scripts 'GameScripts.csproj'), $scriptProject)
        $manifest.buildToolProject = ''
        $manifest.sourceRuntime = Join-Path $engineRoot 'Generated/Output/Release/Sandbox'
        $manifest.buildToolExecutable = Join-Path $engineRoot 'Generated/Output/Release/NEMBuildTool/NEMBuildTool.exe'
    }
    $manifest.gameWidth = 640
    $manifest.gameHeight = 360
    $manifest | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    if ($PrepareOnly) { Write-Output "製品入力の検証完了: $manifestPath"; return }

    $result = Invoke-HarnessProcess -FilePath 'powershell.exe' -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $engineRoot 'Tools/BuildGame.ps1'), '-ManifestPath', $manifestPath) `
        -WorkingDirectory $engineRoot -TimeoutSeconds 1800
    $result.output | Set-Content -LiteralPath (Join-Path $validationRoot 'Build.log') -Encoding utf8
    if ($result.exitCode -ne 0 -or $result.timedOut) { throw "製品の構築とCookに失敗しました: $($result.output)" }
    $productRoot = Join-Path $engineRoot 'LocalWork/Tests/PSP/ProductProbe'
    $pack = Join-Path $productRoot 'Data/Content.nempak'
    foreach ($name in @('particleGPUSimulate', 'particleGPUView', 'particleGPURibbonSelect',
        'particleGPUTrailUpdate', 'particleGPUTrailPrepare', 'particleGPUTrailDraw', 'particleGPUDraw')) {
        $sourcePath = Join-Path $engineRoot ("Project/Engine/Assets/Rendering/Builtin/Particle/GPU/$name.shader.json")
        $result = Invoke-HarnessProcess -FilePath $Probe -Arguments @('--packed-shader', $pack, $sourcePath) -WorkingDirectory $productRoot -TimeoutSeconds 60
        if ($result.exitCode -ne 0 -or $result.timedOut) { throw "製品のShaderを検証できません: $name / $($result.output)" }
        Write-Output $result.output
    }
    Write-Output "ParticleSystem製品の構築とCook検証完了: $productRoot"
} finally {
    $env:NEMENGINE_ROOT = $previousEngineRoot
}
