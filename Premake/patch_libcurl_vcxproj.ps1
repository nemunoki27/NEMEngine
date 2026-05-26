param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot
)

$build = [IO.Path]::GetFullPath($BuildRoot)
$encoding = New-Object System.Text.UTF8Encoding($false)

Get-ChildItem -LiteralPath $build -Recurse -Filter *.vcxproj | ForEach-Object {
    $content = Get-Content -LiteralPath $_.FullName -Raw
    $updated = [regex]::Replace(
        $content,
        '<IntDir([^>]*)>\$\(Platform\)\\\$\(Configuration\)\\\$\(ProjectName\)\\</IntDir>',
        {
            param($match)
            '<IntDir' + $match.Groups[1].Value + '>' + $build + '\Intermediate\$(ProjectName)\$(Configuration)\</IntDir>'
        })

    if ($_.Name -eq 'ZERO_CHECK.vcxproj' -and $updated -notmatch '<OutDir Condition=') {
        $outDirs =
            '    <OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|x64''">' + $build + '\Output\$(ProjectName)\Debug\</OutDir>' + "`r`n" +
            '    <OutDir Condition="''$(Configuration)|$(Platform)''==''Release|x64''">' + $build + '\Output\$(ProjectName)\Release\</OutDir>' + "`r`n" +
            '    <OutDir Condition="''$(Configuration)|$(Platform)''==''MinSizeRel|x64''">' + $build + '\Output\$(ProjectName)\MinSizeRel\</OutDir>' + "`r`n" +
            '    <OutDir Condition="''$(Configuration)|$(Platform)''==''RelWithDebInfo|x64''">' + $build + '\Output\$(ProjectName)\RelWithDebInfo\</OutDir>' + "`r`n"

        $updated = [regex]::Replace(
            $updated,
            '(<_ProjectFileVersion>[^<]+</_ProjectFileVersion>\s*)',
            '$1' + $outDirs,
            1)
    }

    if ($updated -ne $content) {
        [IO.File]::WriteAllText($_.FullName, $updated, $encoding)
    }
}
