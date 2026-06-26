# Repair a game project using the NEMEngine-side GameProject template.

param(
    [Parameter(Mandatory = $true)]
    [string]$GameRoot,
    [switch]$SkipGitIndexCleanup
)

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$ErrorActionPreference = "Stop"

$engineRoot = Split-Path -Parent $PSScriptRoot
$supportRoot = Join-Path $engineRoot "Templates\GameProject"
$repairScript = Join-Path $supportRoot "RepairGameProject.ps1"

if (-not (Test-Path -LiteralPath $repairScript)) {
    throw "RepairGameProject.ps1 was not found: $repairScript"
}

$repairParams = @{
    GameRoot = $GameRoot
    SupportRoot = $supportRoot
}
if ($SkipGitIndexCleanup) {
    $repairParams.SkipGitIndexCleanup = $true
}

& $repairScript @repairParams
