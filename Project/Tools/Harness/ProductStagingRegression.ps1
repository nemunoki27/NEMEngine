Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$operations = Join-Path $engineRoot 'Tools\ProductBuild'
foreach ($file in @('BuildFileOperations', 'StageProduct', 'CookProduct', 'WriteProductSettings')) {
    . (Join-Path $operations ($file + '.ps1'))
}
$root = Join-Path $engineRoot ('Generated\ProductStagingTests\' + [Guid]::NewGuid().ToString('N'))
$source = Join-Path $root 'Runtime'
$managed = Join-Path $source 'Managed'
$stage = Join-Path $root 'Stage'
New-Item -ItemType Directory -Path $managed, $stage | Out-Null
foreach ($file in @('Game.exe', 'NEMRuntime.dll', 'dxcompiler.dll', 'dxil.dll', 'Managed\GameScripts.dll')) {
    [System.IO.File]::WriteAllText((Join-Path $source $file), $file)
}
Write-Utf8Json (Join-Path $source 'nem.runtime-dependencies.json') @{
    schemaVersion = 1; files = @('NEMRuntime.dll', 'dxcompiler.dll', 'dxil.dll')
}
$asset = Join-Path $root 'Example.material.json'
Write-Utf8Json $asset @{ shaderGraph = 'old-graph'; passes = @() }
$manifest = [pscustomobject]@{
    files = @([pscustomobject]@{
        source = $asset; destination = 'GameAssets/Example.material.json'
        size = (Get-Item $asset).Length; sha256 = (Get-FileHash $asset).Hash
    })
    packages = @([pscustomobject]@{ name = 'com.nem.fixture'; version = '1'; contentHash = '1234' })
    startupScene = '12345678901234567890123456789012'; startupFullscreen = $false
    projectGuid = '12345678901234567890123456789013'; cookHash = 'fixture'
}
Copy-ProductFiles $manifest $source 'Game.exe' $stage 'Product.exe' $managed
if (Test-Path (Join-Path $stage 'dxcompiler.dll')) { throw '開発用DLLが製品へ混入しました' }
if (-not (Test-Path (Join-Path $stage 'Managed\GameScripts.dll'))) { throw 'Managed成果物がありません' }
function Invoke-TestCook {
    $global:LASTEXITCODE = 0
}
Invoke-ProductCook $stage 'Invoke-TestCook' (Join-Path $root 'Unused.json')
$material = Get-Content (Join-Path $stage 'GameAssets\Example.material.json') -Raw | ConvertFrom-Json
if ($material.PSObject.Properties.Name -contains 'shaderGraph') { throw 'Graph参照が残っています' }
Write-ProductSettings $manifest $stage 'Product' 'Product.exe'
$settings = Get-Content (Join-Path $stage 'ProjectSettings\Runtime\StartupScene.json') -Raw | ConvertFrom-Json
if ($settings.activeScene -ne $manifest.startupScene) { throw '起動Sceneが変化しました' }
$cook = Get-Content (Join-Path $stage '.nemCookManifest.json') -Raw | ConvertFrom-Json
foreach ($file in $cook.files) {
    Assert-FileHash (Join-Path $stage $file.path) $file.size $file.sha256
}
$rejected = $false
try { Get-ChildPath $stage '..\outside' | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw '配置先外への参照を許可しました' }
[System.IO.File]::AppendAllText($asset, 'changed')
$rejected = $false
try { Copy-ProductFiles $manifest $source 'Game.exe' $stage 'Product.exe' $managed } catch { $rejected = $true }
if (-not $rejected) { throw '入力変更を検出できませんでした' }
Write-Output 'Product staging passed, Cook process is simulated'
