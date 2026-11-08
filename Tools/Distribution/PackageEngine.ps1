param(
    [string]$Version = '0.1.0',
    [string]$ToolchainRoot = '',
    [string]$OutputDirectory = '',
    [string]$BaseUrl = '',
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $engineRepository 'Tools/ProductBuild/BuildFileOperations.ps1')
. (Join-Path $engineRepository 'Tools/FileSystem/DirectorySafety.ps1')
if ($Version -notmatch '^\d+\.\d+\.\d+$' -or ([version]$Version).ToString(3) -ne $Version) { throw '版番号は0.1.0の形式で指定してください' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $engineRepository 'Generated/Distribution' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (!$ToolchainRoot) { $ToolchainRoot = Split-Path -Parent (Get-Command dotnet).Source }
$ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot)
$dotnet = Join-Path $ToolchainRoot 'dotnet.exe'
$sdkVersion = (& $dotnet --version).Trim()
if ($LASTEXITCODE -ne 0 -or $sdkVersion -notmatch '^10\.0\.\d+$') { throw '.NET 10 SDKを指定してください' }
if ($BaseUrl -and ([uri]$BaseUrl).Scheme -ne 'https') { throw '配布URLはHTTPSで指定してください' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$stage = Join-Path $OutputDirectory ('.package-' + [guid]::NewGuid().ToString('N'))
$releaseDirectory = Join-Path $OutputDirectory $Version
if (Test-Path -LiteralPath $releaseDirectory) { throw "作成済みの配布版が存在します: $releaseDirectory" }
$archiveRoot = Join-Path $stage 'Artifacts'
New-Item -ItemType Directory -Path $archiveRoot -Force | Out-Null

function Copy-PackageDirectory([string]$source, [string]$target) {
    Assert-DirectoryTreeWithoutLinks $source
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    robocopy $source $target /E /XJ /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "コピーに失敗しました: $source" }
}
function Copy-RequiredFile([string]$source, [string]$target) {
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "配布ファイルがありません: $source" }
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}
function New-PackageInfo([string]$directory, [string]$id, [string]$key, [bool]$contentAddressed = $false) {
    $zip = Join-Path $archiveRoot ($id + '.zip')
    $temporary = $zip + '.writing'
    if (Test-Path -LiteralPath $zip) { throw "同じ版のアーカイブが既にあります: $zip" }
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::Open($temporary, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($file in Get-ChildItem -LiteralPath $directory -File -Recurse | Sort-Object FullName) {
                $relative = $file.FullName.Substring($directory.Length + 1).Replace('\', '/')
                $entry = $archive.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
                $inputStream = [IO.File]::OpenRead($file.FullName)
                $outputStream = $entry.Open()
                try { $inputStream.CopyTo($outputStream) }
                finally { $outputStream.Dispose(); $inputStream.Dispose() }
            }
        } finally { $archive.Dispose() }
        if ($contentAddressed) {
            $suffix = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.Substring(0, 12).ToLowerInvariant()
            $id += '-' + $suffix
            $zip = Join-Path $archiveRoot ($id + '.zip')
            $key = 'toolchains/' + $id + '.zip'
        }
        Move-Item -LiteralPath $temporary -Destination $zip
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
    $bytes = (Get-ChildItem -LiteralPath $directory -File -Recurse | Measure-Object Length -Sum).Sum
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($key.StartsWith('releases/')) { $key = "releases/$Version/$hash/engine.zip" }
    return [ordered]@{ id = $id; url = $BaseUrl.TrimEnd('/') + '/' + $key; sha256 = $hash; bytes = (Get-Item -LiteralPath $zip).Length; unpackedBytes = [long]$bytes }
}

try {
    if (!$SkipBuild) {
        # ソースを増減していない配布作業では再生成しない
        $msbuild = Get-MSBuildPath
        foreach ($entry in @(@('Develop', 'NEMEditor'), @('Release', 'NEMPlayer'), @('Release', 'NEMBuildTool'))) {
            $buildArguments = @(Get-NativeBuildArguments $entry[0] $entry[1])
            & $msbuild (Join-Path $engineRepository 'Project/NEMEngine.slnx') @buildArguments
            if ($LASTEXITCODE -ne 0) { throw "ネイティブビルドに失敗しました: $entry" }
        }
        foreach ($name in @('NEM.ScriptCore', 'NEM.ScriptCodeGen', 'NEM.ScriptAnalyzers', 'NEM.ScriptMetaSync')) {
            & $dotnet build (Join-Path $engineRepository "Project/Engine/Managed/$name/$name.csproj") -c Release -m:1 -nodeReuse:false
            if ($LASTEXITCODE -ne 0) { throw "C#ツールのビルドに失敗しました: $name" }
        }
    }
    $packageRoot = Join-Path $stage 'Engine'
    Copy-PackageDirectory (Join-Path $engineRepository 'Project/Engine/Assets') (Join-Path $packageRoot 'Engine/Assets')
    $templates = Join-Path $engineRepository 'Templates/ManagedProject'
    foreach ($kind in @('2D', '3D')) {
        $metadata = Get-Content -LiteralPath (Join-Path $templates "$kind/template.json") -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($metadata.schemaVersion -ne 1 -or $metadata.kind -ne $kind -or $metadata.id -ne $kind -or
            $metadata.startupScene -notmatch '^[a-fA-F0-9]{32}$') { throw "テンプレートの保存情報が不正です: $kind" }
        $matched = @(Get-ChildItem -LiteralPath (Join-Path $templates "$kind/GameAssets") -File -Recurse -Filter '*.scene.json.meta' |
            Where-Object { (Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).guid -eq $metadata.startupScene })
        if ($matched.Count -ne 1 -or !(Test-Path -LiteralPath $matched[0].FullName.Substring(0, $matched[0].FullName.Length - 5))) {
            throw "テンプレートの開始シーンが見つかりません: $kind"
        }
    }
    Copy-PackageDirectory $templates (Join-Path $packageRoot 'ProjectTemplates')
    Copy-PackageDirectory (Join-Path $engineRepository 'Tools/ProductBuild') (Join-Path $packageRoot 'Tools/ProductBuild')
    Copy-RequiredFile (Join-Path $engineRepository 'Tools/BuildGame.ps1') (Join-Path $packageRoot 'Tools/BuildGame.ps1')
    Copy-RequiredFile (Join-Path $PSScriptRoot 'NEM.GameScripts.targets') (Join-Path $packageRoot 'Managed/Build/NEM.GameScripts.targets')
    foreach ($entry in @(@('Develop', 'NEMEditor', 'Editor/Develop'), @('Release', 'NEMPlayer', 'Runtime/Release'), @('Release', 'NEMBuildTool', 'Tools/NEMBuildTool'))) {
        $source = Join-Path $engineRepository "Generated/Output/$($entry[0])/$($entry[1])"
        $target = Join-Path $packageRoot $entry[2]
        Copy-RequiredFile (Join-Path $source "$($entry[1]).exe") (Join-Path $target "$($entry[1]).exe")
        $runtimeArguments = @{ TargetDirectory = $target; Configuration = $entry[0] }
        if ($entry[1] -eq 'NEMPlayer') { $runtimeArguments.RuntimeDllPath = Join-Path $engineRepository 'Generated/Bin/Release/NEMRuntime/NEMRuntime.dll' }
        & (Join-Path $engineRepository 'Tools/DeployRuntimeDependencies.ps1') @runtimeArguments
        if ($entry[1] -ne 'NEMBuildTool') {
            Copy-PackageDirectory (Join-Path $engineRepository "Generated/Managed/NEM.ScriptCore/$($entry[0])") (Join-Path $target 'Managed')
        }
    }
    $managedSource = Join-Path $engineRepository 'Generated/Managed/NEM.ScriptCore/Develop'
    foreach ($name in @('NEM.ScriptCore.dll', 'NEM.ScriptCore.pdb')) { Copy-RequiredFile (Join-Path $managedSource $name) (Join-Path $packageRoot "Managed/Ref/$name") }
    foreach ($name in @('NEM.ScriptCodeGen', 'NEM.ScriptAnalyzers')) {
        Copy-RequiredFile (Join-Path $engineRepository "Project/Engine/Managed/$name/bin/Release/netstandard2.0/$name.dll") (Join-Path $packageRoot "Managed/Analyzers/$name.dll")
    }
    Copy-PackageDirectory (Join-Path $engineRepository 'Project/Engine/Managed/NEM.ScriptMetaSync/bin/Release/net10.0') (Join-Path $packageRoot 'Managed/Tools')
    & $dotnet publish (Join-Path $engineRepository 'Project/Tools/NEM.ProjectTool/NEM.ProjectTool.csproj') -c Release --no-self-contained -o (Join-Path $packageRoot 'Tools/ProjectTool') -m:1 -nodeReuse:false
    if ($LASTEXITCODE -ne 0) { throw 'プロジェクトツールの作成に失敗しました' }
    # 依存ライブラリのライセンスを配布へ含める
    $dependencies = Get-Content -LiteralPath (Join-Path $engineRepository 'Project/Externals/dependencies.lock.json') -Raw | ConvertFrom-Json
    foreach ($library in $dependencies.libraries) {
        foreach ($license in $library.licenseFiles) {
            $source = Join-Path $engineRepository "Project/Externals/$($library.name)/$license"
            if (Test-Path -LiteralPath $source) { Copy-RequiredFile $source (Join-Path $packageRoot "Licenses/$($library.name)/$license") }
        }
    }
    # Windows PowerShellが日本語と引用符を正しく読み込める形式へ揃える
    foreach ($script in Get-ChildItem -LiteralPath $packageRoot -File -Recurse -Filter '*.ps1') {
        $contents = [IO.File]::ReadAllText($script.FullName, [Text.UTF8Encoding]::new($false, $true))
        [IO.File]::WriteAllText($script.FullName, $contents, [Text.UTF8Encoding]::new($true))
    }
    $files = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($packageRoot.Length + 1).Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    Write-Utf8Json (Join-Path $packageRoot 'engine.json') ([ordered]@{
        schemaVersion = 1; version = $Version; platform = 'win-x64'; buildID = [guid]::NewGuid().ToString('N');
        toolchainVersion = $sdkVersion; editor = 'Editor/Develop/NEMEditor.exe'; projectTool = 'Tools/ProjectTool/NEM.ProjectTool.dll'; files = $files
    })
    $engineInfo = New-PackageInfo $packageRoot "nemengine-$Version-win-x64" "releases/$Version/engine.zip"
    $sdkStage = Join-Path $stage 'Toolchain'
    # 使用版以外のSDKとランタイムを配布へ混ぜない
    New-Item -ItemType Directory -Path $sdkStage -Force | Out-Null
    foreach ($relative in @('dotnet.exe', 'LICENSE.txt', 'ThirdPartyNotices.txt')) { Copy-RequiredFile (Join-Path $ToolchainRoot $relative) (Join-Path $sdkStage $relative) }
    Copy-PackageDirectory (Join-Path $ToolchainRoot "sdk/$sdkVersion") (Join-Path $sdkStage "sdk/$sdkVersion")
    foreach ($relative in @('host/fxr', 'shared/Microsoft.NETCore.App', 'shared/Microsoft.AspNetCore.App', 'shared/Microsoft.WindowsDesktop.App', 'packs', 'sdk-manifests')) {
        $source = Join-Path $ToolchainRoot $relative
        if (!(Test-Path -LiteralPath $source)) { continue }
        if ($relative -eq 'sdk-manifests') {
            foreach ($folder in Get-ChildItem -LiteralPath $source -Directory | Where-Object Name -like '10.*') {
                Copy-PackageDirectory $folder.FullName (Join-Path $sdkStage "$relative/$($folder.Name)")
            }
            continue
        }
        if ($relative -eq 'packs') {
            foreach ($name in @('Microsoft.NETCore.App.Ref', 'Microsoft.NETCore.App.Host.win-x64', 'Microsoft.WindowsDesktop.App.Ref', 'Microsoft.AspNetCore.App.Ref', 'NETStandard.Library.Ref')) {
                $pack = Join-Path $source $name
                if (!(Test-Path -LiteralPath $pack)) { continue }
                $packVersion = Get-ChildItem -LiteralPath $pack -Directory | Where-Object { $_.Name -like '10.*' -or $name -eq 'NETStandard.Library.Ref' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
                if ($packVersion) { Copy-PackageDirectory $packVersion.FullName (Join-Path $sdkStage "$relative/$name/$($packVersion.Name)") }
            }
            continue
        }
        $selected = Get-ChildItem -LiteralPath $source -Directory | Where-Object Name -match '^10\.0\.\d+$' | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
        if ($selected) { Copy-PackageDirectory $selected.FullName (Join-Path $sdkStage "$relative/$($selected.Name)") }
    }
    $toolchainInfo = New-PackageInfo $sdkStage "dotnet-$sdkVersion-win-x64" "toolchains/dotnet-$sdkVersion-win-x64.zip" $true
    Write-Utf8Json (Join-Path $archiveRoot "release-$Version.json") ([ordered]@{
        version = $Version; notes = 'エンジンデフォルト'; minimumLauncherVersion = '0.1.11'; toolchainVersion = $sdkVersion; engine = $engineInfo; toolchain = $toolchainInfo
    })
    # アーカイブと検証情報を一組で公開する
    Move-Item -LiteralPath $archiveRoot -Destination $releaseDirectory
    Write-Host "配布物を作成しました: $releaseDirectory"
} finally {
    $checked = [IO.Path]::GetFullPath($stage)
    if ([IO.Path]::GetDirectoryName($checked) -ne $OutputDirectory -or ![IO.Path]::GetFileName($checked).StartsWith('.package-')) { throw '作業先の削除範囲が不正です' }
    if (Test-Path -LiteralPath $checked) { Remove-Item -LiteralPath $checked -Recurse -Force }
}
exit 0
