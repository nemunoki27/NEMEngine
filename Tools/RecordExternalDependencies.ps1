param([string]$OutputPath = '', [switch]$Verify)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$externalRoot = Join-Path $engineRoot 'Project\Externals'
if (-not $OutputPath) { $OutputPath = Join-Path $externalRoot 'dependencies.lock.json' }
$libraries = @(
    @{ name = 'imgui'; versionFile = 'imgui.h'; pattern = '^#define IMGUI_VERSION\s+"([^"]+)"' },
    @{ name = 'meshoptimizer'; versionFile = 'include\meshoptimizer.h'; pattern = 'meshoptimizer - version ([0-9.]+)' },
    @{ name = 'DirectXTex'; versionFile = 'DirectXTex.h'; pattern = '' },
    @{ name = 'magic_enum'; versionFile = 'magic_enum.hpp'; prefix = 'MAGIC_ENUM_VERSION_' },
    @{ name = 'nlohmann'; versionFile = 'json.hpp'; prefix = 'NLOHMANN_JSON_VERSION_' },
    @{ name = 'spdlog'; versionFile = 'spdlog\version.h'; prefix = 'SPDLOG_VER_' },
    @{ name = 'assimp'; versionFile = 'CMakeLists.txt'; pattern = 'PROJECT\(Assimp VERSION ([0-9.]+)' },
    @{ name = 'imgui-node-editor'; versionFile = 'imgui_node_editor.h'; pattern = '' },
    @{ name = 'msdf-atlas-gen'; versionFile = 'vcpkg.json'; pattern = '"version"\s*:\s*"([^"]+)"' },
    @{ name = 'freetype'; versionFile = 'include\freetype\freetype.h'; prefix = 'FREETYPE_' },
    @{ name = 'DirectX12'; versionFile = ''; pattern = '' },
    @{ name = 'dotnet-hosting'; versionFile = ''; pattern = '' },
    @{ name = 'WinPixEventRuntime'; versionFile = ''; pattern = '' }
)
$records = @()
foreach ($library in $libraries) {
    $root = Join-Path $externalRoot $library.name
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "外部依存がありません: $root" }
    $version = $null
    if ($library.versionFile) {
        $versionPath = Join-Path $root $library.versionFile
        if (Test-Path -LiteralPath $versionPath -PathType Leaf) {
            $text = [IO.File]::ReadAllText($versionPath)
            if ($library.ContainsKey('prefix')) {
                $parts = @()
                foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
                    $match = [regex]::Match($text, '(?m)^#define\s+' + $library.prefix + $part + '\s+(\d+)')
                    if ($match.Success) { $parts += $match.Groups[1].Value }
                }
                if ($parts.Count -eq 3) { $version = $parts -join '.' }
            } elseif ($library.pattern) {
                $match = [regex]::Match($text, $library.pattern, [Text.RegularExpressions.RegexOptions]::Multiline)
                if ($match.Success) { $version = $match.Groups[1].Value }
            }
        }
    }
    $files = @()
    $paths = @(& rg --files --hidden $root | Sort-Object)
    if ($LASTEXITCODE -notin @(0, 1)) { throw "外部依存を列挙できません: $root" }
    foreach ($path in $paths) {
        $relative = $path.Substring($root.Length).TrimStart('\', '/').Replace('\', '/')
        if ($relative -match '(^|/)(\.git|bin|obj|Generated|Intermediate|Output|Outputs|build|\.vs)(/|$)') { continue }
        $file = Get-Item -LiteralPath $path -Force
        $files += [ordered]@{ path = $relative; size = [long]$file.Length; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    $hashText = ($files | ForEach-Object { $_.path + "`0" + $_.sha256 + "`0" + $_.size }) -join "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $treeHash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($hashText))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $commit = $null
    $changes = @()
    if (Test-Path -LiteralPath (Join-Path $root '.git')) {
        $commit = (& git -C $root rev-parse HEAD | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) { throw "外部依存のHEADを取得できません: $root" }
        $changes = @(& git -C $root diff --name-only HEAD --)
        if ($LASTEXITCODE -ne 0) { throw "外部依存の変更を取得できません: $root" }
    }
    $licenseFiles = @($files | Where-Object { $_.path -match '(^|/)(LICENSE[^/]*|COPYING[^/]*)$' } | ForEach-Object { $_.path })
    $records += [ordered]@{
        name = $library.name; version = $version; versionEvidence = $library.versionFile
        upstreamCommit = $commit; upstreamComparison = $(if ($commit) { 'submodule' } else { 'unknown' })
        knownLocalChanges = $changes; patches = @(); licenseFiles = $licenseFiles
        treeSHA256 = $treeHash; files = $files
    }
}
$record = [ordered]@{ schemaVersion = 1; hashScope = 'rg-visible files excluding build outputs'; libraries = $records }
$json = $record | ConvertTo-Json -Depth 8
if ($Verify) {
    $baseline = Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json
    foreach ($library in $records) {
        $old = @($baseline.libraries | Where-Object { $_.name -eq $library.name })
        if ($old.Count -ne 1 -or $old[0].treeSHA256 -ne $library.treeSHA256) { throw "外部依存の現物hashが変更されています: $($library.name)" }
    }
    Write-Output '外部依存の現物hashが一致しました'
} else {
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath), $json + "`n", [Text.UTF8Encoding]::new($false))
    Write-Output "外部依存の現在版と現物hashを記録しました: $OutputPath"
}
