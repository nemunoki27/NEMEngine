param([ValidateSet('Debug', 'Develop')][string]$Configuration = 'Develop')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$executable = Join-Path $repository "Generated/Output/$Configuration/NEMTests/NEMTests.exe"
$fixtures = Join-Path $repository 'LocalWork/Tests/TestFixtures'
$root = Join-Path $fixtures ('ShaderCache-' + [Guid]::NewGuid().ToString('N'))
$probe = Join-Path $root 'GameAssets/Probe'
$shaders = Join-Path $root 'GameAssets/Shaders'

function Test-Probe([string]$Name, [int]$Width, [int]$Compiles) {
    $result = Invoke-HarnessProcess -FilePath $executable -WorkingDirectory $root `
        -Arguments @('--shader-cache', "$Width") -TimeoutSeconds 60
    $compiled = [regex]::Matches($result.output, '\[ShaderCache\] compile ').Count
    $hits = [regex]::Matches($result.output, '\[ShaderCache\] hit ').Count
    if ($result.exitCode -ne 0 -or $compiled -ne $Compiles -or $hits -ne (4 - $Compiles)) {
        throw "$Name (exit=$($result.exitCode), compile=$compiled, hit=$hits): $($result.output)"
    }
    Write-Output "[PASS] $Name (compile=$compiled, hit=$hits)"
}

$portable = $env:NEMENGINE_PORTABLE
try {
    $env:NEMENGINE_PORTABLE = '1'
    $null = New-Item -ItemType Directory -Path $probe, $shaders -Force
    Copy-Item -LiteralPath (Join-Path $repository 'Project/Sandbox/Sandbox.nemproject') -Destination (Join-Path $root 'Probe.nemproject')
    $header = Join-Path $shaders 'cache-width.hlsli'
    [IO.File]::WriteAllText($header, '#define GROUP_WIDTH 8')
    [IO.File]::WriteAllText((Join-Path $probe 'cache-common.hlsli'), '#include "cache-width.hlsli"')
    [IO.File]::WriteAllText((Join-Path $probe 'cache.CS.hlsl'), @'
#include "cache-common.hlsli"
RWStructuredBuffer<uint> outputBuffer : register(u0);
[numthreads(GROUP_WIDTH, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { outputBuffer[id.x] = id.x; }
'@)
    Test-Probe '同時要求の初回コンパイル' 8 1
    Test-Probe '再起動後のDXILとReflection' 8 0
    $previous = [IO.File]::GetLastWriteTimeUtc($header)
    [IO.File]::WriteAllText($header, '#define GROUP_WIDTH 4')
    [IO.File]::SetLastWriteTimeUtc($header, $previous)
    Test-Probe 'サイズと更新時刻が同じ依存先の変更' 4 1
    $override = Join-Path $probe 'cache-width.hlsli'
    [IO.File]::WriteAllText($override, '#define GROUP_WIDTH 2')
    Test-Probe '検索順の高いIncludeの追加' 2 1
    Remove-Item -LiteralPath $override
    Test-Probe '上位Include削除後の再解決' 4 1
    $cache = @(Get-ChildItem -LiteralPath (Join-Path $root 'Library/ShaderCache') -Recurse -File -Filter '*.dxcache')
    if ($cache.Count -ne 1) { throw "キャッシュの個数が一致しません: $($cache.Count)" }
    $bytes = [IO.File]::ReadAllBytes($cache[0].FullName)
    $bytes[-1] = $bytes[-1] -bxor 255
    [IO.File]::WriteAllBytes($cache[0].FullName, $bytes)
    Test-Probe '破損キャッシュの再コンパイル' 4 1
    Test-Probe '修復結果の再利用' 4 0
} finally {
    $env:NEMENGINE_PORTABLE = $portable
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($root)) -eq $fixtures -and (Test-Path -LiteralPath $root)) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
