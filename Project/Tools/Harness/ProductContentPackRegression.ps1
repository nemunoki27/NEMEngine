param([string]$BuildTool = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (!$BuildTool) { $BuildTool = Join-Path $repo 'Generated/Output/Release/NEMBuildTool/NEMBuildTool.exe' }
$work = Join-Path $repo ('LocalWork/Tests/ProductContentPack/' + [Guid]::NewGuid().ToString('N'))
$stage = Join-Path $work '作業元'
$product = Join-Path $work '再配置した製品/Data'
$archive = Join-Path $work 'Content.nempak'
$guid = '0123456789abcdef0123456789abcdef'
$scene = '123456789abcdef0123456789abcdef0'
$utf8 = [Text.UTF8Encoding]::new($false)

function Write-Document([string]$Name, $Value) {
    $path = Join-Path $stage $Name
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
    [IO.File]::WriteAllText($path, ($Value | ConvertTo-Json -Depth 8), $utf8)
}
function Assert-Verify([string]$Path, [string]$Expected, [bool]$ShouldPass) {
    & $BuildTool --verify-product $Path $Expected
    if (($LASTEXITCODE -eq 0) -ne $ShouldPass) { throw "製品パックの照合結果が異なります: $Path" }
}

[IO.Directory]::CreateDirectory((Join-Path $stage 'Engine/Assets')) | Out-Null
[IO.Directory]::CreateDirectory($product) | Out-Null
Write-Document 'Sandbox.nemproject' @{
    schemaVersion = 1; projectGuid = $guid; name = 'Sandbox'; assetsDirectory = 'GameAssets'
    packagesDirectory = 'Packages'; projectSettingsDirectory = 'ProjectSettings'
}
Write-Document 'GameAssets/シーン/Main.scene.json' @{ Entities = @(); Scene = 'fixture' }
Write-Document 'GameAssets/シーン/Main.scene.json.meta' @{ schemaVersion = 2; guid = $scene; type = 'Scene'; settings = @{} }
Write-Document 'ProjectSettings/Runtime/Game.json' @{ gameName = 'Fixture'; gameWidth = 320; gameHeight = 240 }
Write-Document 'ProjectSettings/Runtime/StartupScene.json' @{ activeScene = $scene }
Write-Document 'Packages/manifest.json' @{ schemaVersion = 1; dependencies = @{} }
Write-Document 'Packages/packages-lock.json' @{ schemaVersion = 1; dependencies = @{} }

$repeatBytes = New-Object byte[] 65536
foreach ($name in 'repeat-a','repeat-b') {
    [IO.File]::WriteAllBytes((Join-Path $stage "ProjectSettings/$name.bin"), $repeatBytes)
}

$previousPortable = $env:NEMENGINE_PORTABLE
try {
    $env:NEMENGINE_PORTABLE = '1'
    & $BuildTool --pack-product $stage $archive
    if ($LASTEXITCODE -ne 0) { throw '製品パックの作成に失敗しました' }
} finally { $env:NEMENGINE_PORTABLE = $previousPortable }

$pack = Join-Path $product 'Content.nempak'
Move-Item -LiteralPath $archive -Destination $pack
Assert-Verify $pack $guid $true
Assert-Verify $pack $scene $false
Assert-Verify (Join-Path $work 'missing.nempak') $guid $false
$bytes = [IO.File]::ReadAllBytes($pack)
if ($bytes.Length -ge $repeatBytes.Length -or [Text.Encoding]::ASCII.GetString($bytes, 0, 8) -ne 'NEMPAK02') {
    throw '製品パックの可逆圧縮が適用されていません'
}
# このfixtureの先頭はGameAssetsのScene文書
if (($bytes[80] -band 0xe0) -ne 0xa0) { throw 'シーンがバイナリのマップ形式で格納されていません' }
$contents = [Text.Encoding]::UTF8.GetString($bytes)
if ($contents.Contains('.nemproject') -or $contents.Contains('.meta')) { throw '編集用のファイルが製品パックに含まれています' }
foreach ($offset in @(0, 80, $($bytes.Length - 1))) {
    $damaged = Join-Path $work "damaged-$offset.nempak"
    $bytes[$offset] = $bytes[$offset] -bxor 1
    [IO.File]::WriteAllBytes($damaged, $bytes)
    Assert-Verify $damaged $guid $false
    $bytes[$offset] = $bytes[$offset] -bxor 1
}
$short = Join-Path $work 'truncated.nempak'
[IO.File]::WriteAllBytes($short, [byte[]]$bytes[0..($bytes.Length - 2)])
Assert-Verify $short $guid $false
$files = @(Get-ChildItem -LiteralPath (Split-Path -Parent $product) -Recurse -File)
if ($files.Count -ne 1 -or $files[0].FullName -ne $pack) { throw '検証時に製品の内容が展開されました' }
Write-Output '製品パックの作成、移動、日本語パス、破損検出、プロジェクト照合を確認しました'
