param(
    [Parameter(Mandatory = $true)][string]$CatalogUrl,
    [string]$ManualUrl = '',
    [string]$InnoCompiler = '',
    [string]$OutputDirectory = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$launcherVersion = ([xml](Get-Content -LiteralPath (Join-Path $PSScriptRoot 'LauncherVersion.props') -Raw)).Project.PropertyGroup.Version
if (([uri]$CatalogUrl).Scheme -ne 'https') { throw '配布一覧のHTTPS URLを指定してください' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $engineRepository 'Generated/Distribution/Launcher' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$publish = Join-Path $OutputDirectory 'App'
New-Item -ItemType Directory -Path $publish -Force | Out-Null
& dotnet publish (Join-Path $engineRepository 'Project/Tools/NEM.Launcher/NEM.Launcher.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -p:DebugType=embedded -o $publish -m:1 -nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw 'ランチャーの発行に失敗しました' }
. (Join-Path $engineRepository 'Tools/ProductBuild/BuildFileOperations.ps1')
New-Item -ItemType Directory -Path (Join-Path $publish 'Config') -Force | Out-Null
Write-Utf8Json (Join-Path $publish 'Config/launcher.json') @{ catalogUrl = $CatalogUrl; manualUrl = $ManualUrl }
# 発行物の混在をSetupへ持ち込まない
$unexpected = @(Get-ChildItem -LiteralPath $publish -File | Where-Object Name -ne 'NEMLauncher.exe')
if ($unexpected.Count) { throw '発行先に旧ファイルがあります 新しいOutputDirectoryを指定してください' }
if (!$InnoCompiler) {
    foreach ($candidate in @("${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe", "$env:ProgramFiles/Inno Setup 6/ISCC.exe")) {
        if (Test-Path -LiteralPath $candidate) { $InnoCompiler = $candidate; break }
    }
}
if (!(Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) { throw 'Inno Setup 6のISCC.exeを-InnoCompilerで指定してください' }
& $InnoCompiler "/DLauncherVersion=$launcherVersion" "/DPublishDirectory=$publish" "/DSetupDirectory=$OutputDirectory" (Join-Path $PSScriptRoot 'NEMLauncher.iss')
if ($LASTEXITCODE -ne 0) { throw 'Setup.exeの作成に失敗しました' }
Write-Host "作成しました: $OutputDirectory/NEMLauncherSetup.exe"
