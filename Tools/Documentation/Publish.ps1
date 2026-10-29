param(
    [string]$ProjectName = '',
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$documentationRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$websiteRoot = Join-Path $documentationRoot 'Docs/Website'
$outputRoot = Join-Path $documentationRoot 'Generated/Documentation'
$configPath = Join-Path $PSScriptRoot 'publish.config.json'
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json

if ($ProjectName) {
    if ($config.projectName -and $config.projectName -ne $ProjectName) {
        throw '既に登録された公開先と異なります。変更する場合はpublish.config.jsonを確認してください'
    }
    $config.projectName = $ProjectName
}
if (-not $config.projectName) {
    throw '初回は -ProjectName nemengine-docs-任意の文字列 を指定してください。公開先は次回から再利用します'
}
if ($config.projectName -notmatch '^[a-z][a-z0-9-]{2,57}[a-z0-9]$') {
    throw 'Pages Project名は小文字英数字とハイフンで4〜59文字にしてください'
}
if ($config.accountID -notmatch '^[a-f0-9]{32}$') {
    throw '公開するCloudflareアカウントをpublish.config.jsonへ設定してください'
}

$previousCloudflareAccountID = $env:CLOUDFLARE_ACCOUNT_ID
$previousWranglerMetrics = $env:WRANGLER_SEND_METRICS
$env:CLOUDFLARE_ACCOUNT_ID = $config.accountID
$env:WRANGLER_SEND_METRICS = 'false'
Push-Location $websiteRoot
try {
    if (-not $SkipBuild) {
        & npm run docs:build
        if ($LASTEXITCODE -ne 0) { throw 'マニュアルのビルド・検証に失敗しました' }
    }
    & node (Join-Path $PSScriptRoot 'verify-dist.mjs')
    if ($LASTEXITCODE -ne 0) { throw '公開物の検証に失敗しました' }

    # 認証は公式CLIからユーザーのブラウザーへ渡す
    $identityText = & npx --no-install wrangler whoami --json
    if ($LASTEXITCODE -ne 0) {
        throw '認証がありません。Docs/Websiteで npx wrangler login を実行し、ブラウザーで認証してください'
    }
    $identity = ($identityText -join "`n") | ConvertFrom-Json
    if (@($identity.accounts | Where-Object { $_.id -eq $config.accountID }).Count -eq 0) {
        throw '指定アカウントが認証先にありません。Cloudflareのログイン先を確認してください'
    }
    $projectsText = & npx --no-install wrangler pages project list --json
    if ($LASTEXITCODE -ne 0) { throw 'Cloudflare Pagesの公開先を取得できませんでした' }
    $projects = ($projectsText -join "`n") | ConvertFrom-Json
    $existing = @($projects | Where-Object { $_.'Project Name' -eq $config.projectName })
    if ($existing.Count -eq 0) {
        # 新規作成時のWorkersへの自動切替を抑止
        & npx --no-install wrangler pages project create $config.projectName --production-branch $config.branch --force
        if ($LASTEXITCODE -ne 0) { throw 'Pages Projectの作成に失敗しました。同じ名前で再作成せず管理画面を確認してください' }
    }
    # 作成済みの公開先を保存し、再実行で増やさない
    [System.IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json) + "`r`n", [System.Text.UTF8Encoding]::new($false))
    & npx --no-install wrangler pages deploy (Join-Path $outputRoot 'dist') `
        --project-name $config.projectName --branch $config.branch --commit-dirty=true
    if ($LASTEXITCODE -ne 0) { throw '公開に失敗しました。表示URLとCloudflareの状態を確認してください' }
    Write-Host "公開先: https://$($config.projectName).pages.dev"
    & node (Join-Path $PSScriptRoot 'http-check.mjs') "https://$($config.projectName).pages.dev/"
    if ($LASTEXITCODE -ne 0) { throw '公開後のページまたはヘッダー確認に失敗しました。Cloudflare側の状態を確認してください' }
}
finally {
    Pop-Location
    $env:CLOUDFLARE_ACCOUNT_ID = $previousCloudflareAccountID
    $env:WRANGLER_SEND_METRICS = $previousWranglerMetrics
}
