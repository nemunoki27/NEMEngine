param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [Parameter(Mandatory = $true)]
    [string]$Configuration
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Configuration -notmatch '^[A-Za-z0-9._-]+$') {
    throw "ビルド構成名が不正です: $Configuration"
}

$fullOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($fullOutputPath)
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$buildTime = (Get-Date).ToString(
    "yyyy-MM-dd HH:mm:ss",
    [System.Globalization.CultureInfo]::InvariantCulture)
$contents = @"
#pragma once

namespace Engine::EditorBuildInfo {

	inline constexpr char kVersion[] = "$buildTime";
	inline constexpr char kConfiguration[] = "$Configuration";
}
"@

[System.IO.File]::WriteAllText(
    $fullOutputPath,
    $contents,
    [System.Text.UTF8Encoding]::new($false))
