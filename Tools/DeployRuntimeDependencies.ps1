[CmdletBinding(DefaultParameterSetName = "TargetDirectory")]
param(
    [Parameter(Mandatory = $true, ParameterSetName = "TargetPath")]
    [string]$TargetPath,

    [Parameter(Mandatory = $true, ParameterSetName = "TargetDirectory")]
    [string]$TargetDirectory,

    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Develop", "Release")]
    [string]$Configuration,

    [string]$WindowsSdkBinaryDirectory = "",
    [string]$RuntimeDllPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding

function Resolve-TargetDirectory {
    if ($PSCmdlet.ParameterSetName -eq "TargetPath") {
        $fullTargetPath = [System.IO.Path]::GetFullPath($TargetPath)
        return [System.IO.Path]::GetDirectoryName($fullTargetPath)
    }
    return [System.IO.Path]::GetFullPath($TargetDirectory)
}

function Find-WindowsSdkBinaryDirectory {
    if (-not [string]::IsNullOrWhiteSpace($WindowsSdkBinaryDirectory)) {
        $explicitDirectory = [System.IO.Path]::GetFullPath($WindowsSdkBinaryDirectory)
        if (-not (Test-Path -LiteralPath (Join-Path $explicitDirectory "dxcompiler.dll")) -or
            -not (Test-Path -LiteralPath (Join-Path $explicitDirectory "dxil.dll"))) {
            throw "DirectX Shader Compiler runtime was not found: $explicitDirectory"
        }
        return $explicitDirectory
    }

    $sdkRoots = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:WindowsSdkDir)) {
        $sdkRoots.Add($env:WindowsSdkDir)
    }

    try {
        $installedRoots = Get-ItemProperty -LiteralPath `
            "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots" -ErrorAction Stop
        if (-not [string]::IsNullOrWhiteSpace($installedRoots.KitsRoot10)) {
            $sdkRoots.Add($installedRoots.KitsRoot10)
        }
    }
    catch {
    }

    if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
        $sdkRoots.Add((Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"))
    }

    foreach ($sdkRoot in $sdkRoots | Select-Object -Unique) {
        $binRoot = Join-Path $sdkRoot "bin"
        if (-not (Test-Path -LiteralPath $binRoot)) {
            continue
        }

        $versions = Get-ChildItem -LiteralPath $binRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object {
                try { [version]$_.Name }
                catch { [version]"0.0" }
            } -Descending

        foreach ($version in $versions) {
            $candidate = Join-Path $version.FullName "x64"
            if ((Test-Path -LiteralPath (Join-Path $candidate "dxcompiler.dll")) -and
                (Test-Path -LiteralPath (Join-Path $candidate "dxil.dll"))) {
                return $candidate
            }
        }
    }

    throw "DirectX Shader Compiler runtime was not found in the Windows SDK"
}

function Copy-RequiredFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$DestinationName
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Runtime dependency was not found: $Source"
    }

    $destination = Join-Path $resolvedTargetDirectory $DestinationName
    Copy-Item -LiteralPath $Source -Destination $destination -Force
    return $DestinationName
}

$engineRoot = Split-Path -Parent $PSScriptRoot
$resolvedTargetDirectory = Resolve-TargetDirectory
$sdkBinaryDirectory = Find-WindowsSdkBinaryDirectory
New-Item -ItemType Directory -Path $resolvedTargetDirectory -Force | Out-Null

$deployedFiles = [System.Collections.Generic.List[string]]::new()
if (-not [string]::IsNullOrWhiteSpace($RuntimeDllPath)) {
    $deployedFiles.Add((Copy-RequiredFile `
        -Source ([System.IO.Path]::GetFullPath($RuntimeDllPath)) `
        -DestinationName "NEMRuntime.dll"))
}

$deployedFiles.Add((Copy-RequiredFile `
    -Source (Join-Path $sdkBinaryDirectory "dxcompiler.dll") `
    -DestinationName "dxcompiler.dll"))
$deployedFiles.Add((Copy-RequiredFile `
    -Source (Join-Path $sdkBinaryDirectory "dxil.dll") `
    -DestinationName "dxil.dll"))
$deployedFiles.Add((Copy-RequiredFile `
    -Source (Join-Path $engineRoot "Project\Externals\dotnet-hosting\bin\x64\nethost.dll") `
    -DestinationName "nethost.dll"))

if ($Configuration -eq "Debug") {
    $deployedFiles.Add((Copy-RequiredFile `
        -Source (Join-Path $engineRoot "Project\Externals\WinPixEventRuntime\bin\x64\WinPixEventRuntime.dll") `
        -DestinationName "WinPixEventRuntime.dll"))
}

$manifest = [ordered]@{
    schemaVersion = 1
    configuration = $Configuration
    files = @($deployedFiles)
}
$manifestPath = Join-Path $resolvedTargetDirectory "nem.runtime-dependencies.json"
$manifestJson = $manifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText(
    $manifestPath,
    $manifestJson,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "[RuntimeDeploy] configuration=$Configuration target=$resolvedTargetDirectory files=$($deployedFiles.Count)"
