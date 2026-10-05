Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
. (Join-Path $engineRoot 'Tools\SDK\SDKFileOperations.ps1')
$root = Join-Path $engineRoot ('Generated\SDKPublicationTests\' + [Guid]::NewGuid().ToString('N'))
$destination = Join-Path $root 'SDK'
$stage = Join-Path $root '.nem-sdk-stage-test'
New-Item -ItemType Directory -Path $destination, $stage -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $destination 'sdk_version.json'), '{"old":true}')
[IO.File]::WriteAllText((Join-Path $destination 'obsolete.txt'), 'obsolete')
[IO.File]::WriteAllText((Join-Path $stage 'sdk_version.json'), '{"new":true}')
Publish-SDKDirectory $stage $destination
if (Test-Path -LiteralPath (Join-Path $destination 'obsolete.txt')) { throw '旧SDKのファイルが残りました' }
New-Item -ItemType Directory -Path (Join-Path $destination 'Bin'), $stage -Force | Out-Null
$binary = Join-Path $destination 'Bin\locked.dll'
[IO.File]::WriteAllText($binary, 'held')
[IO.File]::WriteAllText((Join-Path $stage 'sdk_version.json'), '{"next":true}')
$lock = [IO.File]::Open($binary, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    $failed = $false
    try { Publish-SDKDirectory $stage $destination } catch { $failed = $true }
    if (-not $failed -or [IO.File]::ReadAllText((Join-Path $destination 'sdk_version.json')) -cne '{"new":true}') {
        throw '使用中の旧SDKを保持できませんでした'
    }
} finally { $lock.Dispose() }
# リンクされた親の内側は旧SDKの移動前に拒否する
$link = Join-Path $root 'LinkedParent'
$target = Join-Path $root 'ActualParent'
New-Item -ItemType Directory -Path $target | Out-Null
New-Item -ItemType Junction -Path $link -Target $target | Out-Null
try {
    $linkedStage = Join-Path $link '.nem-sdk-stage-linked'
    New-Item -ItemType Directory -Path $linkedStage | Out-Null
    [IO.File]::WriteAllText((Join-Path $linkedStage 'sdk_version.json'), '{"linked":true}')
    $rejected = $false
    try { Publish-SDKDirectory $linkedStage (Join-Path $link 'SDK') } catch { $rejected = $true }
    if (-not $rejected -or (Test-Path -LiteralPath (Join-Path $target 'SDK'))) {
        throw 'リンクされた親へのSDK公開を拒否しませんでした'
    }
} finally {
    # この検証が作成したリンクだけを外す
    Remove-Item -LiteralPath $link -Force
}
Write-Output 'SDK staged publication and in-use protection passed'
