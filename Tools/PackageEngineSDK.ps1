# NEMEngine prebuilt SDK packaging
# エンジンをDLLとしてビルドし、ゲーム側がgit submoduleで参照する配布物だけを SDK フォルダへ集約する
# 配布物: NEMEngine.dll / import lib / 公開ヘッダ / ランタイムDLL / managed / ゲーム生成用premake
# エンジンのソースcoreは一切含めない（GameProjectからソースを見えなくするため）
# Debug / Develop / Release の全構成をまとめて書き出す

param(
    [string[]]$Configurations = @("Debug", "Develop", "Release"),
    [string]$OutDir = "",
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}

$engineRoot = Split-Path -Parent $PSScriptRoot
$generated  = Join-Path $engineRoot "Generated"
if ([string]::IsNullOrEmpty($OutDir)) {
    $OutDir = Join-Path $engineRoot "Generated\SDK"
}

$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild が見つかりません（vswhere）。Visual Studio をインストールしてください。" }

# 1) プロジェクト生成 + 構成ごとにエンジン(+依存)ビルド
if (-not $SkipBuild) {
    Write-Host "[1/4] Visual Studio プロジェクトを生成中..."
    & (Join-Path $engineRoot "Premake\generate_vs2026.bat") | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "generate_vs2026.bat に失敗しました。" }
    foreach ($cfg in $Configurations) {
        Write-Host "[2/4] エンジンをビルド中（$cfg）... 数分かかる場合があります"
        # 外部ライブラリの構成対応(configmapでDevelop->Release等)はソリューション側にあるため、.slnx経由でビルドする。
        # ただしターゲットはSandboxのみに限定する。理由:
        #   - SDKにゲームプロジェクト(Project/GameProjects/*)は不要（SDKはエンジンDLL+公開ヘッダ+管理ツールチェーンのみ）。
        #   - 取り込んだゲームappとSandboxは同じC#管理ツールチェーン(NEM.ScriptCore/CodeGen/Analyzers/MetaSync)を
        #     プリビルドで同一出力先へビルドするため、-mの並列ビルドで同時実行されるとファイルロック競合で失敗する。
        # Sandboxを介してエンジン(NEMEngine)・外部ライブラリ・管理ツールチェーンまで一式ビルドされ、
        # GameProjectsはSandboxの依存に含まれないため巻き込まれない。
        & $msbuild (Join-Path $engineRoot "Project\NEMEngine.slnx") -t:Sandbox -p:Configuration=$cfg -p:Platform=x64 -m -v:m -nologo
        if ($LASTEXITCODE -ne 0) { throw "エンジンビルドに失敗しました（$cfg）。" }
    }
}

# 2) 構成に依存しない共有部分を一度だけ書き出す
Write-Host "[3/4] 共有ファイル（公開ヘッダ / アセット / C#ツールチェーン / premake）を書き出し中..."
$publicSrc = Join-Path $engineRoot "Project\Engine\Public"
$sdkInclude = Join-Path $OutDir "Include"
New-Item -ItemType Directory -Force -Path $sdkInclude | Out-Null
Copy-Item -Force (Join-Path $publicSrc "*.h") $sdkInclude

# エンジン同梱アセット（実行時に External/NEMEngine/Engine/Assets をエンジンルートとして認識させる）
$engineAssetsSrc = Join-Path $engineRoot "Project\Engine\Assets"
if (Test-Path -LiteralPath $engineAssetsSrc) {
    $sdkAssets = Join-Path $OutDir "Engine\Assets"
    New-Item -ItemType Directory -Force -Path $sdkAssets | Out-Null
    robocopy $engineAssetsSrc $sdkAssets /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
}
$engineLibrarySrc = Join-Path $engineRoot "Project\Engine\Library"
if (Test-Path -LiteralPath $engineLibrarySrc) {
    $sdkLibrary = Join-Path $OutDir "Engine\Library"
    New-Item -ItemType Directory -Force -Path $sdkLibrary | Out-Null
    robocopy $engineLibrarySrc $sdkLibrary /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
}

# ゲーム生成に必要なpremakeヘルパ/exe/パッチ（エンジンソースのpremakeは含めない）
$sdkPremake = Join-Path $OutDir "Premake"
New-Item -ItemType Directory -Force -Path $sdkPremake | Out-Null
foreach ($f in @("premake5.exe","nem_game.lua","patch_script_slnx.ps1","patch_vcxproj_user_debugger.ps1")) {
    $src = Join-Path $engineRoot "Premake\$f"
    if (Test-Path -LiteralPath $src) { Copy-Item -Force $src (Join-Path $sdkPremake $f) }
}

# 既存ゲーム側の Premake / 更新ツールを SDK 更新時に同期できるよう、ゲームプロジェクト用サポートファイルも同梱する
$gameTemplate = Join-Path $engineRoot "Templates\GameProject"
$sdkGameProject = Join-Path $OutDir "GameProject"
$sdkGamePremake = Join-Path $sdkGameProject "Premake"
New-Item -ItemType Directory -Force -Path $sdkGamePremake | Out-Null
foreach ($f in @("premake5.lua","generate_vs2026.bat")) {
    $src = Join-Path $gameTemplate "Premake\$f"
    if (Test-Path -LiteralPath $src) { Copy-Item -Force $src (Join-Path $sdkGamePremake $f) }
}
foreach ($f in @(".gitignore",".gitattributes","SDK更新.bat","UpdateSdk.ps1","RepairGameProject.ps1","ゲームプロジェクト修復.bat")) {
    $src = Join-Path $gameTemplate $f
    if (Test-Path -LiteralPath $src) { Copy-Item -Force $src (Join-Path $sdkGameProject $f) }
}

# C#ゲームビルド用ツールチェーン（参照DLL / Roslynアナライザ / メタ同期ツール）
# 参照はネイティブ構成に依存しないので代表構成（Debug優先）から書き出す
$refConfig = if ($Configurations -contains "Debug") { "Debug" } else { $Configurations[0] }
$mngRoot   = Join-Path $engineRoot "Project\Engine\Managed"
$refManagedSrc = Join-Path $generated "Managed\NEM.ScriptCore\$refConfig"
$sdkMngRef       = Join-Path $OutDir "Managed\Ref"
$sdkMngAnalyzers = Join-Path $OutDir "Managed\Analyzers"
$sdkMngTools     = Join-Path $OutDir "Managed\Tools"
foreach ($d in @($sdkMngRef, $sdkMngAnalyzers, $sdkMngTools)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
if (Test-Path (Join-Path $refManagedSrc "NEM.ScriptCore.dll")) {
    Copy-Item -Force (Join-Path $refManagedSrc "NEM.ScriptCore.dll") $sdkMngRef
    if (Test-Path (Join-Path $refManagedSrc "NEM.ScriptCore.pdb")) { Copy-Item -Force (Join-Path $refManagedSrc "NEM.ScriptCore.pdb") $sdkMngRef }
    if (Test-Path (Join-Path $refManagedSrc "NEM.ScriptCore.xml")) { Copy-Item -Force (Join-Path $refManagedSrc "NEM.ScriptCore.xml") $sdkMngRef }
}
foreach ($a in @("NEM.ScriptCodeGen","NEM.ScriptAnalyzers")) {
    $aDll = Join-Path $mngRoot "$a\bin\$refConfig\netstandard2.0\$a.dll"
    if (Test-Path $aDll) { Copy-Item -Force $aDll $sdkMngAnalyzers }
}
$metaSyncDir = Join-Path $mngRoot "NEM.ScriptMetaSync\bin\$refConfig\net10.0"
if (Test-Path $metaSyncDir) { robocopy $metaSyncDir $sdkMngTools /E /NFL /NDL /NJH /NJS /NP | Out-Null }

# 3) 構成ごとのバイナリとランタイムを書き出す
Write-Host "[4/4] 構成ごとのDLL/ランタイムを書き出し中..."
$packaged = @()
foreach ($cfg in $Configurations) {
    $binSrc     = Join-Path $generated "Bin\$cfg\NEMEngine"
    $appOutSrc  = Join-Path $generated "Output\$cfg\Sandbox"
    $managedSrc = Join-Path $generated "Managed\NEM.ScriptCore\$cfg"
    if (-not (Test-Path (Join-Path $binSrc "NEMEngine.dll"))) {
        Write-Host "  [スキップ] $cfg はビルドされていません: $binSrc"
        continue
    }

    $sdkBin     = Join-Path $OutDir "Bin\$cfg"
    $sdkRuntime = Join-Path $OutDir "Runtime\$cfg"
    $sdkManaged = Join-Path $sdkRuntime "Managed"
    foreach ($d in @($sdkBin, $sdkRuntime, $sdkManaged)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }

    # リンク用（import lib + DLL）
    Copy-Item -Force (Join-Path $binSrc "NEMEngine.dll") (Join-Path $sdkBin "NEMEngine.dll")
    Copy-Item -Force (Join-Path $binSrc "NEMEngine.lib") (Join-Path $sdkBin "NEMEngine.lib")

    # 実行時ランタイム（NEMEngine.dllはエンジンBinから、他はSandbox出力から）
    Copy-Item -Force (Join-Path $binSrc "NEMEngine.dll") (Join-Path $sdkRuntime "NEMEngine.dll")
    foreach ($dll in @("dxcompiler.dll","dxil.dll","nethost.dll","WinPixEventRuntime.dll")) {
        $src = Join-Path $appOutSrc $dll
        if (Test-Path -LiteralPath $src) { Copy-Item -Force $src (Join-Path $sdkRuntime $dll) }
    }
    # managed（NEM.ScriptCore.dll/pdb等）。pdbも入れてC#ブレークポイントを成立させる
    if (Test-Path $managedSrc) { Copy-Item -Force -Recurse (Join-Path $managedSrc "*") $sdkManaged }
    $packaged += $cfg
}

if ($packaged.Count -eq 0) { throw "書き出せた構成がありません。先にエンジンをビルドしてください。" }

# 4) SDK バージョン情報
$version = [ordered]@{
    configurations = $packaged
    packagedAtUtc  = (Get-Date).ToUniversalTime().ToString("o")
}
$version | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir "sdk_version.json") -Encoding UTF8

Write-Host ""
Write-Host "[完了] NEMEngine SDK を書き出しました: $OutDir"
Write-Host "        構成: $($packaged -join ', ')"
