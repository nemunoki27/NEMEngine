# 画像の保存上限と削除対象の境界を調べる
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$scriptPath = Join-Path $engineRoot 'Tools/PruneGeneratedArtifacts.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
foreach ($name in @('Resolve-ArtifactPath', 'Limit-ValidationImages', 'Remove-UnusedAssimpObjects')) {
    $function = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $false)
    if ($null -eq $function) { throw "整理処理がありません: $name" }
    . ([scriptblock]::Create($function.Extent.Text))
}
function Assert-Result([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    Write-Output "[PASS] $Message"
}
$fixtureBase = Join-Path $engineRoot 'Generated/TestFixtures'
$fixture = [IO.Path]::GetFullPath((Join-Path $fixtureBase ('GeneratedArtifacts-' + [Guid]::NewGuid().ToString('N'))))
$generated = Join-Path $fixture 'Generated'
$images = Join-Path $generated 'Refactoring'
$one = Join-Path $images 'Old'
$two = Join-Path $images 'New'
try {
    $null = New-Item -ItemType Directory -Path $one, $two -Force
    $data = [byte[]]::new(1MB)
    $first = Join-Path $one '0-Game.png'
    $second = Join-Path $two '0-Game.png'
    $third = Join-Path $two '0-Scene-SceneColorMain.dds'
    [IO.File]::WriteAllBytes($first, $data)
    [IO.File]::WriteAllBytes($second, $data)
    [IO.File]::WriteAllBytes($third, $data)
    [IO.File]::SetLastWriteTimeUtc($first, [DateTime]::UtcNow.AddDays(-1))
    [IO.File]::WriteAllText((Join-Path $one 'Report.json'), '{}')
    [IO.File]::WriteAllText((Join-Path $one 'asset.png'), 'asset')
    $removed = Limit-ValidationImages $images (2MB) '' ''
    Assert-Result ($removed -eq 1MB -and !(Test-Path -LiteralPath $first) -and
        (Test-Path -LiteralPath $second) -and (Test-Path -LiteralPath $third)) '古い画像から容量を制限する'
    Assert-Result ((Test-Path -LiteralPath (Join-Path $one 'Report.json')) -and
        (Test-Path -LiteralPath (Join-Path $one 'asset.png'))) '記録と確認画像以外のファイルを残す'
    [IO.File]::WriteAllBytes($first, $data)
    $null = Limit-ValidationImages $images (1MB) $one ''
    Assert-Result ((Test-Path -LiteralPath $first) -and !(Test-Path -LiteralPath $second)) '今回の保存結果を優先する'
    $null = Limit-ValidationImages $images (1MB) '' $one
    Assert-Result (!(Test-Path -LiteralPath $first) -and (Test-Path -LiteralPath (Join-Path $one 'Report.json'))) '再実行時に古い画像を混ぜない'
    $rejected = $false
    try { $null = Limit-ValidationImages $images (1MB) '' $fixture } catch { $rejected = $true }
    Assert-Result $rejected '整理対象の範囲外を拒否する'
    $link = Join-Path $images 'Linked'
    $null = New-Item -ItemType Junction -Path $link -Target $one
    try {
        $rejected = $false
        try { $null = Resolve-ArtifactPath $images (Join-Path $link 'asset.png') } catch { $rejected = $true }
        Assert-Result $rejected 'リンク先の削除を拒否する'
    } finally { Remove-Item -LiteralPath $link -Force }
    $project = Join-Path $generated 'Externals/assimp/code'
    $objects = Join-Path $project 'assimp.dir/Debug'
    $null = New-Item -ItemType Directory -Path $objects -Force
    [IO.File]::WriteAllText((Join-Path $project 'assimp.vcxproj'),
        '<Project><ItemGroup><ClCompile Include="FBXImporter.cpp" /></ItemGroup></Project>')
    [IO.File]::WriteAllText((Join-Path $objects 'FBXImporter.obj'), 'keep')
    [IO.File]::WriteAllText((Join-Path $objects 'IFCReader.obj'), 'unused')
    [IO.File]::WriteAllText((Join-Path $objects 'assimp.pdb'), 'symbols')
    $removed = Remove-UnusedAssimpObjects $generated
    Assert-Result ($removed -eq 6 -and (Test-Path -LiteralPath (Join-Path $objects 'FBXImporter.obj')) -and
        !(Test-Path -LiteralPath (Join-Path $objects 'IFCReader.obj')) -and
        (Test-Path -LiteralPath (Join-Path $objects 'assimp.pdb'))) '未使用の中間ファイルだけを除く'
} finally {
    if (!$fixture.StartsWith([IO.Path]::GetFullPath($fixtureBase) + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw '検証先が範囲外です' }
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
