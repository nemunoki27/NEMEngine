param(
    [switch]$ValidationImages,
    [switch]$AssimpObjects,
    [ValidateRange(1, 4096)][int]$MaximumCaptureMB = 128,
    [string]$PreserveCaptureDirectory = '',
    [string]$ResetCaptureDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Generated配下の実体だけを削除対象にする
function Resolve-ArtifactPath([string]$Root, [string]$Path) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $resolved = [IO.Path]::GetFullPath($Path)
    if ($resolved -ne $rootPath -and
        !$resolved.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "生成物の範囲外です: $resolved"
    }
    $ancestor = $resolved
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "リンク先は整理しません: $ancestor" }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
        if (!$ancestor) { break }
    }
    return $resolved
}

# 最終画像と中間画像の合計を上限内に収める
function Limit-ValidationImages([string]$Root, [long]$MaximumBytes, [string]$Preserve, [string]$Reset) {
    $resolved = Resolve-ArtifactPath $Root $Root
    $preservePath = if ($Preserve) { Resolve-ArtifactPath $Root $Preserve } else { '' }
    $resetPath = if ($Reset) { Resolve-ArtifactPath $Root $Reset } else { '' }
    if ($preservePath -eq $resolved -or $resetPath -eq $resolved) { throw '検証ルート全体は指定できません' }
    if (!(Test-Path -LiteralPath $resolved)) { return 0L }
    $images = @(Get-ChildItem -LiteralPath $resolved -Recurse -File |
        Where-Object { $_.Name -match '^\d+-(Game|Scene)(-.+)?\.(png|dds)$' })
    $removed = 0L
    $remaining = @()
    foreach ($image in $images) {
        if ($resetPath -and $image.DirectoryName -eq $resetPath) {
            $file = Resolve-ArtifactPath $Root $image.FullName
            Remove-Item -LiteralPath $file
            $removed += $image.Length
        } else { $remaining += $image }
    }
    # 今回明示した保存結果は残し、古い結果から減らす
    $budget = $MaximumBytes
    foreach ($image in $remaining) {
        if ($preservePath -and $image.DirectoryName -eq $preservePath) { $budget -= $image.Length }
    }
    foreach ($image in $remaining | Sort-Object LastWriteTimeUtc -Descending) {
        if ($preservePath -and $image.DirectoryName -eq $preservePath) { continue }
        if ($image.Length -le $budget) { $budget -= $image.Length; continue }
        $file = Resolve-ArtifactPath $Root $image.FullName
        Remove-Item -LiteralPath $file
        $removed += $image.Length
    }
    return $removed
}

# 構成から外れたAssimpソースの中間ファイルを除く
function Remove-UnusedAssimpObjects([string]$Root) {
    $project = Resolve-ArtifactPath $Root (Join-Path $Root 'Externals/assimp/code/assimp.vcxproj')
    $objects = Resolve-ArtifactPath $Root (Join-Path $Root 'Externals/assimp/code/assimp.dir')
    if (!(Test-Path -LiteralPath $project) -or !(Test-Path -LiteralPath $objects)) { return 0L }
    [xml]$document = [IO.File]::ReadAllText($project)
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($source in $document.GetElementsByTagName('ClCompile')) {
        if (!$source.HasAttribute('Include')) { continue }
        $name = [IO.Path]::GetFileNameWithoutExtension($source.GetAttribute('Include')) + '.obj'
        $null = $names.Add($name)
        foreach ($child in $source.ChildNodes) {
            if ($child.LocalName -eq 'ObjectFileName' -and $child.InnerText.EndsWith('.obj')) {
                $null = $names.Add([IO.Path]::GetFileName($child.InnerText))
            }
        }
    }
    if (!$names.Count) { throw 'Assimpのコンパイル対象を確認できません' }
    $removed = 0L
    foreach ($object in Get-ChildItem -LiteralPath $objects -Recurse -File -Filter '*.obj') {
        if ($names.Contains($object.Name)) { continue }
        $file = Resolve-ArtifactPath $Root $object.FullName
        Remove-Item -LiteralPath $file
        $removed += $object.Length
    }
    return $removed
}

$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$generatedRoot = Join-Path $engineRoot 'Generated'
$removedBytes = 0L
if ($ValidationImages -and !(Get-Process NEMEditor -ErrorAction SilentlyContinue)) {
    $removedBytes += Limit-ValidationImages (Join-Path $generatedRoot 'Refactoring') ($MaximumCaptureMB * 1MB) `
        $PreserveCaptureDirectory $ResetCaptureDirectory
}
if ($AssimpObjects -and !(Get-Process cl,link,lib -ErrorAction SilentlyContinue)) {
    $removedBytes += Remove-UnusedAssimpObjects $generatedRoot
}
if ($removedBytes) { Write-Output ('[生成物整理] {0:N1} MBを削減しました' -f ($removedBytes / 1MB)) }
