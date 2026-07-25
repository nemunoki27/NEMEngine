<#
.SYNOPSIS
  リファクタリング前後の静的構造と回帰項目を検証する

.DESCRIPTION
  Staticではソース構造、include表記、JSON、Shader参照、ABIを検証する
  FullではShaderコンパイル、Managed stress、Debug・Develop・Releaseビルドも実行する

.EXAMPLE
  pwsh -File RefactoringBaseline.ps1 -Mode Verify -Scope Static

.EXAMPLE
  pwsh -File RefactoringBaseline.ps1 -Mode Verify -Scope Full
#>
param(
    [ValidateSet("Capture", "Verify")]
    [string]$Mode = "Verify",
    [ValidateSet("Static", "Full")]
    [string]$Scope = "Static",
    [string[]]$Configurations = @("Debug", "Develop", "Release"),
    [string]$Target = "Sandbox",
    [string]$BaselinePath = "",
    [string]$ReportPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
$engineRoot = Join-Path $repoRoot "Project\Engine"
$shaderRoot = Join-Path $engineRoot "Assets\Shaders"
$generatedRoot = Join-Path $repoRoot "Generated\Refactoring"

if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
    $BaselinePath = Join-Path $PSScriptRoot "RefactoringBaseline.json"
}
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $generatedRoot "BaselineReport.json"
}

function Write-Utf8Json {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [object]$Value
    )

    $directory = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($Path))
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    $json = $Value | ConvertTo-Json -Depth 32
    [System.IO.File]::WriteAllText($Path, $json, [System.Text.UTF8Encoding]::new($false))
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return [System.IO.Path]::GetRelativePath($repoRoot, $Path).Replace("\", "/")
}

function Get-LineCount {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $count = 0
    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        $count++
    }
    return $count
}

function Get-SourceFiles {

    return @(
        Get-ChildItem -LiteralPath $engineRoot -Recurse -File |
            Where-Object {
                $_.Extension -in ".h", ".cpp", ".hlsl", ".hlsli", ".cs" -and
                $_.FullName -notmatch "[\\/](obj|bin)[\\/]"
            }
    )
}

function Get-SourceMetrics {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo[]]$SourceFiles
    )

    $cppFiles = @($SourceFiles | Where-Object { $_.Extension -in ".h", ".cpp" })
    $shaderFiles = @($SourceFiles | Where-Object { $_.Extension -in ".hlsl", ".hlsli" })
    $managedFiles = @($SourceFiles | Where-Object Extension -eq ".cs")

    $lineEntries = foreach ($file in $SourceFiles) {
        [pscustomobject]@{
            path = Get-RelativePath $file.FullName
            lines = Get-LineCount $file.FullName
        }
    }

    $duplicateGroups = @(
        $SourceFiles |
            Where-Object Length -gt 0 |
            ForEach-Object {
                [pscustomobject]@{
                    hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
                    path = Get-RelativePath $_.FullName
                }
            } |
            Group-Object hash |
            Where-Object Count -gt 1 |
            ForEach-Object {
                [pscustomobject]@{
                    count = $_.Count
                    files = @($_.Group.path | Sort-Object)
                }
            }
    )

    $cppLines = @($lineEntries | Where-Object path -match "\.(h|cpp)$")
    $shaderLines = @($lineEntries | Where-Object path -match "\.(hlsl|hlsli)$")
    $managedLines = @($lineEntries | Where-Object path -match "\.cs$")

    return [pscustomobject]@{
        totalFiles = $SourceFiles.Count
        cppFiles = $cppFiles.Count
        headerFiles = @($cppFiles | Where-Object Extension -eq ".h").Count
        cppSourceFiles = @($cppFiles | Where-Object Extension -eq ".cpp").Count
        shaderFiles = $shaderFiles.Count
        managedFiles = $managedFiles.Count
        cppLines = ($cppLines.lines | Measure-Object -Sum).Sum
        shaderLines = ($shaderLines.lines | Measure-Object -Sum).Sum
        managedLines = ($managedLines.lines | Measure-Object -Sum).Sum
        cppFilesAtMost20Lines = @($cppLines | Where-Object lines -le 20).Count
        cppFilesAtMost40Lines = @($cppLines | Where-Object lines -le 40).Count
        cppFilesAtMost80Lines = @($cppLines | Where-Object lines -le 80).Count
        cppFilesAtLeast1000Lines = @($cppLines | Where-Object lines -ge 1000).Count
        duplicateGroups = $duplicateGroups.Count
        duplicateFiles = @($duplicateGroups.files).Count
        duplicateDetails = $duplicateGroups
        largestFiles = @($lineEntries | Sort-Object lines -Descending | Select-Object -First 20)
    }
}

function Get-CanonicalFileMap {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $map = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File) {
        $relative = [System.IO.Path]::GetRelativePath($Root, $file.FullName).Replace("\", "/")
        $key = $relative.ToLowerInvariant()
        if (-not $map.ContainsKey($key)) {
            $map[$key] = $relative
        }
    }
    return $map
}

function Get-CppIncludeCaseMismatches {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo[]]$CppFiles
    )

    $canonicalFiles = Get-CanonicalFileMap $engineRoot
    $engineRootPrefix = [System.IO.Path]::GetFullPath($engineRoot).TrimEnd("\") + "\"
    $mismatches = New-Object System.Collections.Generic.List[object]

    foreach ($file in $CppFiles) {
        foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
            if ($line -notmatch '^\s*#include\s*([<"])([^>"]+)[>"]') {
                continue
            }

            $includePath = $Matches[2].Replace("\", "/")
            $candidate = ""
            if ($includePath.StartsWith("Engine/", [System.StringComparison]::Ordinal)) {
                $candidate = $includePath.Substring("Engine/".Length)
            }
            elseif ($Matches[1] -eq '"') {
                $fullCandidate = [System.IO.Path]::GetFullPath((Join-Path $file.DirectoryName $includePath))
                if ($fullCandidate.StartsWith($engineRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $candidate = [System.IO.Path]::GetRelativePath($engineRoot, $fullCandidate).Replace("\", "/")
                }
            }

            if ([string]::IsNullOrWhiteSpace($candidate)) {
                continue
            }

            $key = $candidate.ToLowerInvariant()
            if ($canonicalFiles.ContainsKey($key) -and $candidate -cne $canonicalFiles[$key]) {
                $mismatches.Add([pscustomobject]@{
                    source = Get-RelativePath $file.FullName
                    include = $includePath
                    actual = "Engine/" + $canonicalFiles[$key]
                })
            }
        }
    }

    return $mismatches.ToArray()
}

function Get-ShaderIncludeCaseMismatches {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo[]]$ShaderFiles
    )

    $canonicalFiles = Get-CanonicalFileMap $shaderRoot
    $shaderRootPrefix = [System.IO.Path]::GetFullPath($shaderRoot).TrimEnd("\") + "\"
    $mismatches = New-Object System.Collections.Generic.List[object]

    foreach ($file in $ShaderFiles) {
        foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
            if ($line -notmatch '^\s*#include\s*"([^"]+)"') {
                continue
            }

            $includePath = $Matches[1].Replace("\", "/")
            $fullCandidate = [System.IO.Path]::GetFullPath((Join-Path $file.DirectoryName $includePath))
            if (-not $fullCandidate.StartsWith($shaderRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                continue
            }

            $candidate = [System.IO.Path]::GetRelativePath($shaderRoot, $fullCandidate).Replace("\", "/")
            $key = $candidate.ToLowerInvariant()
            if ($canonicalFiles.ContainsKey($key) -and $candidate -cne $canonicalFiles[$key]) {
                $mismatches.Add([pscustomobject]@{
                    source = Get-RelativePath $file.FullName
                    include = $includePath
                    actual = $canonicalFiles[$key]
                })
            }
        }
    }

    return $mismatches.ToArray()
}

function Get-StaticRiskMetrics {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo[]]$SourceFiles
    )

    $cppFiles = @($SourceFiles | Where-Object { $_.Extension -in ".h", ".cpp" })
    $coreFiles = @($cppFiles | Where-Object {
        $_.FullName.StartsWith((Join-Path $engineRoot "Core") + "\", [System.StringComparison]::OrdinalIgnoreCase)
    })
    $shaderFiles = @($SourceFiles | Where-Object { $_.Extension -in ".hlsl", ".hlsli" })

    $coreEditorIncludes = New-Object System.Collections.Generic.List[string]
    $coreGuiFiles = New-Object System.Collections.Generic.List[string]
    $typeNameHashFiles = New-Object System.Collections.Generic.List[string]
    $legacyMarkerFiles = New-Object System.Collections.Generic.List[string]

    foreach ($file in $coreFiles) {
        $text = [System.IO.File]::ReadAllText($file.FullName)
        if ($text -match '#include\s*[<"]Engine/Editor/') {
            $coreEditorIncludes.Add((Get-RelativePath $file.FullName))
        }
        if ($text -match 'ImGui::|MyGUI::|\bImVec[24]\b|\bImTextureID\b') {
            $coreGuiFiles.Add((Get-RelativePath $file.FullName))
        }
        if ($text -match 'typeid\s*\([^)]*\)\.name\s*\(\)|EntityToTypeHash') {
            $typeNameHashFiles.Add((Get-RelativePath $file.FullName))
        }
        if ($text -match 'FormerlySerializedAs|FormerlyKnown|legacy|Legacy|旧形式|旧互換|fallback|Fallback') {
            $legacyMarkerFiles.Add((Get-RelativePath $file.FullName))
        }
    }

    return [pscustomobject]@{
        coreEditorIncludeFiles = $coreEditorIncludes.Count
        coreEditorIncludeDetails = @($coreEditorIncludes | Sort-Object -Unique)
        coreGuiFiles = $coreGuiFiles.Count
        coreGuiDetails = @($coreGuiFiles | Sort-Object -Unique)
        typeNameHashFiles = $typeNameHashFiles.Count
        typeNameHashDetails = @($typeNameHashFiles | Sort-Object -Unique)
        legacyMarkerFiles = $legacyMarkerFiles.Count
        legacyMarkerDetails = @($legacyMarkerFiles | Sort-Object -Unique)
        cppIncludeCaseMismatches = @(Get-CppIncludeCaseMismatches $cppFiles)
        shaderIncludeCaseMismatches = @(Get-ShaderIncludeCaseMismatches $shaderFiles)
    }
}

function Get-AssetRoots {

    $roots = New-Object System.Collections.Generic.List[string]
    foreach ($path in @(
        (Join-Path $engineRoot "Assets"),
        (Join-Path $repoRoot "Project\Sandbox\GameAssets")
    )) {
        if (Test-Path -LiteralPath $path) {
            $roots.Add([System.IO.Path]::GetFullPath($path))
        }
    }

    $gameProjectsRoot = Join-Path $repoRoot "Project\GameProjects"
    if (Test-Path -LiteralPath $gameProjectsRoot) {
        foreach ($container in Get-ChildItem -LiteralPath $gameProjectsRoot -Directory) {
            $gameAssets = Join-Path $container.FullName (Join-Path $container.Name "GameAssets")
            if (Test-Path -LiteralPath $gameAssets) {
                $roots.Add([System.IO.Path]::GetFullPath($gameAssets))
            }
        }
    }

    return $roots.ToArray()
}

function Get-JsonMetrics {

    $jsonFiles = @(
        Get-AssetRoots |
            ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File -Filter "*.json" }
    )
    $errors = New-Object System.Collections.Generic.List[object]

    foreach ($file in $jsonFiles) {
        try {
            $null = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8) |
                ConvertFrom-Json
        }
        catch {
            $errors.Add([pscustomobject]@{
                path = Get-RelativePath $file.FullName
                message = $_.Exception.Message
            })
        }
    }

    return [pscustomobject]@{
        files = $jsonFiles.Count
        parseErrors = $errors.Count
        errorDetails = $errors.ToArray()
    }
}

function Get-ShaderManifestEntries {

    $guidMap = @{}
    foreach ($metaFile in Get-ChildItem -LiteralPath $shaderRoot -Recurse -File -Filter "*.hlsl.meta") {
        try {
            $meta = [System.IO.File]::ReadAllText($metaFile.FullName, [System.Text.Encoding]::UTF8) |
                ConvertFrom-Json
            $sourcePath = $metaFile.FullName.Substring(0, $metaFile.FullName.Length - ".meta".Length)
            $guidMap[[string]$meta.guid] = $sourcePath
        }
        catch {
        }
    }

    $entries = New-Object System.Collections.Generic.List[object]
    $errors = New-Object System.Collections.Generic.List[object]
    foreach ($manifestFile in Get-ChildItem -LiteralPath $shaderRoot -Recurse -File -Filter "*.shader.json") {
        try {
            $manifest = [System.IO.File]::ReadAllText($manifestFile.FullName, [System.Text.Encoding]::UTF8) |
                ConvertFrom-Json
            foreach ($stage in @($manifest.stages)) {
                $guid = [string]$stage.file
                if (-not $guidMap.ContainsKey($guid)) {
                    $errors.Add([pscustomobject]@{
                        manifest = Get-RelativePath $manifestFile.FullName
                        guid = $guid
                        message = "Shader GUIDが見つかりません"
                    })
                    continue
                }

                $entries.Add([pscustomobject]@{
                    manifest = Get-RelativePath $manifestFile.FullName
                    path = $guidMap[$guid]
                    relativePath = Get-RelativePath $guidMap[$guid]
                    stage = [string]$stage.stage
                    entry = [string]$stage.entry
                    profile = [string]$stage.profile
                })
            }
        }
        catch {
            $errors.Add([pscustomobject]@{
                manifest = Get-RelativePath $manifestFile.FullName
                guid = ""
                message = $_.Exception.Message
            })
        }
    }

    $uniqueEntries = @(
        $entries |
            Sort-Object path, entry, profile -Unique
    )
    return [pscustomobject]@{
        entries = $uniqueEntries
        errors = $errors.ToArray()
    }
}

function Get-DxcPath {

    $command = Get-Command "dxc.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path -LiteralPath $sdkRoot) {
        $found = Get-ChildItem -LiteralPath $sdkRoot -Recurse -File -Filter "dxc.exe" -ErrorAction SilentlyContinue |
            Where-Object FullName -match "\\x64\\dxc\.exe$" |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    }

    throw "dxc.exeが見つかりません"
}

function Invoke-ShaderCompile {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Entries
    )

    $dxc = Get-DxcPath
    $outputRoot = [System.IO.Path]::GetFullPath((Join-Path $generatedRoot "ShaderCheck"))
    $expectedPrefix = [System.IO.Path]::GetFullPath($generatedRoot).TrimEnd("\") + "\"
    if (-not $outputRoot.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Shader出力先がGenerated外です"
    }
    if (Test-Path -LiteralPath $outputRoot) {
        Remove-Item -LiteralPath $outputRoot -Recurse -Force
    }
    [System.IO.Directory]::CreateDirectory($outputRoot) | Out-Null

    $failures = New-Object System.Collections.Generic.List[object]
    $compiled = 0
    foreach ($entry in $Entries) {
        $key = "$($entry.relativePath)|$($entry.entry)|$($entry.profile)"
        $keyBytes = [System.Text.Encoding]::UTF8.GetBytes($key)
        $hash = [System.Convert]::ToHexString(
            [System.Security.Cryptography.SHA256]::HashData($keyBytes)
        ).Substring(0, 16)
        $outputPath = Join-Path $outputRoot ($hash + ".dxil")
        $arguments = @(
            "-nologo",
            "-E", $entry.entry,
            "-T", $entry.profile,
            "-Zi",
            "-Qembed_debug",
            "-Zpr",
            "-I", ([System.IO.Path]::GetDirectoryName($entry.path)),
            "-I", $shaderRoot,
            "-Fo", $outputPath,
            $entry.path
        )

        $messages = & $dxc @arguments 2>&1
        if ($LASTEXITCODE -ne 0) {
            $failures.Add([pscustomobject]@{
                path = $entry.relativePath
                entry = $entry.entry
                profile = $entry.profile
                message = ($messages | Out-String).Trim()
            })
            continue
        }
        $compiled++
    }

    return [pscustomobject]@{
        compiler = $dxc
        compiled = $compiled
        failures = $failures.Count
        failureDetails = $failures.ToArray()
    }
}

function Get-MSBuildPath {

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $found = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($found -and (Test-Path -LiteralPath $found)) {
            return $found
        }
    }

    $command = Get-Command "MSBuild.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "MSBuild.exeが見つかりません"
}

function Invoke-BuildChecks {

    $msbuild = Get-MSBuildPath
    $solution = Join-Path $repoRoot "Project\NEMEngine.slnx"
    if (-not (Test-Path -LiteralPath $solution)) {
        throw "NEMEngine.slnxが見つかりません"
    }

    $results = New-Object System.Collections.Generic.List[object]
    foreach ($configuration in $Configurations) {
        Write-Host "=== Build: $Target $configuration ==="
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        $messages = & $msbuild $solution "/t:$Target" "/p:Configuration=$configuration" "/p:Platform=x64" `
            "/m:1" "/nodeReuse:false" "/v:minimal" 2>&1
        $exitCode = $LASTEXITCODE
        $messages | ForEach-Object { Write-Host $_ }
        $watch.Stop()

        $outputDirectory = Join-Path $repoRoot "Generated\Output\$configuration\$Target"
        $binaryBytes = 0L
        if (Test-Path -LiteralPath $outputDirectory) {
            $binaryBytes = (
                Get-ChildItem -LiteralPath $outputDirectory -Recurse -File |
                    Measure-Object Length -Sum
            ).Sum
        }
        $results.Add([pscustomobject]@{
            configuration = $configuration
            exitCode = $exitCode
            elapsedMilliseconds = [Math]::Round($watch.Elapsed.TotalMilliseconds, 1)
            outputBytes = $binaryBytes
        })
    }

    return [pscustomobject]@{
        msbuild = $msbuild
        failures = @($results | Where-Object exitCode -ne 0).Count
        results = $results.ToArray()
    }
}

function Invoke-ManagedChecks {

    $script = Join-Path $PSScriptRoot "ManagedScriptingStress.ps1"
    if (-not (Test-Path -LiteralPath $script)) {
        throw "ManagedScriptingStress.ps1が見つかりません"
    }

    $pwsh = (Get-Command "pwsh.exe" -ErrorAction Stop).Source
    Write-Host "=== Managed scripting regression ==="
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $messages = & $pwsh -NoProfile -File $script -Mode quick 2>&1
    $exitCode = $LASTEXITCODE
    $messages | ForEach-Object { Write-Host $_ }
    $watch.Stop()

    return [pscustomobject]@{
        exitCode = $exitCode
        failures = if ($exitCode -eq 0) { 0 } else { 1 }
        elapsedMilliseconds = [Math]::Round($watch.Elapsed.TotalMilliseconds, 1)
    }
}

function Invoke-AbiCheck {

    $generator = Join-Path $repoRoot "Project\Tools\NEM.ComponentBindingGen\NEM.ComponentBindingGen.csproj"
    $bindings = Join-Path $engineRoot "Core\Scripting\Managed\Bindings"
    $nativeOutput = Join-Path $engineRoot "Core\Scripting\Managed\Generated"
    $managedOutput = Join-Path $engineRoot "Managed\NEM.ScriptCore\Generated"

    $messages = & dotnet run --project $generator -c Release -- --verify `
        --manifest (Join-Path $bindings "ComponentManifest.json") `
        --abi (Join-Path $bindings "ManagedNativeApi.json") `
        --out-native-dir $nativeOutput `
        --out-cs-dir $managedOutput 2>&1
    $exitCode = $LASTEXITCODE
    $messages | ForEach-Object { Write-Host $_ }
    return [pscustomobject]@{
        failures = if ($exitCode -eq 0) { 0 } else { 1 }
    }
}

function Get-ComparableMetrics {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Report
    )

    return [ordered]@{
        jsonParseErrors = [int]$Report.assets.parseErrors
        shaderReferenceErrors = [int]$Report.shaders.referenceErrors
        abiFailures = [int]$Report.abi.failures
        cppIncludeCaseMismatches = @($Report.risks.cppIncludeCaseMismatches).Count
        shaderIncludeCaseMismatches = @($Report.risks.shaderIncludeCaseMismatches).Count
        coreEditorIncludeFiles = [int]$Report.risks.coreEditorIncludeFiles
        coreGuiFiles = [int]$Report.risks.coreGuiFiles
        shaderCompileFailures = if ($null -eq $Report.shaderCompile) { 0 } else { [int]$Report.shaderCompile.failures }
        managedFailures = if ($null -eq $Report.managed) { 0 } else { [int]$Report.managed.failures }
        buildFailures = if ($null -eq $Report.build) { 0 } else { [int]$Report.build.failures }
    }
}

function New-Baseline {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Report
    )

    $metrics = Get-ComparableMetrics $Report
    return [ordered]@{
        schemaVersion = 1
        capturedUtc = [DateTime]::UtcNow.ToString("o")
        sourceSnapshot = $Report.source
        limits = [ordered]@{
            jsonParseErrors = 0
            shaderReferenceErrors = 0
            abiFailures = 0
            cppIncludeCaseMismatches = $metrics.cppIncludeCaseMismatches
            shaderIncludeCaseMismatches = $metrics.shaderIncludeCaseMismatches
            coreEditorIncludeFiles = $metrics.coreEditorIncludeFiles
            coreGuiFiles = $metrics.coreGuiFiles
            shaderCompileFailures = 0
            managedFailures = 0
            buildFailures = 0
        }
    }
}

function Test-Baseline {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Report,
        [Parameter(Mandatory = $true)]
        [object]$Baseline
    )

    $metrics = Get-ComparableMetrics $Report
    $failures = New-Object System.Collections.Generic.List[object]
    foreach ($property in $Baseline.limits.PSObject.Properties) {
        $name = $property.Name
        $limit = [int]$property.Value
        $actual = [int]$metrics[$name]
        if ($actual -gt $limit) {
            $failures.Add([pscustomobject]@{
                metric = $name
                actual = $actual
                limit = $limit
            })
        }
    }
    return $failures.ToArray()
}

Write-Host "=== Refactoring baseline: Mode=$Mode Scope=$Scope ==="
$sourceFiles = Get-SourceFiles
$sourceMetrics = Get-SourceMetrics $sourceFiles
$riskMetrics = Get-StaticRiskMetrics $sourceFiles
$jsonMetrics = Get-JsonMetrics
$shaderManifest = Get-ShaderManifestEntries
$abi = Invoke-AbiCheck

$shaderCompile = $null
$managed = $null
$build = $null
if ($Scope -eq "Full") {
    $shaderCompile = Invoke-ShaderCompile $shaderManifest.entries
    $managed = Invoke-ManagedChecks
    $build = Invoke-BuildChecks
}

$report = [ordered]@{
    schemaVersion = 1
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    mode = $Mode
    scope = $Scope
    source = $sourceMetrics
    risks = $riskMetrics
    assets = $jsonMetrics
    shaders = [ordered]@{
        manifests = @(Get-ChildItem -LiteralPath $shaderRoot -Recurse -File -Filter "*.shader.json").Count
        entries = @($shaderManifest.entries).Count
        referenceErrors = @($shaderManifest.errors).Count
        referenceErrorDetails = @($shaderManifest.errors)
    }
    abi = $abi
    shaderCompile = $shaderCompile
    managed = $managed
    build = $build
}

Write-Utf8Json -Path $ReportPath -Value $report
Write-Host "Report: $ReportPath"

if ($Mode -eq "Capture") {
    $baseline = New-Baseline $report
    Write-Utf8Json -Path $BaselinePath -Value $baseline
    Write-Host "Baseline: $BaselinePath"
    exit 0
}

if (-not (Test-Path -LiteralPath $BaselinePath)) {
    throw "Baselineが見つかりません: $BaselinePath"
}

$baseline = Get-Content -LiteralPath $BaselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
$failures = @(Test-Baseline -Report $report -Baseline $baseline)
foreach ($failure in $failures) {
    Write-Host "[FAIL] $($failure.metric): actual=$($failure.actual) limit=$($failure.limit)" -ForegroundColor Red
}

if ($failures.Count -gt 0) {
    Write-Host "=== Refactoring baseline failed: $($failures.Count) ===" -ForegroundColor Red
    exit 1
}

Write-Host "=== Refactoring baseline passed ==="
exit 0
