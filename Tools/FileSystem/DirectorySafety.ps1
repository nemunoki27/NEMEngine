# コピー元と置換先が同じ階層を共有しないか確認する
function Assert-SeparateDirectories([string]$Source, [string]$Destination) {

    $sourceFull = [IO.Path]::GetFullPath($Source).TrimEnd('\', '/')
    $destinationFull = [IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')
    if ($sourceFull.Equals($destinationFull, [StringComparison]::OrdinalIgnoreCase) -or
        $sourceFull.StartsWith($destinationFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $destinationFull.StartsWith($sourceFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw '複製元と取り込み先が重なっています'
    }
}

# リンクされた親や子を辿らず操作対象を検査する
function Assert-DirectoryTreeWithoutLinks([string]$Path, [string[]]$ExcludedPaths = @()) {

    $rootPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $ancestor = $rootPath
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "複製対象にリンクされたパスは使用できません: $ancestor"
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }

    $directories = [Collections.Generic.Stack[string]]::new()
    if (Test-Path -LiteralPath $rootPath -PathType Container) { $directories.Push($rootPath) }
    while ($directories.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $directories.Pop() -Force) {
            if ($item.FullName -in $ExcludedPaths) { continue }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "複製対象にリンクされたパスは使用できません: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $directories.Push($item.FullName) }
        }
    }
}
