param([Parameter(Mandatory = $true)][string]$Root)
$ErrorActionPreference = 'Stop'
$OutputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding

# Windows PowerShell自身の読込方法で梱包済みスクリプトを検査する
$scripts = @(Get-ChildItem -LiteralPath $Root -File -Recurse -Filter '*.ps1')
foreach ($script in $scripts) {
    $tokens = $null
    $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($script.FullName, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw ($script.FullName + "`n" + ($errors.Message -join "`n")) }
}
Write-Output ("Windows PowerShell検証成功: {0}ファイル" -f $scripts.Count)
