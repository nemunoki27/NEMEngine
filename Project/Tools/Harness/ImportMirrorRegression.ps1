Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
. (Join-Path $engineRoot 'Tools\FileSystem\DirectorySafety.ps1')
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $engineRoot 'Tools\Import\Import-GameProject.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($name in @('Invoke-Robocopy', 'Convert-GameScriptsCsproj')) {
    $node = $ast.Find({ param($item)
        $item -is [Management.Automation.Language.FunctionDefinitionAst] -and $item.Name -eq $name
    }, $true)
    if (-not $node) { throw "検証対象がありません: $name" }
    . ([scriptblock]::Create($node.Extent.Text))
}
$workRoot = Join-Path $engineRoot ('Generated\ImportTests\' + [Guid]::NewGuid().ToString('N'))
$source = Join-Path $workRoot 'source'
$gameProjectsRoot = Join-Path $workRoot 'GameProjects'
$destination = Join-Path $gameProjectsRoot 'Probe\Probe'
foreach ($directory in @($source, $destination, (Join-Path $destination 'Saved'))) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
[IO.File]::WriteAllText((Join-Path $source 'keep.txt'), 'source')
[IO.File]::WriteAllText((Join-Path $destination 'keep.txt'), 'local')
[IO.File]::WriteAllText((Join-Path $destination 'deleted.txt'), 'obsolete')
[IO.File]::WriteAllText((Join-Path $destination 'Saved\keep.log'), 'local saved')
Invoke-Robocopy $source $destination @((Join-Path $destination 'Saved')) @()
if (Test-Path -LiteralPath (Join-Path $destination 'deleted.txt')) { throw '元で削除したファイルが残りました' }
if ([IO.File]::ReadAllText((Join-Path $destination 'keep.txt')) -cne 'source') { throw '元の内容へ置き換わっていません' }
if (-not (Test-Path -LiteralPath (Join-Path $destination 'Saved\keep.log'))) { throw 'ローカル作業データが削除されました' }
# 複製範囲が重なる要求はファイル変更前に拒否する
$rejected = $false
try { Invoke-Robocopy $destination (Join-Path $destination 'nested') @() @() }
catch { $rejected = $true }
if (-not $rejected) { throw '複製元の内側へのミラーを拒否しませんでした' }
$project = Join-Path $destination 'GameScripts.csproj'
[IO.File]::WriteAllText($project, @'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><NEMEngineSdkManaged>sdk</NEMEngineSdkManaged><CustomGameSetting>keep</CustomGameSetting></PropertyGroup><ItemGroup>
<Reference Include="NEM.ScriptCore"><HintPath>sdk\NEM.ScriptCore.dll</HintPath></Reference>
<Analyzer Include="sdk\NEM.ScriptCodeGen.dll"/><Analyzer Include="sdk\NEM.ScriptAnalyzers.dll"/>
<Reference Include="ThirdParty"><HintPath>vendor\ThirdParty.dll</HintPath></Reference>
<Analyzer Include="vendor\ThirdParty.Analyzer.dll"/><ProjectReference Include="..\Custom\Custom.csproj"/>
</ItemGroup></Project>
'@)
Convert-GameScriptsCsproj $project
Convert-GameScriptsCsproj $project
[xml]$xml = Get-Content -LiteralPath $project -Raw
if (@($xml.SelectNodes('//Reference')).Count -ne 1 -or @($xml.SelectNodes('//Analyzer')).Count -ne 1 -or
    @($xml.SelectNodes('//ProjectReference')).Count -ne 4 -or $xml.SelectSingleNode('//NEMEngineSdkManaged') -or
    $xml.SelectSingleNode('//CustomGameSetting').InnerText -cne 'keep') {
    throw '独自参照の保持かエンジン参照の置換に失敗しました'
}
Write-Output 'Import mirror and independent references passed'
