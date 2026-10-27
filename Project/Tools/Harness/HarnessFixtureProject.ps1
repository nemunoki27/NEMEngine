function New-HarnessFixtureProject {
    param(
        [Parameter(Mandatory)][string]$Destination,
        [ValidateSet('Decals', 'Particles')][string]$Fixture = 'Decals'
    )

    $fixtureEngineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
    $generatedRoot = [IO.Path]::GetFullPath((Join-Path $fixtureEngineRoot 'Generated'))
    $destinationRoot = [IO.Path]::GetFullPath($Destination)
    if (!$destinationRoot.StartsWith($generatedRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw '検証用Projectの配置先が不正です' }

    # 検証データはSandboxへ書き戻さない
    $source = Join-Path $fixtureEngineRoot "Project/Tests/Fixtures/$Fixture/GameAssets"
    if (!(Test-Path -LiteralPath $source -PathType Container)) { throw "検証データがありません: $source" }
    if (Test-Path -LiteralPath $destinationRoot) { Remove-Item -LiteralPath $destinationRoot -Recurse -Force }
    $null = New-Item -ItemType Directory -Path $destinationRoot -Force
    Copy-Item -LiteralPath $source -Destination (Join-Path $destinationRoot 'GameAssets') -Recurse

    # RuntimePathsが独立したゲームProjectとして解決できるようにする
    $descriptor = @{
        schemaVersion = 1; projectGuid = 'd0d59331ff0a4eb589ea7801cb52f208'; name = 'Sandbox'
        sceneStorage = 'Monolithic'; assetsDirectory = 'GameAssets'
        packagesDirectory = 'Packages'; projectSettingsDirectory = 'ProjectSettings'
    }
    $descriptor | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destinationRoot 'Sandbox.nemproject') -Encoding utf8
    $settings = Join-Path $destinationRoot 'ProjectSettings/Runtime'
    $null = New-Item -ItemType Directory -Path $settings -Force
    $scene = Get-ChildItem -LiteralPath (Join-Path $destinationRoot 'GameAssets/Scenes') -Recurse -File |
        Where-Object { $_.Name.EndsWith('.scene.json.meta') } | Sort-Object FullName | Select-Object -First 1
    $sceneMeta = Get-Content -LiteralPath $scene.FullName -Raw | ConvertFrom-Json
    @{ activeScene = $sceneMeta.guid } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $settings 'StartupScene.json') -Encoding utf8
    return $destinationRoot
}
