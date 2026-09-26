# 構成を評価した実ファイルとオブジェクト出力を照合する
param(
    [string]$MSBuildPath = '',
    [string[]]$Configurations = @('Debug', 'Develop', 'Release')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (-not $MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' |
        Select-Object -First 1
}
if (-not $MSBuildPath) { throw 'MSBuildが見つかりません' }
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')

# 現在の出力先に対応するlink入力だけを記録する
function Get-LinkInputs([string]$ProjectPath, [string]$IntermediatePath, [string]$TargetPath) {
    $projectDirectory = [IO.Path]::GetDirectoryName($ProjectPath)
    $intermediate = [IO.Path]::GetFullPath($IntermediatePath, $projectDirectory)
    $target = [IO.Path]::GetFullPath($TargetPath, $projectDirectory)
    $commands = @()
    if (Test-Path -LiteralPath $intermediate) {
        $logs = Get-ChildItem -LiteralPath $intermediate -Filter '*.command.1.tlog' -Recurse -File |
            Where-Object { $_.Name -in @('link.command.1.tlog', 'Lib.command.1.tlog') }
        foreach ($log in $logs) {
            foreach ($line in Get-Content -LiteralPath $log.FullName -Encoding Unicode) {
                $match = [regex]::Match($line, '/OUT:(?:"([^"\r\n]+)"|(\S+))', 'IgnoreCase')
                if (-not $match.Success) { continue }
                $output = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
                if ([IO.Path]::GetFullPath($output, $projectDirectory) -ne $target) { continue }
                $libraries = @()
                foreach ($token in [regex]::Matches($line, '"[^"\r\n]*"|[^\s"]+')) {
                    $value = $token.Value.Trim('"')
                    if ($value.StartsWith('/') -or -not $value.EndsWith('.lib', [StringComparison]::OrdinalIgnoreCase)) { continue }
                    $libraries += if ($value.Contains('\') -or $value.Contains('/')) {
                        [IO.Path]::GetFullPath($value, $projectDirectory)
                    } else { $value }
                }
                $commands += [pscustomobject]@{ log = $log.FullName; command = $line; libraries = $libraries }
            }
        }
    }
    return [pscustomobject]@{
        state = if ($commands.Count) { 'Recorded' } else { 'NotBuilt' }
        commands = $commands
    }
}
$projects = @(
    'Project/Engine/Core/NEMCore.vcxproj', 'Project/Engine/NEMRuntime.vcxproj',
    'Project/Engine/Editor/NEMEditor.vcxproj', 'Project/Sandbox/Sandbox.vcxproj',
    'Project/Tests/NEMTests.vcxproj', 'Project/Tools/NEM.BuildTool/NEMBuildTool.vcxproj'
)
$records = [Collections.Generic.List[object]]::new()
$failures = [Collections.Generic.List[string]]::new()
foreach ($configuration in $Configurations) {
    foreach ($relative in $projects) {
        $projectPath = Join-Path $repoRoot $relative
        if (-not (Test-Path -LiteralPath $projectPath)) { throw "構築対象がありません: $relative" }
        $process = Invoke-HarnessProcess $MSBuildPath @($projectPath, "-p:Configuration=$configuration", '-p:Platform=x64',
            '-getItem:ClCompile,ProjectReference', '-getProperty:IntDir,TargetPath')
        if ($process.exitCode -ne 0) { throw $process.output }
        $evaluation = $process.output | ConvertFrom-Json
        $owner = [IO.Path]::GetFileNameWithoutExtension($projectPath)
        $sources = @()
        foreach ($item in $evaluation.Items.ClCompile) {
            if ($item.PSObject.Properties['ExcludedFromBuild'] -and $item.ExcludedFromBuild -eq 'true') { continue }
            # ディレクトリ指定の出力はソース名からオブジェクト名を補う
            $objectPath = $item.ObjectFileName
            if ($objectPath.EndsWith('\') -or $objectPath.EndsWith('/')) {
                $objectPath += $item.Filename + '.obj'
            }
            $objectPath = [IO.Path]::GetFullPath($objectPath, [IO.Path]::GetDirectoryName($projectPath))
            $sources += [pscustomobject]@{ source = $item.FullPath; object = $objectPath }
        }
        foreach ($field in @('source', 'object')) {
            foreach ($duplicate in @($sources | Group-Object $field | Where-Object Count -gt 1)) {
                $failures.Add("${owner}/${configuration}: ${field}重複: $($duplicate.Name)")
            }
        }
        $records.Add([pscustomobject]@{
            owner = $owner; configuration = $configuration
            target = $evaluation.Properties.TargetPath
            references = @($evaluation.Items.ProjectReference | ForEach-Object FullPath)
            linkInputs = Get-LinkInputs $projectPath $evaluation.Properties.IntDir $evaluation.Properties.TargetPath
            sources = $sources
        })
    }
    # 同じ実行ファイルへリンクするCoreとEditorの所有を検査する
    $core = $records | Where-Object { $_.owner -eq 'NEMCore' -and $_.configuration -eq $configuration }
    $editor = $records | Where-Object { $_.owner -eq 'NEMEditor' -and $_.configuration -eq $configuration }
    foreach ($source in $editor.sources) {
        if ($source.source -in $core.sources.source) {
            $failures.Add("Core/Editorの二重所有: $configuration $($source.source)")
        }
    }
}
$report = [ordered]@{
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    failures = $failures.ToArray(); projects = $records.ToArray()
}
$reportPath = Join-Path $repoRoot 'Generated/Refactoring/BuildOwnership.json'
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($reportPath)) | Out-Null
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
$failures | ForEach-Object { Write-Host "[FAIL] $_" }
Write-Host "構築所有: $($records.Count)構成、失敗$($failures.Count)件"
if ($failures.Count -gt 0) { exit 1 }
exit 0
