Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$support = Join-Path $engineRoot 'Templates\GameProject'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $support 'RepairGameProject.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($name in @('Write-Utf8NoBom', 'Copy-TemplateFile', 'Sync-PremakeFiles')) {
    $node = $ast.Find({ param($item)
        $item -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $item.Name -eq $name
    }, $true)
    if (-not $node) { throw "修復関数が見つかりません: $name" }
    . ([scriptblock]::Create($node.Extent.Text))
}
$root = Join-Path $engineRoot ('Generated\SDKSettingsTests\' + [Guid]::NewGuid().ToString('N'))
Sync-PremakeFiles $root $support 'OwnershipProbe'
$settings = Join-Path $root 'Premake\game_settings.lua'
if (-not (Test-Path -LiteralPath $settings -PathType Leaf)) { throw '独自設定が作成されませんでした' }
$custom = 'defines { "GAME_CUSTOM_SETTING" }'
[System.IO.File]::WriteAllText($settings, $custom)
Sync-PremakeFiles $root $support 'OwnershipProbe'
if ([System.IO.File]::ReadAllText($settings) -cne $custom) { throw '独自設定が上書きされました' }
$common = [System.IO.File]::ReadAllText((Join-Path $root 'Premake\premake5.lua'))
if ($common.Contains('__GAME_NAME__') -or -not $common.Contains('OwnershipProbe')) { throw 'ゲーム名が反映されませんでした' }
if ($common.LastIndexOf('game_settings.lua') -lt $common.LastIndexOf('NEM_GameApplyConfigFilters()')) {
    throw '独自設定の評価順が不正です'
}
Write-Output 'SDK settings ownership passed'
