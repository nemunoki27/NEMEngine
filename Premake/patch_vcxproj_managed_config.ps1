param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,

    [Parameter(Mandatory = $true)]
    [string]$ScriptCoreProjectPath,

    [Parameter(Mandatory = $true)]
    [string]$ScriptCoreManagedOutputPath,

    # ProjectDirからリポジトリルートまでの相対プレフィックス(末尾の\は不要、区切りはこのスクリプトが付与する)。
    # Sandbox(Project/Sandbox)は "..\.."、取り込みゲーム(Project/GameProjects/<name>/<name>)は "..\..\..\.."。
    # 末尾に\を付けてバッチから "..\..\..\..\" のように渡すと、cmd→powershellの引数解析で末尾 \" が
    # エスケープ扱いされ値が壊れる(末尾に引用符が混入する)ため、呼び出し側は末尾\を付けないこと。
    [string]$RepoRootFromProject = "..\.."
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ProjectPath)) {
    throw "Visual C++ project was not found: $ProjectPath"
}

if (-not (Test-Path -LiteralPath $ScriptCoreProjectPath)) {
    throw "NEM.ScriptCore project was not found: $ScriptCoreProjectPath"
}

function Convert-ToCommandPath {
    param([string]$Path)
    return [System.IO.Path]::GetFullPath($Path).Replace("\", "/")
}

function Get-OrCreateChildElement {
    param(
        [xml]$Document,
        [System.Xml.XmlElement]$Parent,
        [string]$Name
    )

    foreach ($child in $Parent.ChildNodes) {
        if ($child.LocalName -eq $Name) {
            return $child
        }
    }

    $created = $Document.CreateElement($Name, $Parent.NamespaceURI)
    [void]$Parent.AppendChild($created)
    return $created
}

function Set-EventCommand {
    param(
        [xml]$Document,
        [System.Xml.XmlElement]$ItemDefinitionGroup,
        [string]$EventName,
        [string]$Command
    )

    $event = Get-OrCreateChildElement -Document $Document -Parent $ItemDefinitionGroup -Name $EventName
    $commandNode = Get-OrCreateChildElement -Document $Document -Parent $event -Name "Command"
    $commandNode.InnerText = $Command
}

$resolvedProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$scriptCoreProject = Convert-ToCommandPath $ScriptCoreProjectPath
$scriptCoreOutput = Convert-ToCommandPath $ScriptCoreManagedOutputPath

# NEM.ScriptCodeGen（Roslyn source generator）は NEM.ScriptCore と同じ Managed フォルダ配下にある。
# GameScripts は --no-dependencies でビルドするため、Analyzer 参照先の DLL を先に同一構成でビルドしておく。
$scriptCoreDirectory = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($ScriptCoreProjectPath))
$managedDirectory = [System.IO.Path]::GetDirectoryName($scriptCoreDirectory)
$scriptCodeGenProject = Convert-ToCommandPath (Join-Path $managedDirectory "NEM.ScriptCodeGen\NEM.ScriptCodeGen.csproj")

if (-not (Test-Path -LiteralPath $scriptCodeGenProject)) {
    throw "NEM.ScriptCodeGen project was not found: $scriptCodeGenProject"
}

# NEM.ScriptAnalyzers（ScriptBehaviour constructor analyzer）も同じ Managed 配下にある。
# GameScripts は --no-dependencies でビルドするため、Analyzer 参照先の DLL を先に同一構成でビルドしておく
# （ScriptCodeGen と同様。これが無いと Develop など未ビルド構成で CS0006 になる）。
$scriptAnalyzersProject = Convert-ToCommandPath (Join-Path $managedDirectory "NEM.ScriptAnalyzers\NEM.ScriptAnalyzers.csproj")

if (-not (Test-Path -LiteralPath $scriptAnalyzersProject)) {
    throw "NEM.ScriptAnalyzers project was not found: $scriptAnalyzersProject"
}

# NEM.ScriptMetaSync（Editor script metadata 同期ツール）も同じ Managed 配下にある。
# GameScripts ビルド前に .cs.meta の Stable ID を採番・維持する（Stable ID の正は sidecar metadata）。
$scriptMetaSyncProject = Convert-ToCommandPath (Join-Path $managedDirectory "NEM.ScriptMetaSync\NEM.ScriptMetaSync.csproj")

if (-not (Test-Path -LiteralPath $scriptMetaSyncProject)) {
    throw "NEM.ScriptMetaSync project was not found: $scriptMetaSyncProject"
}

# 同期ツールの出力 DLL（AppendTargetFrameworkToOutputPath=true なので <Config>/net10.0/）
$scriptMetaSyncDll = Convert-ToCommandPath (Join-Path $managedDirectory 'NEM.ScriptMetaSync\bin\$(Configuration)\net10.0\NEM.ScriptMetaSync.dll')

$preBuildCommand = @(
    'set DOTNET_CLI_UI_LANGUAGE=en',
    ('dotnet build "' + $scriptCoreProject + '" -c "$(Configuration)"'),
    ('dotnet build "' + $scriptCodeGenProject + '" -c "$(Configuration)"'),
    ('dotnet build "' + $scriptAnalyzersProject + '" -c "$(Configuration)"'),
    ('dotnet build "' + $scriptMetaSyncProject + '" -c "$(Configuration)"'),
    # script metadata 同期（CI は NEMScriptMetadataMode=ValidateOnly で自動採番せず error）
    'if "%NEMScriptMetadataMode%"=="" set NEMScriptMetadataMode=EditorSync',
    ('if exist "$(ProjectDir)GameAssets" dotnet "' + $scriptMetaSyncDll + '" --root "$(ProjectDir)GameAssets" --mode "%NEMScriptMetadataMode%"'),
    'if errorlevel 1 exit /b 1',
    'if exist "$(ProjectDir)Scripts\GameScripts.csproj" dotnet build "$(ProjectDir)Scripts\GameScripts.csproj" -c "$(Configuration)" --no-dependencies -p:NEMScriptMetadataMode=%NEMScriptMetadataMode%'
) -join "`r`n"

# Generated/ と Project/Externals/ はリポジトリルート基準で参照する。プロジェクトの階層深さ
# (Sandbox=Project/Sandbox, ゲーム=Project/GameProjects/<name>)で $(ProjectDir) からの距離が変わるため、
# $RepoRootFromProject でリポジトリルートまで遡ってから絶対的な配置に解決する。
# 末尾の\有無に依存しないよう正規化し、区切りはここで明示的に付与する
$repoRoot = '$(ProjectDir)' + $RepoRootFromProject.TrimEnd('\')
$generatedBinDll = $repoRoot + '\Generated\Bin\$(Configuration)\NEMRuntime\NEMRuntime.dll'
$winPixDll = $repoRoot + '\Project\Externals\WinPixEventRuntime\bin\x64\WinPixEventRuntime.dll'
$nethostDll = $repoRoot + '\Project\Externals\dotnet-hosting\bin\x64\nethost.dll'

$postBuildCommand = @(
    # NEMRuntime.dll を実行ファイル横へ配置する（in-repo は Generated/Bin から、利用側はimport lib参照のみ）
    ('if exist "' + $generatedBinDll + '" copy /Y "' + $generatedBinDll + '" "$(TargetDir)NEMRuntime.dll"'),
    'copy /Y "$(WindowsSdkDir)bin\$(TargetPlatformVersion)\x64\dxcompiler.dll" "$(TargetDir)dxcompiler.dll"',
    'copy /Y "$(WindowsSdkDir)bin\$(TargetPlatformVersion)\x64\dxil.dll" "$(TargetDir)dxil.dll"',
    ('if exist "' + $scriptCoreOutput + '\$(Configuration)\*" xcopy /Y /I "' + $scriptCoreOutput + '\$(Configuration)\*" "$(TargetDir)Managed\"'),
    'if exist "$(ProjectDir)Managed\$(Configuration)\*" xcopy /Y /I "$(ProjectDir)Managed\$(Configuration)\*" "$(TargetDir)Managed\"',
    # WinPixEventRuntime.dll は USE_PIX が有効な Debug のみ実行ファイル横へ配置する
    ('if "$(Configuration)"=="Debug" copy /Y "' + $winPixDll + '" "$(TargetDir)WinPixEventRuntime.dll"'),
    # nethost.dll を実行ファイル横へ配置する。DotnetHostResolver が動的ロードして get_hostfxr_path を取得する（全構成）
    ('copy /Y "' + $nethostDll + '" "$(TargetDir)nethost.dll"')
) -join "`r`n"

$document = New-Object xml
$document.PreserveWhitespace = $true
$document.Load($resolvedProjectPath)

$namespaceManager = New-Object System.Xml.XmlNamespaceManager($document.NameTable)
$namespaceManager.AddNamespace("msb", $document.DocumentElement.NamespaceURI)

$itemDefinitionGroups = $document.SelectNodes("//msb:ItemDefinitionGroup[msb:PreBuildEvent or msb:PostBuildEvent]", $namespaceManager)
if ($itemDefinitionGroups.Count -eq 0) {
    throw "No ItemDefinitionGroup with build events was found in: $resolvedProjectPath"
}

foreach ($group in $itemDefinitionGroups) {
    Set-EventCommand -Document $document -ItemDefinitionGroup $group -EventName "PreBuildEvent" -Command $preBuildCommand
    Set-EventCommand -Document $document -ItemDefinitionGroup $group -EventName "PostBuildEvent" -Command $postBuildCommand
}

$settings = [System.Xml.XmlWriterSettings]::new()
$settings.Indent = $true
$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writer = [System.Xml.XmlWriter]::Create($resolvedProjectPath, $settings)
$document.Save($writer)
$writer.Close()

$patchedText = Get-Content -LiteralPath $resolvedProjectPath -Raw
if ($patchedText -notmatch '-c "\$\(Configuration\)"') {
    throw 'Managed build command verification failed: missing -c "$(Configuration)".'
}

if ($patchedText -notmatch 'Managed\\\$\(Configuration\)\\\*') {
    throw 'Managed copy command verification failed: missing Managed\$(Configuration)\*.'
}

if ($patchedText -match '-c "Debug"|Managed\\Debug\\\*|Release"\) else') {
    throw "Managed config verification failed: stale Debug/Release fallback command remains."
}

if ($patchedText -match 'dotnet build "\s+[^"]+\s+" -c|if exist "\s+[^"]+\s+\\\$\(Configuration\)') {
    throw "Managed config verification failed: command path contains unexpected line breaks."
}

Write-Host "[OK] Patched managed build/copy configuration: $resolvedProjectPath"
