# NEMEngine ゲームプロジェクト 新規作成（対話形式）
# ゲーム作成.bat からダブルクリックで起動される想定。プロンプトと表示はすべて日本語。

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$ErrorActionPreference = "Stop"

$scriptDir  = $PSScriptRoot
$engineRoot = Split-Path -Parent $scriptDir
$sdk        = Join-Path $engineRoot "Generated\SDK"

Write-Host "============================================"
Write-Host "  NEMEngine ゲームプロジェクト 新規作成"
Write-Host "============================================"
Write-Host ""

# SDKの存在チェック
if (-not (Test-Path (Join-Path $sdk "Include\NEMEngineRuntime.h"))) {
    Write-Host "[エラー] エンジンSDKが見つかりません:"
    Write-Host "         $sdk"
    Write-Host ""
    Write-Host "先に「SDK作成.bat」をダブルクリックしてSDKを作成してください。"
    Write-Host ""
    Read-Host "Enterキーを押すと終了します"
    exit 1
}

# プロジェクト名の入力
$name = $null
while (-not $name) {
    $entered = Read-Host "ゲームプロジェクト名を入力してください（半角英数字とアンダースコア / 先頭は数字以外）"
    if ($entered -match '^[A-Za-z_][A-Za-z0-9_]*$') {
        $name = $entered
    } else {
        Write-Host "  → 名前が不正です。例: MyGame / Action01 / puzzle_game"
    }
}

# 作成先フォルダの入力
$defaultDest = Split-Path -Parent $engineRoot
Write-Host ""
Write-Host "ゲームを作成するフォルダを入力してください。"
Write-Host "（何も入力せずEnterを押すと: $defaultDest）"
$dest = Read-Host "作成先フォルダ"
if ([string]::IsNullOrWhiteSpace($dest)) { $dest = $defaultDest }
if (-not (Test-Path -LiteralPath $dest)) {
    Write-Host "[エラー] 指定したフォルダが存在しません: $dest"
    Read-Host "Enterキーを押すと終了します"
    exit 1
}

# エンジンSDKの参照方法（任意）
Write-Host ""
Write-Host "チームで共有する場合は、【SDK専用リポジトリ】のGit URLを入力してください。"
Write-Host "  ※ これは「SDK公開.bat」で作るSDK配布用リポジトリのURLです。"
Write-Host "  ※ エンジン本体のソースリポジトリ(NEMEngine.git)ではありません。"
Write-Host "（自分のPCだけで試す場合は、何も入力せずEnter → ローカルのSDKを参照します）"
$sdkUrl = Read-Host "SDK専用リポジトリのGit URL（任意）"

# 確認
Write-Host ""
Write-Host "----- 次の内容で作成します -----"
Write-Host "  プロジェクト名 : $name"
Write-Host "  作成先         : $dest\$name"
if ([string]::IsNullOrWhiteSpace($sdkUrl)) {
    Write-Host "  エンジンSDK    : ローカル参照（$sdk）"
} else {
    Write-Host "  エンジンSDK    : Git submodule（$sdkUrl）"
}
Write-Host "--------------------------------"
$ok = Read-Host "この内容で作成しますか？ (Y/n)"
if ($ok -match '^[nN]') {
    Write-Host "中止しました。"
    Read-Host "Enterキーを押すと終了します"
    exit 0
}

Write-Host ""
try {
    if ([string]::IsNullOrWhiteSpace($sdkUrl)) {
        & (Join-Path $scriptDir "NewGame.ps1") -Name $name -Dest $dest -SdkPath $sdk
    } else {
        & (Join-Path $scriptDir "NewGame.ps1") -Name $name -Dest $dest -SdkUrl $sdkUrl
    }
} catch {
    Write-Host ""
    Write-Host "[エラー] 作成に失敗しました: $($_.Exception.Message)"
    Read-Host "Enterキーを押すと終了します"
    exit 1
}

Write-Host ""
Write-Host "完了しました。Enterキーを押すと終了します。"
Read-Host | Out-Null
