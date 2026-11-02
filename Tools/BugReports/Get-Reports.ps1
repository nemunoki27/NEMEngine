param([string]$ID = '', [int]$Limit = 30)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repository 'Generated/BugReports'
if ($Limit -lt 1 -or $Limit -gt 100) { throw '取得件数は1〜100を指定してください' }
if ($ID -and $ID -notmatch '^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$') { throw '受付番号の形式が不正です' }
$query = if ($ID) { "SELECT * FROM reports WHERE id='$ID' AND state='ready' AND received_at >= unixepoch()-2592000" }
else { "SELECT id, datetime(received_at,'unixepoch') AS received_utc, title, kind, launcher_version, engine_version FROM reports WHERE state='ready' AND received_at >= unixepoch()-2592000 ORDER BY received_at DESC LIMIT $Limit" }
Push-Location (Join-Path $repository 'Tools/Distribution')
try {
    # 管理権限は既存のCloudflare認証から取得する
    $result = & npx --no-install wrangler d1 execute nemengine-bug-reports --config ../BugReports/wrangler.jsonc --remote --json --command $query
    if ($LASTEXITCODE -ne 0) { throw '報告一覧を取得できません Cloudflareの認証を確認してください' }
    $rows = @((($result -join "`n") | ConvertFrom-Json)[0].results)
    if (!$ID) { $rows | Format-Table -AutoSize; return }
    if ($rows.Count -ne 1) { throw '指定した報告が見つかりません' }
    $keys = @($rows[0].object_keys | ConvertFrom-Json)
    $target = Join-Path $output $ID
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    foreach ($key in $keys) {
        if ($key -notmatch "^\d{4}-\d{2}-\d{2}/$ID/(report\.json|launcher\.log|engine\.log|gameLogic\.log)$") { throw '添付先の形式が不正です' }
        $file = Join-Path $target ([IO.Path]::GetFileName($key))
        & npx --no-install wrangler r2 object get "nemengine-bug-reports/$key" --file $file --remote
        if ($LASTEXITCODE -ne 0) { throw '添付を取得できませんでした' }
    }
    Write-Host "取得先: $target"
    Get-Content -LiteralPath (Join-Path $target 'report.json') -Raw
} finally { Pop-Location }
