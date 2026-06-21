# NewGame: prebuilt NEMEngine SDK を参照する新しいゲームプロジェクトを1コマンドで作る
#
# 使い方の例:
#   ローカル開発(自分のPCだけ、git不要):
#     Tools\NewGame.ps1 -Name MyGame -SdkPath C:\path\to\NEMEngine\Generated\SDK
#   チーム配布(SDKをリモートgitに置いてsubmoduleで参照):
#     Tools\NewGame.ps1 -Name MyGame -SdkUrl https://github.com/you/NEMEngineSDK.git
#
# 注意: -SdkUrl はエンジン本体のソースではなく「prebuilt SDK 専用リポジトリ」のURLを指定する

param(
    [Parameter(Mandatory = $true)]
    [string]$Name,
    [string]$SdkPath = "",
    [string]$SdkUrl = "",
    [string]$Dest = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
    throw "ゲーム名は半角英数字とアンダースコアのみ、先頭は数字以外にしてください: '$Name'"
}
if ([string]::IsNullOrEmpty($SdkPath) -and [string]::IsNullOrEmpty($SdkUrl)) {
    throw "-SdkPath <ローカルSDKフォルダ> または -SdkUrl <SDKのGitリポジトリURL> を指定してください。"
}

$engineRoot = Split-Path -Parent $PSScriptRoot
$template   = Join-Path $engineRoot "Templates\GameProject"
if ([string]::IsNullOrEmpty($Dest)) { $Dest = (Get-Location).Path }
$gameRoot = Join-Path $Dest $Name
if (Test-Path -LiteralPath $gameRoot) { throw "すでに同名のフォルダが存在します: $gameRoot" }

# 失敗時に作りかけのゲームフォルダを安全に削除する
# External\NEMEngine がジャンクションのときはSDK本体を消さないよう、reparse pointだけ先に外す
function Remove-PartialGame([string]$root) {
    if (-not (Test-Path -LiteralPath $root)) { return }
    $ee = Join-Path $root "External\NEMEngine"
    if (Test-Path -LiteralPath $ee) {
        $item = Get-Item -LiteralPath $ee -Force
        if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            [System.IO.Directory]::Delete($ee, $false)
        }
    }
    Get-ChildItem -Recurse -Force $root -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Attributes = 'Normal' } catch {} }
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

try {
    Write-Host "[1/5] ゲームフォルダを作成中: $gameRoot"
    $appRoot = Join-Path $gameRoot "Project\$Name"
    New-Item -ItemType Directory -Force -Path (Join-Path $gameRoot "Premake"), $appRoot, (Join-Path $gameRoot "External") | Out-Null

    Write-Host "[2/5] テンプレートをコピー中（__GAME_NAME__ -> $Name に置換）"
    Copy-Item -Recurse -Force (Join-Path $template "Premake\*") (Join-Path $gameRoot "Premake")
    Copy-Item -Recurse -Force (Join-Path $template "Project\__GAME_NAME__\*") $appRoot
    if (Test-Path (Join-Path $template ".gitignore")) { Copy-Item -Force (Join-Path $template ".gitignore") (Join-Path $gameRoot ".gitignore") }
    if (Test-Path (Join-Path $engineRoot "Project\EditorLayout.ini")) { Copy-Item -Force (Join-Path $engineRoot "Project\EditorLayout.ini") (Join-Path $gameRoot "Project\EditorLayout.ini") }
    # ゲームルート直下のツール(SDK更新.bat / UpdateSdk.ps1 等)をコピーする
    Get-ChildItem -LiteralPath $template -File | Where-Object { $_.Extension -in '.bat','.ps1' } | ForEach-Object {
        Copy-Item -Force $_.FullName (Join-Path $gameRoot $_.Name)
    }

    # テンプレ内の __GAME_NAME__ トークンを置換し、ファイル名のトークンもリネームする
    Get-ChildItem -Recurse -File $gameRoot | Where-Object { $_.Extension -in '.lua','.csproj','.cs','.json','.txt','.md','.bat' } | ForEach-Object {
        # テンプレはUTF-8なので必ずUTF-8として読み書きする（既定コードページで読むと日本語コメントが化ける）
        $text = [System.IO.File]::ReadAllText($_.FullName)
        $replaced = $text.Replace('__GAME_NAME__', $Name)
        if ($replaced -ne $text) { [System.IO.File]::WriteAllText($_.FullName, $replaced, (New-Object System.Text.UTF8Encoding($false))) }
    }
    Get-ChildItem -Recurse -File $gameRoot | Where-Object { $_.Name -match '__GAME_NAME__' } | ForEach-Object {
        Rename-Item -LiteralPath $_.FullName -NewName ($_.Name.Replace('__GAME_NAME__', $Name))
    }

    Write-Host "[3/5] エンジンSDKを External\NEMEngine に設定中"
    $externalEngine = Join-Path $gameRoot "External\NEMEngine"
    if (-not [string]::IsNullOrEmpty($SdkUrl)) {
        Push-Location $gameRoot
        try {
            git init | Out-Null
            git branch -M main 2>$null | Out-Null
            git -c protocol.file.allow=always submodule add $SdkUrl External/NEMEngine
            git submodule update --init --recursive
        } finally { Pop-Location }
    } else {
        $resolvedSdk = (Resolve-Path -LiteralPath $SdkPath).Path
        if (-not (Test-Path (Join-Path $resolvedSdk "Include\NEMEngineRuntime.h"))) {
            throw "正しいSDKフォルダではありません（Include\NEMEngineRuntime.h が見つかりません）: $resolvedSdk"
        }
        New-Item -ItemType Junction -Path $externalEngine -Target $resolvedSdk | Out-Null
    }

    # External\NEMEngine が prebuilt SDK かどうかを検証する（エンジンソースリポジトリ等の誤指定を弾く）
    if (-not (Test-Path (Join-Path $externalEngine "Include\NEMEngineRuntime.h"))) {
        throw ("External\NEMEngine が prebuilt SDK ではありません（Include\NEMEngineRuntime.h が見つかりません）。" + [Environment]::NewLine +
            "指定したURLはエンジン本体のソースリポジトリの可能性があります。ゲームが参照するのは prebuilt SDK 専用リポジトリです。" + [Environment]::NewLine +
            "手順: 1) SDK作成.bat でSDKを作る  2) SDK公開.bat でSDK専用リポジトリへ公開  3) そのSDKリポジトリのURLを指定する")
    }

    Write-Host "[4/5] Visual Studio プロジェクトを生成中"
    & (Join-Path $gameRoot "Premake\generate_vs2026.bat")
    if ($LASTEXITCODE -ne 0) { throw "ゲームプロジェクトの生成に失敗しました。" }

    Write-Host "[5/5] 完了しました。"
    Write-Host ""
    Write-Host "  ゲームプロジェクト : $gameRoot"
    Write-Host "  Visual Studioで開く : $gameRoot\Project\$Name.slnx"
    Write-Host "  '$Name' プロジェクトを Debug / x64 でビルドして実行してください。"
    Write-Host "  C#スクリプトの場所  : $gameRoot\Project\$Name\GameAssets\ （デバッグは Visual Studio でソリューションを開く）"
}
catch {
    Write-Host ""
    Write-Host "[クリーンアップ] 失敗したので作りかけのゲームフォルダを削除します..."
    Remove-PartialGame $gameRoot
    throw
}
