param(
    [string]$SourceDirectory = '',
    [string]$OutputArchive = '',
    [string]$ArchivePath = '',
    [Parameter(Mandatory = $true)][string]$InfoFile
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$OutputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
. (Join-Path $PSScriptRoot '../FileSystem/DirectorySafety.ps1')

function Assert-ArchivePath([string]$Path) {
    if (!$Path -or [IO.Path]::IsPathRooted($Path) -or $Path.Contains(':')) { throw 'ZIPの相対パスが不正です' }
    foreach ($part in $Path.Replace('\', '/').TrimEnd('/').Split('/')) {
        if (!$part -or $part -eq '..' -or $part -eq '.' -or $part.EndsWith(' ') -or $part.EndsWith('.') -or
            $part -match '[<>"|?*\x00-\x1f]' -or $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') {
            throw "ZIPの保存先名が不正です: $Path"
        }
    }
}

# 開始シーンの参照と保存された本体が一致することを確認する
function Assert-SampleStartup([string]$Root, [object]$Manifest) {
    $property = $Manifest.PSObject.Properties['startupScene']
    if (!$property -or $property.Value -isnot [string] -or $property.Value -notmatch '^[a-fA-F0-9]{32}$' -or
        $property.Value -eq '00000000000000000000000000000000') { throw 'サンプルの開始シーンGUIDが不正です' }
    $startup = $property.Value
    $matchingScenes = 0
    foreach ($content in @('GameAssets', 'Packages')) {
        $directory = Join-Path $Root $content
        if (!(Test-Path -LiteralPath $directory -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $directory -File -Recurse -Force -Filter '*.scene.json.meta') {
            $metadata = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
            $reference = $metadata.PSObject.Properties['guid']
            if (!$reference -or $reference.Value -isnot [string] -or $reference.Value -ne $startup) { continue }
            $scene = $file.FullName.Substring(0, $file.FullName.Length - '.meta'.Length)
            if (!(Test-Path -LiteralPath $scene -PathType Leaf)) { throw 'サンプルの開始シーン本体がありません' }
            ++$matchingScenes
        }
    }
    if ($matchingScenes -eq 0) { throw 'サンプルの開始シーンに対応するmetaがありません' }
    if ($matchingScenes -ne 1) { throw 'サンプルの開始シーンGUIDが重複しています' }
}

if ($SourceDirectory) {
    if (!$OutputArchive -or $ArchivePath) { throw 'アーカイブの出力先を指定してください' }
    $source = [IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\', '/')
    Assert-DirectoryTreeWithoutLinks $source
    $manifest = Get-Content -LiteralPath (Join-Path $source 'template.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1 -or $manifest.kind -ne 'Sample') { throw 'サンプルの保存形式が不正です' }
    Assert-SampleStartup $source $manifest
    $archiveFull = [IO.Path]::GetFullPath($OutputArchive)
    if ($archiveFull.StartsWith($source + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'アーカイブの出力先は保存元の外に指定してください' }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($archiveFull)) -Force | Out-Null
    $temporary = $archiveFull + '.writing'
    if (Test-Path -LiteralPath $temporary) { throw '作成途中のアーカイブが残っています' }
    try {
        $zip = [IO.Compression.ZipFile]::Open($temporary, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($file in Get-ChildItem -LiteralPath $source -File -Recurse -Force | Sort-Object FullName) {
                $relative = $file.FullName.Substring($source.Length + 1).Replace('\', '/')
                Assert-ArchivePath $relative
                $parts = $relative.Split('/')
                if ($relative -notin @('template.json', '.gitignore', '.gitattributes') -and $parts[0] -notin @('GameAssets', 'ProjectSettings', 'Scripts', 'Packages')) {
                    throw "サンプルの保存元に対象外のファイルがあります: $relative"
                }
                if (@($parts | Where-Object { $_ -in @('.git', '.vs', 'obj', 'bin', 'Library', 'Saved', 'UserSettings', 'Generated', 'Managed') }).Count) {
                    throw "サンプルに生成物が含まれています: $relative"
                }
                $entry = $zip.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
                $sourceStream = [IO.File]::OpenRead($file.FullName)
                $archiveStream = $entry.Open()
                try { $sourceStream.CopyTo($archiveStream) } finally { $archiveStream.Dispose(); $sourceStream.Dispose() }
            }
        } finally { $zip.Dispose() }
        Move-Item -LiteralPath $temporary -Destination $archiveFull
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
    $ArchivePath = $archiveFull
}

if (!$ArchivePath -or [IO.Path]::GetExtension($ArchivePath) -ne '.zip') { throw 'ZIPファイルを指定してください' }
$archiveFull = [IO.Path]::GetFullPath($ArchivePath)
$archiveItem = Get-Item -LiteralPath $archiveFull
if ($archiveItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'リンクされたZIPは使用できません' }
$paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$zip = [IO.Compression.ZipFile]::OpenRead($archiveFull)
[long]$unpacked = 0
$files = 0
try {
    foreach ($entry in $zip.Entries) {
        Assert-ArchivePath $entry.FullName
        $path = $entry.FullName.Replace('\', '/').TrimEnd('/')
        if (!$paths.Add($path) -or (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000 -or
            ($entry.ExternalAttributes -band [int][IO.FileAttributes]::ReparsePoint)) { throw 'ZIPに重複した保存先またはリンクが含まれています' }
        if ([long]::MaxValue - $unpacked -lt $entry.Length) { throw 'ZIPの展開容量が上限を超えています' }
        $unpacked += $entry.Length
        if (!$entry.FullName.EndsWith('/') -and !$entry.FullName.EndsWith('\')) { ++$files }
    }
} finally { $zip.Dispose() }
if (!$files -or $unpacked -le 0) { throw '空のZIPは公開できません' }
$information = @{ bytes = $archiveItem.Length; unpackedBytes = $unpacked; files = $files }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($InfoFile))) | Out-Null
[IO.File]::WriteAllText($InfoFile, ($information | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Write-Output ("アーカイブを検証しました: {0}ファイル" -f $files)
