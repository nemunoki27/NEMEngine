param([string]$BuildTool = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding

$engineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if ([string]::IsNullOrWhiteSpace($BuildTool)) {
    $BuildTool = Join-Path $engineRoot "Generated\Output\Release\NEMBuildTool\NEMBuildTool.exe"
}
$BuildTool = (Resolve-Path -LiteralPath $BuildTool).Path
$testRoot = Join-Path $engineRoot ("Generated\DependencyTests\" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path (Join-Path $testRoot "GameAssets"), (Join-Path $testRoot "Engine\Assets") | Out-Null

function Write-TestJson([string]$Path, [object]$Value) {
    [System.IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 16), [System.Text.UTF8Encoding]::new($false))
}

function Write-TestAsset([string]$Name, [string]$Type, [object]$Value) {
    $path = Join-Path $testRoot ("GameAssets\" + $Name)
    Write-TestJson $path $Value
    Write-TestJson ($path + ".meta") @{
        schemaVersion = 2
        guid = [Guid]::NewGuid().ToString("N")
        type = $Type
        importer = $Type + "Importer"
        importerVersion = 1
        settings = @{}
    }
}

Write-TestJson (Join-Path $testRoot "DependencyProbe.nemproject") @{
    schemaVersion = 1
    projectGuid = [Guid]::NewGuid().ToString("N")
    name = "DependencyProbe"
    assetsDirectory = "GameAssets"
    packagesDirectory = "Packages"
    projectSettingsDirectory = "ProjectSettings"
}
$sourceID = [Guid]::NewGuid().ToString("N")
$pickingID = [Guid]::NewGuid().ToString("N")
$pipelineID = [Guid]::NewGuid().ToString("N")
$textureID = [Guid]::NewGuid().ToString("N")
Write-TestAsset "source.shader.json" "Shader" @{
    sourceShader = $sourceID
    stages = @(@{ stage = "PS"; file = $sourceID; entry = "main"; profile = "ps_6_0" })
}
Write-TestAsset "picking.material.json" "Material" @{
    passes = @(@{ passKind = "EditorPicking"; pipeline = $pickingID })
}
Write-TestAsset "runtime.material.json" "Material" @{
    passes = @(@{ passKind = "Opaque"; pipeline = $pipelineID })
    baseColorTexture = $textureID
}

foreach ($mode in @("Source", "Product")) {
    if ($mode -eq "Product") {
        Write-TestJson (Join-Path $testRoot ".nemBuildManifest.json") @{ schemaVersion = 2 }
    }
    # 参照欠損はこの検証で意図しているため終了コードと診断内容を検査する
    $ErrorActionPreference = "Continue"
    $output = @(& $BuildTool --validate-project $testRoot 2>&1)
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    $log = $output -join "`n"
    $output | Set-Content -LiteralPath (Join-Path $testRoot ($mode + ".log")) -Encoding UTF8
    if ($code -ne 4) { throw "参照欠損の終了コードが不正です: $mode code=$code" }
    foreach ($required in @($pipelineID, $textureID)) {
        if (-not $log.Contains("参照=$required")) { throw "製品に必要な依存先の欠損を検出できません: $mode $required" }
    }
    foreach ($sourceOnly in @($sourceID, $pickingID)) {
        if ($log.Contains("参照=$sourceOnly") -ne ($mode -eq "Source")) {
            throw "開発用依存先の判定が不正です: $mode $sourceOnly"
        }
    }
    Write-Host "[成功] $mode の参照検証"
}
Write-Host "[完了] 開発用依存先の除外と製品用依存先の欠損検出を確認しました: $testRoot"
