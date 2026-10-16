# ヘッダー変更後の部分コンパイルと通常ビルドを照合する
param([string]$MSBuildPath = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')
if (-not $MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' |
        Select-Object -First 1
}
if (-not $MSBuildPath) { throw 'MSBuildが見つかりません' }

# 生成済みCoreと同じコンパイラー設定を使う
$sourceProject = Join-Path $repoRoot 'Project/Engine/Core/NEMCore.vcxproj'
[xml]$document = [IO.File]::ReadAllText($sourceProject)
$ns = [Xml.XmlNamespaceManager]::new($document.NameTable)
$ns.AddNamespace('msb', $document.DocumentElement.NamespaceURI)
$configuration = $document.SelectSingleNode('//msb:PropertyGroup[msb:UseMultiToolTask]', $ns)
if ($null -eq $configuration -or $configuration.UseMultiToolTask -ne 'true') {
    throw 'ソース単位のコンパイル設定がありません。Premakeを再生成してください'
}
$output = Join-Path $repoRoot ('Generated/Refactoring/NativeIncrementalBuild/' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($output) | Out-Null
$projectPath = Join-Path $output 'NativeIncrementalBuild.vcxproj'
$utf8 = [Text.UTF8Encoding]::new($false)

# 実行結果で翻訳単位間のクラスサイズを比較する
[IO.File]::WriteAllText((Join-Path $output 'Layout.h'), "#pragma once`nstruct Layout { int value; };`n", $utf8)
[IO.File]::WriteAllText((Join-Path $output 'Producer.cpp'), "#include `"Layout.h`"`nint GetLayoutSize() { return sizeof(Layout); }`n", $utf8)
[IO.File]::WriteAllText((Join-Path $output 'Consumer.cpp'), "#include `"Layout.h`"`nint GetLayoutSize();`nint main() { return GetLayoutSize() == sizeof(Layout) ? 0 : 1; }`n", $utf8)
foreach ($node in @($document.SelectNodes('//msb:ItemGroup[msb:ClCompile or msb:ClInclude or msb:ProjectReference or msb:None]', $ns))) {
    [void]$node.ParentNode.RemoveChild($node)
}
foreach ($node in $document.SelectNodes('//msb:ConfigurationType', $ns)) { $node.InnerText = 'Application' }
foreach ($name in @('OutDir', 'IntDir', 'TargetName', 'TargetExt', 'ExternalIncludePath')) {
    foreach ($node in $document.SelectNodes("//msb:$name", $ns)) {
        $node.InnerText = switch ($name) {
            'OutDir' { '$(ProjectDir)Output\' }
            'IntDir' { '$(ProjectDir)Intermediate\' }
            'TargetName' { 'NativeIncrementalBuild' }
            'TargetExt' { '.exe' }
            'ExternalIncludePath' { '$(ExternalIncludePath)' }
        }
    }
}
foreach ($node in $document.SelectNodes('//msb:SubSystem', $ns)) { $node.InnerText = 'Console' }
$group = $document.CreateElement('ItemGroup', $document.DocumentElement.NamespaceURI)
foreach ($source in @('Producer.cpp', 'Consumer.cpp')) {
    $item = $document.CreateElement('ClCompile', $document.DocumentElement.NamespaceURI)
    $item.SetAttribute('Include', $source)
    [void]$group.AppendChild($item)
}
[void]$document.DocumentElement.AppendChild($group)
$document.Save($projectPath)

function Invoke-ProbeBuild([string]$Name, [string[]]$ExtraArguments = @()) {
    $result = Invoke-HarnessProcess $MSBuildPath (@($projectPath, '/nologo', '/v:minimal', '/m:1',
        '/p:Configuration=Develop', '/p:Platform=x64', '/p:CL_MPCount=1', '/p:PreferredToolArchitecture=x64') + $ExtraArguments)
    [IO.File]::WriteAllText((Join-Path $output "$Name.log"), $result.output, $utf8)
    if ($result.exitCode -ne 0) { throw "$Name に失敗しました: $($result.output)" }
}

Invoke-ProbeBuild 'Initial'
$executable = Join-Path $output 'Output/NativeIncrementalBuild.exe'
$producerObject = Join-Path $output 'Intermediate/Producer.obj'
$consumerObject = Join-Path $output 'Intermediate/Consumer.obj'
$initialTime = [IO.File]::GetLastWriteTimeUtc($producerObject)
if ((Invoke-HarnessProcess $executable @()).exitCode -ne 0) { throw '初回のクラスサイズが一致しません' }

# 片方だけ先にコンパイルしても、通常ビルドで残りを更新する
[IO.File]::WriteAllText((Join-Path $output 'Layout.h'), "#pragma once`nstruct Layout { int prefix; int value; };`n", $utf8)
Invoke-ProbeBuild 'Partial' @('/t:ClCompile', '/p:SelectedFiles=Consumer.cpp')
Invoke-ProbeBuild 'Incremental'
if ([IO.File]::GetLastWriteTimeUtc($producerObject) -le $initialTime) { throw '依存ヘッダーを変更したソースが再コンパイルされていません' }
if ((Invoke-HarnessProcess $executable @()).exitCode -ne 0) { throw '部分コンパイル後のクラスサイズが一致しません' }

# 変更がなければ両方のオブジェクトを再利用する
$producerTime = [IO.File]::GetLastWriteTimeUtc($producerObject)
$consumerTime = [IO.File]::GetLastWriteTimeUtc($consumerObject)
Invoke-ProbeBuild 'Unchanged'
if ([IO.File]::GetLastWriteTimeUtc($producerObject) -ne $producerTime -or
    [IO.File]::GetLastWriteTimeUtc($consumerObject) -ne $consumerTime) { throw '変更なしのビルドで再コンパイルされました' }
Write-Host "[OK] ヘッダー変更・部分コンパイル・再リンク・変更なしの再利用: $output"
