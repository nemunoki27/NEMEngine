param(
    [string]$BuildToolPath = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildToolPath)) {
    $BuildToolPath = Join-Path $root "Generated\Output\Develop\NEMBuildTool\NEMBuildTool.exe"
}

$resolvedBuildTool = (Resolve-Path -LiteralPath $BuildToolPath).Path
$driver = "`"$resolvedBuildTool`" --merge-json %O %A %B %A"

& git -C $root config merge.nem-json.name "NEMEngine semantic JSON merge"
& git -C $root config merge.nem-json.driver $driver

Write-Host "Configured NEMEngine semantic JSON merge driver"
