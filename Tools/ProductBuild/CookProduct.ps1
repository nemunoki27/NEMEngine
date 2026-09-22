function Invoke-ProductCook {
    param([string]$stageDirectory, [string]$buildToolExecutable, [string]$ManifestPath)

    $cookedShaderRoot = Join-Path $stageDirectory "Cooked\Shaders"
    New-Item -ItemType Directory -Path $cookedShaderRoot -Force | Out-Null
    Write-Output "シェーダーのCookを開始します"
    & $buildToolExecutable --cook-shaders $ManifestPath $cookedShaderRoot
    if ($LASTEXITCODE -ne 0) {
        throw "シェーダーのCookに失敗しました。直前の診断ログを確認してください"
    }

    # MaterialはCook済みPassだけを参照し、製品AssetDatabaseへGraph依存を残さない
    $detachedMaterialCount = 0
    Get-ChildItem -LiteralPath $stageDirectory -Recurse -File -Filter "*.material.json" |
        ForEach-Object {
            $material = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($material.PSObject.Properties.Name -contains "shaderGraph") {
                $material.PSObject.Properties.Remove("shaderGraph")
                Write-Utf8Json -Path $_.FullName -Value $material
                ++$detachedMaterialCount
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
            $lower.EndsWith(".shadergraph.json") -or
            $lower.EndsWith(".shadergraph.json.meta")
        } |
        Remove-Item -Force

}
