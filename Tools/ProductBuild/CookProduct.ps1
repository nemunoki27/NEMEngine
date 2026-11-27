function Invoke-ProductCook {
    param([string]$stageDirectory, [string]$buildToolExecutable, [string]$ManifestPath)

    $cookedShaderRoot = Join-Path $stageDirectory "Cooked\Shaders"
    New-Item -ItemType Directory -Path $cookedShaderRoot -Force | Out-Null
    Write-BuildProgress 72 "シェーダーの入力ファイルを検証しています"
    $cookManifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $cookManifest.gameRoot = [IO.Path]::GetFullPath($stageDirectory)
    foreach ($entry in $cookManifest.files) {
        $entry.source = Get-ChildPath -Root $stageDirectory -Relative ([string]$entry.destination)
        Assert-FileHash ([string]$entry.source) ([long]$entry.size) ([string]$entry.sha256)
        if ($entry.destination -like '*.material' -or $entry.destination -like '*.material.json') {
            $material = Get-Content -LiteralPath $entry.source -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($material.PSObject.Properties.Name -contains 'passes') {
                $passes = @($material.passes | Where-Object { $_.passKind -ne 'EditorPicking' })
                if ($passes.Count -ne @($material.passes).Count) {
                    $material.passes = $passes
                    Write-Utf8Json $entry.source $material
                    $entry.size = (Get-Item -LiteralPath $entry.source).Length
                    $entry.sha256 = (Get-FileHash -LiteralPath $entry.source -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
        }
    }
    $snapshotPath = Get-ChildPath -Root $stageDirectory -Relative '.nemCookInputs.json'
    Write-Utf8Json $snapshotPath $cookManifest
    $previousPortable = $env:NEMENGINE_PORTABLE
    $previousEngineRoot = $env:NEMENGINE_ROOT
    $previousToolchainRoot = $env:NEMENGINE_TOOLCHAIN_ROOT
    try {
        $env:NEMENGINE_PORTABLE = '1'
        $env:NEMENGINE_ROOT = $stageDirectory
        $env:NEMENGINE_TOOLCHAIN_ROOT = ''
        Write-BuildProgress 76 "製品用シェーダーをコンパイルしています"
        $cookError = ""
        & $buildToolExecutable --cook-shaders $snapshotPath $cookedShaderRoot | ForEach-Object {
            if (([string]$_).StartsWith("[シェーダーCook] ")) { $cookError = $_ }
            Write-Output $_
        }
        if ($LASTEXITCODE -ne 0) {
            if ($cookError) { throw $cookError }
            throw "シェーダーのCookに失敗しました（終了コード $LASTEXITCODE）"
        }
        Write-BuildProgress 90 "シェーダーの入力と出力を確認しています"
        # 処理中に入力が変わった結果は公開しない
        foreach ($entry in $cookManifest.files) {
            Assert-FileHash ([string]$entry.source) ([long]$entry.size) ([string]$entry.sha256)
        }
    } finally {
        $env:NEMENGINE_PORTABLE = $previousPortable
        $env:NEMENGINE_ROOT = $previousEngineRoot
        $env:NEMENGINE_TOOLCHAIN_ROOT = $previousToolchainRoot
        Remove-Item -LiteralPath $snapshotPath -Force
    }

    Write-BuildProgress 93 "製品に含めるマテリアルとファイルを整理しています"
    # Cookの作業データは製品へ含めない
    foreach ($directory in @('Library', 'Saved', 'UserSettings')) {
        $path = Get-ChildPath -Root $stageDirectory -Relative $directory
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
    }

    # MaterialはCook済みPassだけを参照し、製品AssetDatabaseへGraph依存を残さない
    $detachedMaterialCount = 0
    Get-ChildItem -LiteralPath $stageDirectory -Recurse -File |
        Where-Object { $_.Name -like '*.material' -or $_.Name -like '*.material.json' } |
        ForEach-Object {
            $material = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($material.PSObject.Properties.Name -contains "shaderGraph") {
                $material.PSObject.Properties.Remove("shaderGraph")
                $material.PSObject.Properties.Remove("graphOverrides")
                ++$detachedMaterialCount
                Write-Utf8Json -Path $_.FullName -Value $material
            }
        }
    Write-Output "Shader Graphのソース参照を解除しました: $detachedMaterialCount"

    # 製品はCook済みDXILのみを使用し、Graph/HLSLソースを配置しない
    Get-ChildItem -LiteralPath $stageDirectory -Recurse -File |
        Where-Object {
            $lower = $_.Name.ToLowerInvariant()
            $lower.EndsWith(".hlsl") -or
            $lower.EndsWith(".hlsli") -or
            $lower.EndsWith(".hlsl.meta") -or
            $lower.EndsWith(".hlsli.meta") -or
            $lower.EndsWith(".shadergraph") -or
            $lower.EndsWith(".shadergraph.meta") -or
            $lower.EndsWith(".shadergraph.json") -or
            $lower.EndsWith(".shadergraph.json.meta")
        } |
        Remove-Item -Force

}
