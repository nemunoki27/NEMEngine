param(
    [string]$Probe = "",
    [string]$BuildTool = "",
    [string]$GameSourceRoot = "",
    [switch]$Cook
)

# GameBuildServiceProbe.targetsをForceImportAfterCppTargetsへ指定してNEMTestsのDebug構成をビルドしておく
# 製品検証はpowershell.exe -NoProfile -Fileで実行し、Menubarの子PowerShellと環境を揃える
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding
$engineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if (-not $Probe) { $Probe = Join-Path $engineRoot "Generated\Output\Debug\GameBuildServiceProbe\GameBuildServiceProbe.exe" }
if (-not $BuildTool) { $BuildTool = Join-Path $engineRoot "Generated\Output\Release\NEMBuildTool\NEMBuildTool.exe" }
# Windows PowerShellの長いパス制限に検証用の階層を持ち込まない
$testRoot = Join-Path $engineRoot ("Generated\RT\" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
$gameRoot = Join-Path $testRoot "Game"
$settingsRoot = Join-Path $gameRoot "CustomSettings"
New-Item -ItemType Directory -Path $settingsRoot, (Join-Path $gameRoot "Engine"), (Join-Path $gameRoot "GameAssets") | Out-Null
Copy-Item -LiteralPath (Join-Path $engineRoot "Project\Engine\Assets") -Destination (Join-Path $gameRoot "Engine") -Recurse

function Write-TestJson([string]$Path, [object]$Value) {
    [System.IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 64), $OutputEncoding)
}

function Invoke-Collect([string]$Name, [string]$Expected = "") {
    $ErrorActionPreference = "Continue"
    $output = @(& $Probe $gameRoot $sceneID (Join-Path $testRoot ($Name + ".json")) --collect 2>&1)
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    $output | Set-Content -LiteralPath (Join-Path $testRoot ($Name + ".log")) -Encoding UTF8
    if ($Expected) {
        if ($code -eq 0 -or -not (($output -join "`n").Contains($Expected))) {
            throw "欠損の診断が不正です: $Name code=$code"
        }
    } elseif ($code -ne 0) {
        throw "収集に失敗しました: $Name code=$code / $testRoot"
    }
    Write-Host "[成功] $Name"
}

Write-TestJson (Join-Path $gameRoot "RuntimeProbe.nemproject") @{
    schemaVersion = 1; projectGuid = [Guid]::NewGuid().ToString("N"); name = "RuntimeProbe"
    assetsDirectory = "GameAssets"; packagesDirectory = "Packages"; projectSettingsDirectory = "CustomSettings"
}
$sceneID = [Guid]::NewGuid().ToString("N")
Write-TestJson (Join-Path $gameRoot "GameAssets\Empty.scene.json") @{
    SchemaVersion = 3; Header = @{ name = "Empty"; subScenes = @() }; Entities = @(); PrefabInstances = @()
}
Write-TestJson (Join-Path $gameRoot "GameAssets\Empty.scene.json.meta") @{
    schemaVersion = 2; guid = $sceneID; type = "Scene"; importer = "SceneImporter"; importerVersion = 1; settings = @{}
}
$collisionPath = Join-Path $settingsRoot "CollisionSettings.json"
Write-TestJson $collisionPath @{
    types = @("Default", "StageWall", "BlockSearcher", "StageBlock", "Player", "Checkpoint", "FilmSwitchPoint") |
        ForEach-Object { @{ name = $_; enabled = $true } }
    matrixRows = @(27, 17, 8, 21, 107, 16, 16)
}
[System.IO.File]::WriteAllText((Join-Path $gameRoot "GameAssets\Stage.csv"), "0,1,2`n3,4,5`n", $OutputEncoding)

Invoke-Collect "MinimumProject"
# 新規ゲームの既定Sceneにも解決不能な組込参照を残さない
$previousSceneID = $sceneID
try {
    $sampleMeta = Get-Content -LiteralPath (Join-Path $gameRoot "Engine\Assets\Scenes\Sample.scene.json.meta") -Raw |
        ConvertFrom-Json
    $sceneID = $sampleMeta.guid
    Invoke-Collect "BuiltinSampleScene"
} finally {
    $sceneID = $previousSceneID
}
$files = @((Get-Content -LiteralPath (Join-Path $testRoot "MinimumProject.json") -Raw -Encoding UTF8 | ConvertFrom-Json) | ForEach-Object { $_ })
$collisionFile = @($files | Where-Object destination -eq "ProjectSettings/CollisionSettings.json")
if ($collisionFile.Count -ne 1 -or [System.IO.Path]::GetFullPath($collisionFile[0].source) -ne $collisionPath) {
    throw "カスタム設定フォルダーから衝突設定を収集できません"
}
foreach ($suffix in @("screenSpaceOutlineDilate.material.json", "screenSpaceOutlineComposite.material.json",
    "defaultLine.material.json", "Stage.csv")) {
    if (-not @($files | Where-Object { $_.destination.EndsWith($suffix) }).Count) { throw "常時同梱ファイルがありません: $suffix" }
}
if (@($files | Where-Object { $_.destination -match '^Engine/Assets/(Textures/Editor|Rendering/Editor)/' }).Count) {
    throw "Editor専用アセットが混入しています"
}
$productSettings = Join-Path $testRoot "ProductCollisionSettings.json"
Copy-Item -LiteralPath $collisionFile[0].source -Destination $productSettings
& $Probe --collision $collisionPath $productSettings
if ($LASTEXITCODE -ne 0) { throw "配置前後でCollision Matrixが異なります" }

# 欠損検証はこの実行で作ったコピーだけを移動し、必ず元の場所へ戻す
$heldSettings = Join-Path $testRoot "HeldCollisionSettings.json"
Move-Item -LiteralPath $collisionPath -Destination $heldSettings
try {
    Invoke-Collect "AbsentOptionalSettings"
    New-Item -ItemType Directory -Path $collisionPath | Out-Null
    Invoke-Collect "InvalidSettingsFile" "CollisionSettings.json"
    Move-Item -LiteralPath $collisionPath -Destination (Join-Path $testRoot "InvalidSettingsDirectory")
} finally {
    Move-Item -LiteralPath $heldSettings -Destination $collisionPath
}
$settingsLock = [System.IO.File]::Open($collisionPath, [System.IO.FileMode]::Open,
    [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
try { Invoke-Collect "UnreadableSettings" "CollisionSettings.json" }
finally { $settingsLock.Dispose() }

$outlinePath = Join-Path $gameRoot "Engine\Assets\Rendering\Builtin\ScreenSpaceOutline\DilateHorizontal\screenSpaceOutlineDilate.material.json"
$heldOutline = Join-Path $testRoot "HeldOutline.json"
Move-Item -LiteralPath $outlinePath -Destination $heldOutline
Move-Item -LiteralPath ($outlinePath + ".meta") -Destination ($heldOutline + ".meta")
try { Invoke-Collect "MissingRequiredMaterial" "必須のアセット" }
finally {
    Move-Item -LiteralPath $heldOutline -Destination $outlinePath
    Move-Item -LiteralPath ($heldOutline + ".meta") -Destination ($outlinePath + ".meta")
}

$originalOutline = [System.IO.File]::ReadAllText($outlinePath)
$missingID = [Guid]::NewGuid().ToString("N")
try {
    $outline = $originalOutline | ConvertFrom-Json
    $outline.passes[0].pipeline = $missingID
    Write-TestJson $outlinePath $outline
    Invoke-Collect "MissingRequiredDependency" $missingID
} finally {
    [System.IO.File]::WriteAllText($outlinePath, $originalOutline, $OutputEncoding)
}
Invoke-Collect "RestoredProject"

# 日本語と既定コードページにない文字も索引登録から製品収集まで保持する
$unicodeDirectory = "GameAssets/BGM・SE/" + [char]::ConvertFromUtf32(0x1F3B5)
$unicodeFiles = @("$unicodeDirectory/音楽.txt", "$unicodeDirectory/雨.データ")
New-Item -ItemType Directory -Path (Join-Path $gameRoot $unicodeDirectory) | Out-Null
foreach ($assetPath in $unicodeFiles) {
    [System.IO.File]::WriteAllText((Join-Path $gameRoot $assetPath), "Unicode asset", $OutputEncoding)
}
Invoke-Collect "UnicodeAssets"
$unicodeResult = @((Get-Content -LiteralPath (Join-Path $testRoot "UnicodeAssets.json") -Raw -Encoding UTF8 | ConvertFrom-Json) | ForEach-Object { $_ })
foreach ($assetPath in $unicodeFiles) {
    $entry = @($unicodeResult | Where-Object destination -eq $assetPath)
    if ($entry.Count -ne 1 -or $entry[0].sha256 -ne (Get-FileHash -LiteralPath (Join-Path $gameRoot $assetPath) -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw "Unicodeアセットのパスか内容が変化しました: $assetPath"
    }
}

# 不正なUTF-8のincludeでも未処理例外ではなく収集失敗を返す
$invalidIncludePath = Join-Path $gameRoot "GameAssets/InvalidInclude.hlsl"
$invalidInclude = [byte[]]($OutputEncoding.GetBytes('#include "') + @(0xFF) + $OutputEncoding.GetBytes('"'))
[System.IO.File]::WriteAllBytes($invalidIncludePath, $invalidInclude)
try { Invoke-Collect "InvalidIncludeEncoding" "製品ファイルの収集中に例外が発生しました" }
finally { [System.IO.File]::WriteAllText($invalidIncludePath, "// restored", $OutputEncoding) }
Invoke-Collect "RecoveredAfterException"

if ($GameSourceRoot) {
    # 元ゲームは変更せずGameAssetsと設定だけを検証用プロジェクトへコピーする
    foreach ($directory in @("GameAssets", "Packages")) {
        $source = Join-Path $GameSourceRoot $directory
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $gameRoot -Recurse -Force }
    }
    $sourceSettings = Join-Path $GameSourceRoot "ProjectSettings"
    if (Test-Path -LiteralPath $sourceSettings) {
        Get-ChildItem -LiteralPath $sourceSettings | Copy-Item -Destination $settingsRoot -Recurse -Force
    }
    Invoke-Collect "GameProject"
    $files = @((Get-Content -LiteralPath (Join-Path $testRoot "GameProject.json") -Raw -Encoding UTF8 | ConvertFrom-Json) | ForEach-Object { $_ })
}

if ($Cook) {
    $manifest = @{ schemaVersion = 2; gameRoot = $gameRoot; files = $files }
    $manifestPath = Join-Path $testRoot "Cook.json"
    Write-TestJson $manifestPath $manifest
    $cookRoot = Join-Path $testRoot "Cooked"
    & $BuildTool --cook-shaders $manifestPath $cookRoot
    if ($LASTEXITCODE -ne 0) { throw "収集した製品アセットのCookに失敗しました" }
    $cooked = Get-Content -LiteralPath (Join-Path $cookRoot "ShaderCookManifest.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($shader in $cooked.shaders) {
        foreach ($stage in $shader.stages) {
            $bytecode = Join-Path $cookRoot $stage.bytecode
            if (-not (Test-Path -LiteralPath $bytecode) -or (Get-Item -LiteralPath $bytecode).Length -eq 0) {
                throw "CookしたShaderのバイトコードがありません: $bytecode"
            }
        }
    }
    foreach ($missing in @("screenSpaceOutlineDilate.material.json", "screenSpaceOutlineDilate.material.json.meta",
        "screenSpaceOutlineDilateHorizontal.pipeline.json")) {
        $manifest.files = @($files | Where-Object { -not $_.destination.EndsWith($missing) })
        Write-TestJson $manifestPath $manifest
        $ErrorActionPreference = "Continue"
        $output = @(& $BuildTool --cook-shaders $manifestPath (Join-Path $testRoot "MissingCook") 2>&1)
        $code = $LASTEXITCODE
        $ErrorActionPreference = "Stop"
        if ($code -eq 0 -or -not (($output -join "`n").Contains("製品に必須のアセット"))) { throw "Cookで必須ファイル欠損を検出できません: $missing" }
    }
    Write-Host "[成功] Cookと必須ファイル欠損検出"
}

Write-Host "[完了] 製品同梱ファイルの回帰検証: $testRoot"
