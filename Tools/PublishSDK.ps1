# NEMEngine SDK をSDK専用のGitリポジトリへ公開する
# Generated\SDK を公開先の最新履歴へ接続して push する

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$ErrorActionPreference = "Stop"

$engineRoot = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $engineRoot "Generated\SDK"
$remoteUrl = "https://github.com/nemunoki27/NEMEngineSDK.git"

Write-Host "============================================"
Write-Host "  NEMEngine SDK 公開（Gitリポジトリへ）"
Write-Host "============================================"
Write-Host ""

if (-not (Test-Path (Join-Path $sdk "Include\NEMEngineRuntime.h"))) {
    Write-Host "[エラー] SDKが見つかりません: $sdk"
    Write-Host "先に「SDK作成.bat」をダブルクリックしてSDKを作成してください。"
    Read-Host "Enterキーを押すと終了します"
    exit 1
}

Push-Location $sdk
try {
    if (-not (Test-Path (Join-Path $sdk ".git"))) {
        Write-Host "SDKフォルダをGitリポジトリとして初期化します..."
        git init | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Gitリポジトリの初期化に失敗しました。" }
    }

    # 配布物は実体をそのまま管理するため、改行コード変換を無効化する
    Set-Content -LiteralPath (Join-Path $sdk ".gitattributes") -Value "* -text" -Encoding ASCII

    $hasOrigin = @(git remote) -contains "origin"
    if ($hasOrigin) {
        git remote set-url origin $remoteUrl
        if ($LASTEXITCODE -ne 0) { throw "公開先URLの更新に失敗しました。" }
    } else {
        git remote add origin $remoteUrl
        if ($LASTEXITCODE -ne 0) { throw "公開先URLの設定に失敗しました。" }
    }

    Write-Host "GitHubの最新履歴を取得します..."
    git fetch origin main
    if ($LASTEXITCODE -ne 0) {
        throw "GitHubの最新履歴を取得できませんでした。GitHubの認証状態を確認してください。"
    }

    # SDKフォルダが再作成されても、現在の配布物をGitHub側の最新履歴へ接続する
    $originMain = (git rev-parse refs/remotes/origin/main).Trim()
    if ($LASTEXITCODE -ne 0) { throw "GitHubのmainブランチを確認できませんでした。" }
    git symbolic-ref HEAD refs/heads/main
    if ($LASTEXITCODE -ne 0) { throw "mainブランチへの切り替えに失敗しました。" }
    git update-ref refs/heads/main $originMain
    if ($LASTEXITCODE -ne 0) { throw "mainブランチを最新履歴へ接続できませんでした。" }
    git read-tree refs/remotes/origin/main
    if ($LASTEXITCODE -ne 0) { throw "Gitのインデックス更新に失敗しました。" }

    Write-Host "SDKの変更を確認します..."
    git add -A
    if ($LASTEXITCODE -ne 0) { throw "SDKの変更をステージできませんでした。" }

    git diff --cached --quiet
    $diffResult = $LASTEXITCODE
    if ($diffResult -eq 0) {
        Write-Host "公開する変更はありません。"
    } elseif ($diffResult -eq 1) {
        $stamp = (Get-Date).ToString("yyyy-MM-dd HH:mm")
        git commit -m "Update NEMEngine SDK ($stamp)"
        if ($LASTEXITCODE -ne 0) { throw "SDKのコミットに失敗しました。" }
    } else {
        throw "SDKの変更確認に失敗しました。"
    }

    Write-Host "NEMEngineSDKへ push します..."
    git push -u origin main
    if ($LASTEXITCODE -ne 0) {
        throw "NEMEngineSDKへのpushに失敗しました。GitHubの認証状態を確認してください。"
    }
} catch {
    Write-Host ""
    Write-Host "[エラー] $($_.Exception.Message)"
    Read-Host "Enterキーを押すと終了します"
    exit 1
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "[完了] SDKを公開しました。"
Write-Host "  SDKリポジトリURL : $remoteUrl"
Write-Host ""
Read-Host "Enterキーを押すと終了します"
