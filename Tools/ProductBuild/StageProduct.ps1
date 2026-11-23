function Test-ProductManagedScripts {
    param([string]$assemblies, [string]$manifestPath, [string]$toolchainRoot)
    $dotnet = if ($toolchainRoot) { Join-Path $toolchainRoot 'dotnet.exe' } else { 'dotnet' }
    $checker = Join-Path $PSScriptRoot 'InspectProductScripts.cs'
    try {
        $result = & $dotnet run --file $checker -- $assemblies $manifestPath 2>&1
        if ($LASTEXITCODE -eq 0 -and @($result) -ccontains 'NEM_NATIVE_ONLY') { return $false }
        if ($LASTEXITCODE -ne 0) { Write-Warning "C#の使用判定ができないため.NETを同梱します: $result" }
    } catch { Write-Warning "C#の使用判定ができないため.NETを同梱します: $_" }
    return $true
}

function Copy-ProductFiles {
    param($manifest, [string]$sourceRuntime, [string]$runtimeExecutable, [string]$stageDirectory, [string]$executableName, [string]$managedSource, [bool]$managedScripts = $true)

    $runtimeSource = Join-Path $sourceRuntime $runtimeExecutable
    if (-not (Test-Path -LiteralPath $runtimeSource)) {
        throw "ゲームの実行ファイルが見つかりません: $runtimeSource"
    }
    Copy-Item -LiteralPath $runtimeSource `
        -Destination (Join-Path $stageDirectory $executableName) -Force

    $runtimeManifestName = "nem.runtime-dependencies.json"
    $runtimeManifestPath = Join-Path $sourceRuntime $runtimeManifestName
    if (-not (Test-Path -LiteralPath $runtimeManifestPath)) {
        throw "ランタイム依存関係のマニフェストが見つかりません: $runtimeManifestPath"
    }

    $runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ([int]$runtimeManifest.schemaVersion -ne 1) {
        throw "ランタイム依存関係のマニフェスト形式に対応していません"
    }

    $runtimeFiles = @($runtimeManifest.files | ForEach-Object { [string]$_ })
    if ($runtimeFiles.Count -eq 0 -or $runtimeFiles -notcontains "NEMRuntime.dll") {
        throw "ランタイム依存関係のマニフェストにNEMRuntime.dllが含まれていません"
    }

    $productRuntimeFiles = @($runtimeFiles | Where-Object {
        $_ -ne "dxcompiler.dll" -and $_ -ne "dxil.dll" -and ($managedScripts -or $_ -ne "nethost.dll")
    })
    foreach ($runtimeFile in $productRuntimeFiles) {
        $source = Get-ChildPath -Root $sourceRuntime -Relative $runtimeFile
        if (-not (Test-Path -LiteralPath $source)) {
            throw "ランタイムファイルが見つかりません: $source"
        }
        $destination = Get-ChildPath -Root $stageDirectory -Relative $runtimeFile
        $destinationDirectory = [System.IO.Path]::GetDirectoryName($destination)
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    Write-Utf8Json -Path (Join-Path $stageDirectory $runtimeManifestName) -Value ([ordered]@{
        schemaVersion = 1
        configuration = "Release"
        files = $productRuntimeFiles
    })

    if ($managedScripts) {
        if (-not (Test-Path -LiteralPath $managedSource)) { throw "C#ランタイムが見つかりません: $managedSource" }
        $managed = Join-Path $stageDirectory 'Managed'
        New-Item -ItemType Directory -Path $managed -Force | Out-Null
        foreach ($name in 'NEM.ScriptCore.dll','NEM.ScriptCore.deps.json','NEM.ScriptCore.runtimeconfig.json') {
            Copy-Item -LiteralPath (Join-Path $managedSource $name) -Destination $managed -Force
        }
    }
    $total = $manifest.files.Count
    $copied = 0
    $percent = 45
    Write-BuildProgress $percent "アセットをコピー・検証しています（0 / $total ファイル）"
    foreach ($entry in $manifest.files) {
        $source = [System.IO.Path]::GetFullPath([string]$entry.source)
        $relative = [string]$entry.destination
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "アセットファイルが見つかりません: $source"
        }
        Assert-FileHash -Path $source -ExpectedSize ([long]$entry.size) -ExpectedSha256 ([string]$entry.sha256)
        $destination = Get-ChildPath -Root $stageDirectory -Relative $relative
        New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        # Cookする作業データへ共有元の保護属性を持ち込まない
        Set-ItemProperty -LiteralPath $destination -Name IsReadOnly -Value $false
        Assert-FileHash -Path $destination -ExpectedSize ([long]$entry.size) -ExpectedSha256 ([string]$entry.sha256)
        ++$copied
        $next = 45 + [math]::Floor(24 * $copied / $total)
        if ($percent -lt $next) {
            $percent = $next
            Write-BuildProgress $percent "アセットをコピー・検証しています（$copied / $total ファイル）"
        }
    }

}

function Copy-ProductGameAssemblies {
    param([string]$source, [string]$destination)
    $file = Join-Path $source 'GameScripts.deps.json'
    $deps = Get-Content -LiteralPath $file -Raw -Encoding UTF8 | ConvertFrom-Json
    Copy-Item -LiteralPath $file -Destination $destination -Force
    $target = $deps.targets.PSObject.Properties[[string]$deps.runtimeTarget.name].Value
    foreach ($library in $target.PSObject.Properties) {
        foreach ($category in 'runtime','native','resources','runtimeTargets') {
            $group = $library.Value.PSObject.Properties[$category]
            if (!$group) { continue }
            foreach ($item in $group.Value.PSObject.Properties) {
                $name = [IO.Path]::GetFileName($item.Name)
                if ($name -eq '_._' -or $name -eq 'NEM.ScriptCore.dll') { continue }
                if ($category -eq 'runtimeTargets' -and $item.Value.rid -notmatch '^win([0-9.]+)?(-x64)?$') { continue }
                $relative = if ($category -eq 'resources') { [string]$item.Value.locale + '/' + $name } else { $name }
                $path = Get-ChildPath $source $item.Name
                if (Test-Path -LiteralPath $path -PathType Leaf) { $relative = $item.Name }
                else { $path = Get-ChildPath $source $relative }
                if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "C# ゲームの実行時依存ファイルがありません: $($item.Name)" }
                $output = Get-ChildPath $destination $relative
                New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null
                Copy-Item -LiteralPath $path -Destination $output -Force
            }
        }
    }
}