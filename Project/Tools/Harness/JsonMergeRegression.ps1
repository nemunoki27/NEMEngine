# マージ本体と競合レポートの保存・復旧を実行ファイルで確認する
param([string]$Configuration = 'Develop')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$executable = Join-Path $repoRoot "Generated/Output/$Configuration/NEMBuildTool/NEMBuildTool.exe"
$fixtureRoot = Join-Path $repoRoot 'Generated/TestFixtures'
$directory = Join-Path $fixtureRoot ('Merge_' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($directory) | Out-Null
$basePath = Join-Path $directory 'base.json'
$oursPath = Join-Path $directory 'ours.json'
$theirsPath = Join-Path $directory 'theirs.json'
$outputPath = Join-Path $directory 'result.json'
$reportPath = $outputPath + '.merge-conflicts.json'

function Write-Document([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Invoke-Merge([int]$Expected) {
    $result = Invoke-HarnessProcess $executable @('--merge-json', $basePath, $oursPath, $theirsPath, $outputPath)
    if ($result.exitCode -ne $Expected) { throw "終了コード $($result.exitCode): $($result.output)" }
}
try {
    Write-Document $basePath '{"value":1}'
    Write-Document $oursPath '{"value":2}'
    Write-Document $theirsPath '{"value":3}'
    Invoke-Merge 7
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ($report.conflicts.Count -ne 1) { throw '競合レポートがありません' }
    Write-Host '[PASS] 競合結果とレポートを保存する'

    Write-Document $oursPath '{"value":1}'
    Invoke-Merge 0
    if (Test-Path -LiteralPath $reportPath) { throw '古い競合レポートが残っています' }
    $before = [IO.File]::ReadAllText($outputPath)
    Write-Host '[PASS] 解決済みのレポートを除去する'

    Write-Document $oursPath '{"value":4}'
    Write-Document $theirsPath '{"value":5}'
    $locked = [IO.File]::Open($outputPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { Invoke-Merge 6 } finally { $locked.Dispose() }
    if ([IO.File]::ReadAllText($outputPath) -ne $before -or (Test-Path -LiteralPath $reportPath)) {
        throw '後続の保存失敗で先行変更が復旧されていません'
    }
    Write-Host '[PASS] 本体の置換失敗で先行レポートを復旧する'
    Invoke-Merge 7
    Write-Host '[PASS] 復旧後に再保存できる'

    Write-Document $basePath 'null'
    Write-Document $oursPath '{"value":6}'
    Write-Document $theirsPath 'null'
    Invoke-Merge 0
    $before = [IO.File]::ReadAllText($outputPath)
    Write-Document $oursPath '{'
    Invoke-Merge 6
    if ([IO.File]::ReadAllText($outputPath) -ne $before) { throw '入力の解析失敗で出力が変わりました' }
    Write-Host '[PASS] null入力と解析失敗を区別する'
} finally {
    # 作成したfixtureの範囲を確認してから削除する
    $resolved = [IO.Path]::GetFullPath($directory)
    $parent = [IO.Path]::GetFullPath($fixtureRoot) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase)) { throw 'fixtureの削除先が範囲外です' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
