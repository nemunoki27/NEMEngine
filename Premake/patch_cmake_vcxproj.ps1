param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot
)

$build = [IO.Path]::GetFullPath($BuildRoot)
$buildXML = [Security.SecurityElement]::Escape($build)
$encoding = New-Object System.Text.UTF8Encoding($false)

Get-ChildItem -LiteralPath $build -Recurse -Filter *.vcxproj | ForEach-Object {
    $content = [System.IO.File]::ReadAllText($_.FullName)
    
    # 1. Fix IntDir to be absolute within BuildRoot
    $updated = [regex]::Replace(
        $content,
        '<IntDir([^>]*)>\$\(Platform\)\\\$\(Configuration\)\\\$\(ProjectName\)\\</IntDir>',
        {
            param($match)
            '<IntDir' + $match.Groups[1].Value + '>' + $buildXML + '\Intermediate\$(ProjectName)\$(Configuration)\</IntDir>'
        })

    # 2. Add OutDir for ZERO_CHECK (CMake standard boilerplate) if missing
    if ($_.Name -eq 'ZERO_CHECK.vcxproj' -and $updated -notmatch '<OutDir Condition=') {
        $outDirs =
            '    <OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|x64''">' + $buildXML + '\Output\$(ProjectName)\Debug\</OutDir>' + "`r`n" +
            '    <OutDir Condition="''$(Configuration)|$(Platform)''==''Release|x64''">' + $buildXML + '\Output\$(ProjectName)\Release\</OutDir>' + "`r`n" +
            '    <OutDir Condition="''$(Configuration)|$(Platform)''==''MinSizeRel|x64''">' + $buildXML + '\Output\$(ProjectName)\MinSizeRel\</OutDir>' + "`r`n" +
            '    <OutDir Condition="''$(Configuration)|$(Platform)''==''RelWithDebInfo|x64''">' + $buildXML + '\Output\$(ProjectName)\RelWithDebInfo\</OutDir>' + "`r`n"

        $updated = [regex]::Replace(
            $updated,
            '(<_ProjectFileVersion>[^<]+</_ProjectFileVersion>\s*)',
            { param($match) $match.Groups[1].Value + $outDirs },
            1)
    }

    if ($updated -ne $content) {
        [System.IO.File]::WriteAllText($_.FullName, $updated, $encoding)
    }
}
