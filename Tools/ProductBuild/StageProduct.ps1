function Copy-ProductFiles {
    param($manifest, [string]$sourceRuntime, [string]$runtimeExecutable, [string]$stageDirectory, [string]$executableName, [string]$managedSource)

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
        $_ -ne "dxcompiler.dll" -and $_ -ne "dxil.dll"
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

    if (-not (Test-Path -LiteralPath $managedSource)) {
        throw "C#ランタイムが見つかりません: $managedSource"
    }
    $managedSourceFull = [System.IO.Path]::GetFullPath($managedSource).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    Get-ChildItem -LiteralPath $managedSource -Recurse -File |
        Where-Object { $_.Extension -eq ".dll" -or $_.Extension -eq ".json" } |
        ForEach-Object {
            $relative = $_.FullName.Substring($managedSourceFull.Length).TrimStart(
                [System.IO.Path]::DirectorySeparatorChar,
                [System.IO.Path]::AltDirectorySeparatorChar)
            $destination = Get-ChildPath -Root (Join-Path $stageDirectory "Managed") -Relative $relative
            New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
        }

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
        Assert-FileHash -Path $destination -ExpectedSize ([long]$entry.size) -ExpectedSha256 ([string]$entry.sha256)
    }

}
