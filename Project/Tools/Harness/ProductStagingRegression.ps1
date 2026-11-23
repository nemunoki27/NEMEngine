Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$operations = Join-Path $engineRoot 'Tools\ProductBuild'
foreach ($file in @('BuildFileOperations', 'StageProduct', 'CookProduct', 'WriteProductSettings', 'PublishProduct')) {
    . (Join-Path $operations ($file + '.ps1'))
}
$root = Join-Path $engineRoot ('LocalWork\Tests\ProductStagingTests\' + [Guid]::NewGuid().ToString('N'))
$source = Join-Path $root 'Runtime'
$managed = Join-Path $source 'Managed'
$stage = Join-Path $root 'Stage'
New-Item -ItemType Directory -Path $managed, $stage | Out-Null
foreach ($file in @('Game.exe', 'NEMRuntime.dll', 'nethost.dll', 'dxcompiler.dll', 'dxil.dll', 'Managed\GameScripts.dll', 'Managed\NEM.ScriptCore.dll', 'Managed\NEM.ScriptCore.deps.json', 'Managed\NEM.ScriptCore.runtimeconfig.json', 'Managed\Extra.dll', 'Managed\Stale.dll')) {
    [System.IO.File]::WriteAllText((Join-Path $source $file), $file)
}
# 実際のPEリソースへ配布表記を書き込む
Copy-Item -LiteralPath (Get-Process -Id $PID).Path -Destination (Join-Path $source 'Game.exe') -Force
Write-Utf8Json (Join-Path $source 'nem.runtime-dependencies.json') @{
    schemaVersion = 1; files = @('NEMRuntime.dll', 'nethost.dll', 'dxcompiler.dll', 'dxil.dll')
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
    gameWidth = 1280; gameHeight = 720
    projectGuid = '12345678901234567890123456789013'; cookHash = 'fixture'
}
Copy-ProductFiles $manifest $source 'Game.exe' $stage 'Product.exe' $managed
if (Test-Path (Join-Path $stage 'dxcompiler.dll')) { throw '開発用DLLが製品へ混入しました' }
Write-Utf8Json (Join-Path $managed 'GameScripts.deps.json') @{
    runtimeTarget=@{name='test'}
    targets=@{test=@{'GameScripts/1'=@{runtime=@{'GameScripts.dll'=@{}}}; 'Example/1'=@{runtime=@{'lib/net10.0/Extra.dll'=@{}}}}}
}
Copy-ProductGameAssemblies $managed (Join-Path $stage 'Managed')
if (!(Test-Path (Join-Path $stage 'Managed/Extra.dll')) -or (Test-Path (Join-Path $stage 'Managed/Stale.dll'))) { throw 'C#依存ファイルの選別に失敗しました' }
if (-not (Test-Path (Join-Path $stage 'Managed\GameScripts.dll'))) { throw 'Managed成果物がありません' }
$nativeStage = Join-Path $root 'NativeStage'
New-Item -ItemType Directory -Path $nativeStage | Out-Null
Copy-ProductFiles $manifest $source 'Game.exe' $nativeStage 'Product.exe' $managed $false
Write-ProductSettings $manifest $nativeStage 'Product' 'Product.exe' $false
$nativeSettings = Get-Content (Join-Path $nativeStage 'ProjectSettings/Runtime/Game.json') -Raw | ConvertFrom-Json
if ($nativeSettings.managedScripts -ne $false) { throw 'ネイティブ製品の実行設定が一致しません' }
function Invoke-TestCook {
    param([string]$mode, [string]$inputs, [string]$output)
    $snapshot = Get-Content -LiteralPath $inputs -Raw | ConvertFrom-Json
    if ($snapshot.gameRoot -ne [IO.Path]::GetFullPath($stage) -or
        $snapshot.files[0].source -ne (Join-Path $stage 'GameAssets/Example.material.json')) {
        throw 'Cookの入力が配置済みファイルを参照していません'
    }
    # 元Assetの変更は確定済みのCook入力へ混ざらない
    [IO.File]::AppendAllText($asset, 'changed during Cook')
    $global:LASTEXITCODE = 0
}
$manifestPath = Join-Path $root 'Inputs.json'
$manifest | Add-Member -NotePropertyName gameRoot -NotePropertyValue $root
Write-Utf8Json $manifestPath $manifest
function Invoke-FailedCook {
    Write-Output '[シェーダーCook] GameAssets/Test.shadergraph.json / 深度パスの基底パイプラインを読み込めません'
    $global:LASTEXITCODE = 11
}
$cookFailure = ''
try { Invoke-ProductCook $stage 'Invoke-FailedCook' $manifestPath } catch { $cookFailure = $_.Exception.Message }
if ($cookFailure -notmatch 'GameAssets/Test\.shadergraph\.json / 深度パス') { throw 'Cookの失敗理由がビルド画面に渡されていません' }
Invoke-ProductCook $stage 'Invoke-TestCook' $manifestPath
$material = Get-Content (Join-Path $stage 'GameAssets\Example.material.json') -Raw | ConvertFrom-Json
if ($material.PSObject.Properties.Name -contains 'shaderGraph') { throw 'Graph参照が残っています' }
Write-ProductSettings $manifest $stage 'Product' 'Product.exe'
$settings = Get-Content (Join-Path $stage 'ProjectSettings\Runtime\StartupScene.json') -Raw | ConvertFrom-Json
if ($settings.activeScene -ne $manifest.startupScene) { throw '起動Sceneが変化しました' }
$gameSettings = Get-Content (Join-Path $stage 'ProjectSettings\Runtime\Game.json') -Raw | ConvertFrom-Json
if ($gameSettings.gameWidth -ne 1280 -or $gameSettings.gameHeight -ne 720) { throw 'ゲーム画像サイズが変化しました' }
$dotnet = Join-Path $root 'Toolchain'
foreach ($relative in 'host/fxr/10.0.1/hostfxr.dll', 'shared/Microsoft.NETCore.App/10.0.1/coreclr.dll',
    'shared/Microsoft.NETCore.App/10.0.1/README.txt', 'shared/Microsoft.NETCore.App/10.0.1/fr/System.resources.dll', 'shared/Microsoft.NETCore.App/10.0.1/fr/Nested.md', 'shared/Microsoft.NETCore.App/10.0.1/createdump.exe', 'shared/Microsoft.NETCore.App/10.0.1/mscordbi.dll', 'shared/Microsoft.NETCore.App/10.0.1/.version', 'shared/Microsoft.NETCore.App/8.0.1/coreclr.dll', 'sdk/10.0.100/sdk.dll', 'LICENSE.txt', 'ThirdPartyNotices.txt') {
    $file = Get-ChildPath $dotnet $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force | Out-Null
    [IO.File]::WriteAllText($file, $relative)
}
Write-Utf8Json (Join-Path $stage 'Managed/NEM.ScriptCore.runtimeconfig.json') @{
    runtimeOptions = @{ framework = @{ name = 'Microsoft.NETCore.App'; version = '10.0.0' } }
}
function Invoke-TestPack {
    param([string]$mode,[string]$inputPath,[string]$outputOrGuid,[string]$inputs)
    if ($mode -eq '--pack-product') {
        [IO.File]::WriteAllText($outputOrGuid, $manifest.projectGuid)
        $global:LASTEXITCODE = 0
    }
    else { $global:LASTEXITCODE = [int]([IO.File]::ReadAllText($inputPath) -ne $outputOrGuid) }
}
$ready = Join-Path $root 'Ready'
New-PackedProduct $manifest $stage $ready 'Product.exe' 'Invoke-TestPack' $manifestPath $dotnet
if ((@(Get-ChildItem -LiteralPath $ready -Name | Sort-Object) -join ',') -ne 'Data,Product.exe') { throw '製品ルートに不要なファイルがあります' }
$raw = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $ready 'Product.exe')))
if (!$raw.Contains('Native .NET hosting') -or !$raw.Contains('.NET / ThirdPartyNotices.txt')) { throw '配布表記をEXEから読み取れません' }
if (@(Get-ChildItem -LiteralPath $ready -Recurse -File -Filter '*.txt').Count) { throw '配布表記が外部ファイルへ残っています' }
foreach ($file in 'Data/Runtime/NEMRuntime.dll','Data/Runtime/Managed/GameScripts.dll','Data/Content.nempak','Data/Runtime/DotNet/shared/Microsoft.NETCore.App/10.0.1/fr/System.resources.dll','Data/Runtime/DotNet/host/fxr/10.0.1/hostfxr.dll') {
    if (-not (Test-Path (Join-Path $ready $file))) { throw "製品に必要なファイルがありません: $file" }
}
foreach ($file in 'Data/Runtime/DotNet/shared/Microsoft.NETCore.App/10.0.1/README.txt','Data/Runtime/DotNet/shared/Microsoft.NETCore.App/10.0.1/fr/Nested.md','Data/Runtime/DotNet/sdk','Data/Runtime/DotNet/shared/Microsoft.NETCore.App/8.0.1','Data/Runtime/dxcompiler.dll','Data/Runtime/DotNet/shared/Microsoft.NETCore.App/10.0.1/createdump.exe','Data/Runtime/DotNet/shared/Microsoft.NETCore.App/10.0.1/mscordbi.dll','Data/Runtime/DotNet/shared/Microsoft.NETCore.App/10.0.1/.version') {
    if (Test-Path (Join-Path $ready $file)) { throw "製品に不要なファイルがあります: $file" }
}
$nativeReady = Join-Path $root 'NativeReady'
New-PackedProduct $manifest $nativeStage $nativeReady 'Product.exe' 'Invoke-TestPack' $manifestPath $dotnet $false
$nativeFiles = @(Get-ChildItem -LiteralPath $nativeReady -Recurse -File | ForEach-Object Name | Sort-Object)
if (($nativeFiles -join ',') -ne 'Content.nempak,NEMRuntime.dll,Product.exe') { throw 'ネイティブ製品のファイル構成が一致しません' }
$previous = Join-Path $root 'Product'
New-Item -ItemType Directory -Path (Join-Path $previous 'Saved') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $previous 'Saved/save.dat'), 'previous')
Copy-Item -LiteralPath (Join-Path $stage 'Product.nemproject') -Destination $previous
Write-Utf8Json (Join-Path $previous '.nemBuildManifest.json') @{ schemaVersion = 2 }
Publish-PackedProduct $root 'Product' 'Ready' 'Backup' 'Invoke-TestPack' $manifest.projectGuid 'Product.exe'
if (-not (Test-Path (Join-Path $previous 'Data/Content.nempak')) -or -not (Test-Path (Join-Path $root 'Backup/Saved/save.dat'))) {
    throw '旧製品の更新、またはセーブデータの保持に失敗しました'
}
$rejected = $false
try { Publish-PackedProduct $root 'Product' 'Missing' 'AnotherBackup' 'Invoke-TestPack' 'wrong-guid' 'Product.exe' } catch { $rejected = $true }
if (-not $rejected -or -not (Test-Path (Join-Path $previous 'Data/Content.nempak'))) { throw '別製品への上書きを拒否できませんでした' }
$rejected = $false
try { Publish-PackedProduct $root 'Product' 'Missing' 'AnotherBackup' 'Invoke-TestPack' $manifest.projectGuid 'Product.exe' } catch { $rejected = $true }
if (!$rejected -or !(Test-Path (Join-Path $previous 'Data/Content.nempak')) -or (Test-Path (Join-Path $root 'AnotherBackup'))) {
    throw '更新に失敗したときに旧製品が復元されていません'
}
$rejected = $false
try { Get-ChildPath $stage '..\outside' | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw '配置先外への参照を許可しました' }
[System.IO.File]::AppendAllText($asset, 'changed')
$rejected = $false
try { Copy-ProductFiles $manifest $source 'Game.exe' $stage 'Product.exe' $managed } catch { $rejected = $true }
if (-not $rejected) { throw '入力変更を検出できませんでした' }
Write-Output 'Product staging passed, Cook process is simulated'
