# ゲームが参照する NEMEngine SDK を最新へ更新する
# - git submodule 参照: SDK専用リポジトリから最新を取得する
# - ローカル junction 参照: SDK作成.bat の再エクスポート結果がそのまま反映されるため取得は不要
# 最後に Visual Studio プロジェクトを再生成する

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$ErrorActionPreference = "Stop"

# このスクリプトはゲームルート直下に置く
$gameRoot = $PSScriptRoot
$externalEngine = Join-Path $gameRoot "External\NEMEngine"

Write-Host "============================================"
Write-Host "  NEMEngine SDK 更新"
Write-Host "============================================"
Write-Host ""

if (-not (Test-Path -LiteralPath $externalEngine)) {
    Write-Host "[エラー] External\NEMEngine が見つかりません: $externalEngine"
    Read-Host "Enterキーを押すと終了します"
    exit 1
}

# ジャンクション(ローカルSDK)か git submodule かで更新方法が変わる
$item = Get-Item -LiteralPath $externalEngine -Force
$isJunction = [bool]($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)

if ($isJunction) {
    Write-Host "ローカルSDK(ジャンクション)参照です。"
    Write-Host "エンジン側で SDK作成.bat を実行すれば、その内容がそのまま反映されます。取得は不要です。"
} else {
    Write-Host "SDKリポジトリから最新を取得します..."
    # git は進捗やメッセージを stderr へ出すため、ネイティブ stderr でスクリプトを止めないよう一時的に緩める
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $updateOk = $false
    Push-Location $externalEngine
    try {
        git fetch origin
        # 実行中エディタが書き込んだ設定ファイル等のローカル変更を破棄し、確実に最新SDK(origin/main)へ揃える
        # SDKは配布物なのでローカル変更を保持する必要はない
        git reset --hard origin/main
        git submodule update --init --recursive
        $updateOk = ($LASTEXITCODE -eq 0)
    } finally {
        Pop-Location
        $ErrorActionPreference = $prevEAP
    }
    if (-not $updateOk) {
        Write-Host ""
        Write-Host "[エラー] SDKの取得に失敗しました。External\NEMEngine の状態を確認してください。"
        Read-Host "Enterキーを押すと終了します"
        exit 1
    }
}

Write-Host ""
Write-Host "Visual Studio プロジェクトを再生成します..."
& (Join-Path $gameRoot "Premake\generate_vs2026.bat")
if ($LASTEXITCODE -ne 0) {
    Write-Host "[エラー] プロジェクトの再生成に失敗しました。"
    Read-Host "Enterキーを押すと終了します"
    exit 1
}

Write-Host ""
Write-Host "[完了] SDKを更新しました。"
Write-Host "  Visual Studio でソリューションを開き直し、ビルドし直してください。"
Write-Host ""
Read-Host "Enterキーを押すと終了します"
