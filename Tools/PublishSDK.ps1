# NEMEngine SDK を「SDK専用のGitリポジトリ」へ公開する（対話形式）
# Generated\SDK をそれ自身のgitリポジトリにして push する
# ここで得られるURLを「ゲーム作成.bat」のSDK URLに指定すると、ゲームはsubmoduleでSDKを参照できる
# 注意: エンジン本体のソースリポジトリとは別の、配布用リポジトリを作る

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$ErrorActionPreference = "Stop"

$engineRoot = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $engineRoot "Generated\SDK"

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
        git branch -M main 2>$null | Out-Null
        # 配布物は実体をそのまま管理するため、改行コード変換を無効化する
        Set-Content -LiteralPath (Join-Path $sdk ".gitattributes") -Value "* -text" -Encoding ASCII
    }

    Write-Host "変更をコミットします..."
    git add -A | Out-Null
    $stamp = (Get-Date).ToString("yyyy-MM-dd HH:mm")
    git commit -m "Update NEMEngine SDK ($stamp)" 2>$null | Out-Null

    # origin の有無は get-url ではなく remote 一覧で判定する
    # （get-url は origin が無いと stderr にエラーを出し、Stop設定だと途中で止まるため）
    $hasOrigin = @(git remote) -contains 'origin'

    if ($hasOrigin) {
        Write-Host "既存のリモートへ push します..."
        git push -u origin main
    } else {
        Write-Host ""
        Write-Host "公開先を選んでください。"
        Write-Host "  ・GitHub等で作った【空のリポジトリ】のURLを入力 → そこへ push"
        Write-Host "  ・何も入力せずEnter → GitHub CLI(gh)で新規作成して push"
        $url = Read-Host "公開先のGit URL（任意）"
        if (-not [string]::IsNullOrWhiteSpace($url)) {
            git remote add origin $url
            git push -u origin main
        } else {
            $gh = Get-Command gh -ErrorAction SilentlyContinue
            if (-not $gh) {
                Write-Host ""
                Write-Host "[情報] GitHub CLI(gh) が見つかりません。手動で公開してください:"
                Write-Host "  1) GitHubで【空の】リポジトリを作成（例: NEMEngineSDK）"
                Write-Host "  2) 次を実行:"
                Write-Host "       cd `"$sdk`""
                Write-Host "       git remote add origin <作ったリポジトリのURL>"
                Write-Host "       git push -u origin main"
                Read-Host "Enterキーを押すと終了します"
                exit 0
            }
            $repoName = Read-Host "新規リポジトリ名（空欄なら NEMEngineSDK）"
            if ([string]::IsNullOrWhiteSpace($repoName)) { $repoName = "NEMEngineSDK" }
            $vis = Read-Host "公開範囲 private/public（空欄なら private）"
            if ($vis -ne "public") { $vis = "private" }
            gh repo create $repoName "--$vis" --source . --remote origin --push
        }
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "[完了] SDKを公開しました。"
$pushedUrl = ""
Push-Location $sdk
if (@(git remote) -contains 'origin') { $pushedUrl = (git remote get-url origin) }
Pop-Location
if ($pushedUrl) {
    Write-Host "  SDKリポジトリURL : $pushedUrl"
    Write-Host "  → このURLを「ゲーム作成.bat」の『SDK専用リポジトリのGit URL』に入力してください。"
}
Write-Host ""
Read-Host "Enterキーを押すと終了します"
