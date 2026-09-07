param(
    [string]$SdkRoot = "",
    [switch]$IncludeSourceBuild,
    [string]$GameSourceRoot = "",
    [string]$StartupScene = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding

$engineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    $SdkRoot = Join-Path $engineRoot "Generated\SDK"
}
$SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
$buildTool = Join-Path $SdkRoot "Tools\NEMBuildTool\NEMBuildTool.exe"
$buildScript = Join-Path $SdkRoot "Tools\BuildGame.ps1"
foreach ($required in @($buildTool, $buildScript)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "SDKの検証に必要なファイルが見つかりません: $required"
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $SdkRoot "Managed\Tools\NEM.GameScripts.targets") -PathType Leaf)) {
    throw "C#ゲームスクリプトの配置設定がSDKにありません"
}
foreach ($configuration in @("Debug", "Develop", "Release")) {
    if (-not (Test-Path -LiteralPath (Join-Path $SdkRoot "Editor\$configuration\NEMEditor.exe"))) {
        throw "ゲーム生成の検証には3構成を同梱したSDKが必要です: $configuration"
    }
}
if (Test-Path -LiteralPath (Join-Path $SdkRoot "Tools\NEM.BuildTool\NEMBuildTool.vcxproj")) {
    throw "SDK単体の検証にエンジンソースのプロジェクトを使用できません"
}

function Write-Utf8Json([string]$Path, [object]$Value) {
    $json = $Value | ConvertTo-Json -Depth 32
    [System.IO.File]::WriteAllText($Path, $json, [System.Text.UTF8Encoding]::new($false))
}

function Assert-BuildFailure([string]$Name, [string]$Expected) {
    $path = Join-Path $workRoot ($Name + ".json")
    Write-Utf8Json $path $manifest
    $ErrorActionPreference = "Continue"
    $log = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript -ManifestPath $path 2>&1)
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    $log | Set-Content -LiteralPath (Join-Path $workRoot ($Name + ".log")) -Encoding UTF8
    if ($code -eq 0 -or -not (($log -join "`n").Contains($Expected))) {
        throw "想定した日本語エラーを確認できませんでした: $Name"
    }
    Write-Host "[成功] $Name : $Expected"
}

# 検証成果物は毎回別フォルダーに保存し、既存のゲームや製品を変更しない
$workRoot = Join-Path $engineRoot ("Generated\SdkTests\" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $workRoot | Out-Null
$previousEngineRoot = $env:NEMENGINE_ROOT
try {
    $env:NEMENGINE_ROOT = $SdkRoot
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $engineRoot "Tools\NewGame.ps1") `
        -Name SdkProbe -SdkPath $SdkRoot -Dest $workRoot
    if ($LASTEXITCODE -ne 0) { throw "検証用ゲームの生成に失敗しました" }

    $gameRoot = Join-Path $workRoot "SdkProbe"
    $appRoot = Join-Path $gameRoot "Project\SdkProbe"
    $gameAssets = Join-Path $appRoot "GameAssets"
    [xml]$solution = Get-Content -LiteralPath (Join-Path $gameRoot "Project\SdkProbe.slnx") -Raw
    $nativeProject = @($solution.Solution.SelectNodes("//Project") | Where-Object {
        $_.Path -eq "SdkProbe\SdkProbe.vcxproj"
    }) | Select-Object -First 1
    if ($null -eq $nativeProject -or
        $nativeProject.BuildDependency.Project -ne "SdkProbe\Scripts\GameScripts.csproj") {
        throw "ゲームプロジェクトにC#のビルド依存が設定されていません"
    }
    if (-not [string]::IsNullOrWhiteSpace($GameSourceRoot)) {
        $GameSourceRoot = (Resolve-Path -LiteralPath $GameSourceRoot).Path
        foreach ($directory in @("GameAssets", "ProjectSettings", "Packages")) {
            $source = Join-Path $GameSourceRoot $directory
            if (Test-Path -LiteralPath $source -PathType Container) {
                Copy-Item -LiteralPath $source -Destination $appRoot -Recurse -Force
            }
        }
    }
    # 検証先のGeneratedが除外判定に入らないよう相対パスでメタを同期する
    Push-Location $appRoot
    try {
        & dotnet (Join-Path $SdkRoot "Managed\Tools\NEM.ScriptMetaSync.dll") --root GameAssets --mode EditorSync
        if ($LASTEXITCODE -ne 0) { throw "検証用スクリプトのメタ同期に失敗しました" }
    }
    finally {
        Pop-Location
    }
    $scenePath = Join-Path $gameAssets "SdkProbe.scene.json"
    $sceneGUID = [Guid]::NewGuid().ToString("N")
    Write-Utf8Json $scenePath @{
        SchemaVersion = 3
        Header = @{ name = "SdkProbe"; subScenes = @() }
        Entities = @()
        PrefabInstances = @()
    }
    Write-Utf8Json ($scenePath + ".meta") @{
        schemaVersion = 2
        guid = $sceneGUID
        type = "Scene"
        importer = "SceneImporter"
        importerVersion = 1
        settings = @{}
    }
    if (-not [string]::IsNullOrWhiteSpace($StartupScene)) {
        $sceneMeta = Get-Content -LiteralPath (Join-Path $gameAssets ($StartupScene + ".meta")) -Raw -Encoding UTF8 | ConvertFrom-Json
        $sceneGUID = $sceneMeta.guid
    }

    $files = @(
        foreach ($root in @(
            @{ path = $gameAssets; destination = "GameAssets" },
            @{ path = (Join-Path $appRoot "ProjectSettings"); destination = "ProjectSettings" },
            @{ path = (Join-Path $SdkRoot "Engine\Assets"); destination = "Engine/Assets" }
        )) {
            foreach ($file in Get-ChildItem -LiteralPath $root.path -Recurse -File) {
                $relative = $file.FullName.Substring($root.path.Length).TrimStart('\', '/').Replace('\', '/')
                @{
                    source = $file.FullName
                    destination = $root.destination + "/" + $relative
                    size = $file.Length
                    sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
        }
    )
    $descriptor = Get-Content -LiteralPath (Join-Path $appRoot "SdkProbe.nemproject") -Raw -Encoding UTF8 | ConvertFrom-Json
    $manifest = @{
        schemaVersion = 2
        projectPath = (Join-Path $appRoot "SdkProbe.vcxproj")
        gameRoot = $appRoot
        buildToolProject = ""
        buildToolExecutable = $buildTool
        sourceRuntime = (Join-Path $gameRoot "Generated\Output\Release\SdkProbe")
        runtimeExecutable = "SdkProbe.exe"
        outputRoot = (Join-Path $workRoot "Products")
        productName = "SdkProbe"
        executableName = "SdkProbe.exe"
        projectGuid = $descriptor.projectGuid
        startupScene = $sceneGUID
        startupFullscreen = $false
        packages = @()
        files = $files
        cookHash = "sdk-product-build-regression"
    }

    $manifest.buildToolExecutable = Join-Path $workRoot "missing.exe"
    Assert-BuildFailure "MissingSdkTool" "SDKの製品ビルドツールが見つかりません"
    $manifest.buildToolExecutable = $buildTool
    $manifest.buildToolProject = Join-Path $workRoot "missing.vcxproj"
    Assert-BuildFailure "MissingSourceProject" "製品ビルドツールのプロジェクトが見つかりません"
    $manifest.buildToolProject = ""

    $manifestPath = Join-Path $workRoot "SdkBuild.json"
    Write-Utf8Json $manifestPath $manifest
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript -ManifestPath $manifestPath |
        Tee-Object -FilePath (Join-Path $workRoot "SdkBuild.log")
    if ($LASTEXITCODE -ne 0) { throw "SDK単体の製品ビルドに失敗しました" }
    $sdkLog = Get-Content -LiteralPath (Join-Path $workRoot "SdkBuild.log") -Raw
    if (-not $sdkLog.Contains("SDKに同梱された製品ビルドツールを使用します") -or
        $sdkLog.Contains("製品ビルドツールをビルドしています")) {
        throw "SDKの製品ビルドでソース開発環境の経路が使用されています"
    }

    if ($IncludeSourceBuild) {
        $manifest.buildToolProject = Join-Path $engineRoot "Project\Tools\NEM.BuildTool\NEMBuildTool.vcxproj"
        $manifest.buildToolExecutable = Join-Path $engineRoot "Generated\Output\Release\NEMBuildTool\NEMBuildTool.exe"
        $sourceManifestPath = Join-Path $workRoot "SourceBuild.json"
        Write-Utf8Json $sourceManifestPath $manifest
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $engineRoot "Tools\BuildGame.ps1") `
            -ManifestPath $sourceManifestPath | Tee-Object -FilePath (Join-Path $workRoot "SourceBuild.log")
        if ($LASTEXITCODE -ne 0) { throw "ソース開発環境の製品ビルドに失敗しました" }
        $manifest.buildToolProject = ""
        $manifest.buildToolExecutable = $buildTool
    }

    $productRoot = Join-Path $manifest.outputRoot $manifest.productName
    foreach ($required in @("SdkProbe.exe", "NEMRuntime.dll", "Managed\GameScripts.dll", "Cooked\Shaders\ShaderCookManifest.json")) {
        if (-not (Test-Path -LiteralPath (Join-Path $productRoot $required) -PathType Leaf)) {
            throw "製品の必須ファイルが見つかりません: $required"
        }
    }
    $releaseGameScripts = Join-Path $appRoot "Managed\Release\GameScripts.dll"
    $productGameScripts = Join-Path $productRoot "Managed\GameScripts.dll"
    if ((Get-FileHash -LiteralPath $releaseGameScripts -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $productGameScripts -Algorithm SHA256).Hash) {
        throw "製品のC#ゲームスクリプトDLLが最新のRelease成果物と一致しません"
    }
    $sources = @(Get-ChildItem -LiteralPath $productRoot -Recurse -File | Where-Object {
        $_.Name -match '\.(hlsl|hlsli)(\.meta)?$|\.shadergraph\.json(\.meta)?$' -or
        $_.Name -in @("dxcompiler.dll", "dxil.dll", "NEMBuildTool.exe")
    })
    if ($sources.Count -ne 0) { throw "製品に開発用シェーダーまたはツールが残っています" }

    & $buildTool --verify-cook (Join-Path $productRoot ".nemCookManifest.json") $productRoot
    if ($LASTEXITCODE -ne 0) { throw "製品ファイルのハッシュ検証に失敗しました" }

    $shaderManifest = Get-Content -LiteralPath (Join-Path $productRoot "Cooked\Shaders\ShaderCookManifest.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($required in @(
        @("4e454d41535345548ed3590f7d583b7a", "VS", "main", "vs_6_0"),
        @("4e454d41535345544c49474854000001", "PS", "main", "ps_6_6"),
        @("4e454d41535345544c49474854000002", "PS", "mainShadowed", "ps_6_6"),
        @("4e454d41535345544c49474854000003", "CS", "main", "cs_6_6"),
        @("4e454d41535345544c49474854000004", "PS", "main", "ps_6_0"),
        @("4e454d41535345544c49474854000005", "VS", "main", "vs_6_0"),
        @("4e454d41535345544c49474854000005", "PS", "main", "ps_6_6")
    )) {
        $shader = @($shaderManifest.shaders | Where-Object { $_.asset.guid -eq $required[0] })
        $stage = @($shader | ForEach-Object { $_.stages } | Where-Object {
            $_.stage -eq $required[1] -and $_.entry -eq $required[2] -and $_.profile -eq $required[3]
        })
        if ($stage.Count -ne 1) { throw "固定描画パスのCook済みステージが不正です: $required" }
        $bytecode = Join-Path (Join-Path $productRoot "Cooked\Shaders") $stage[0].bytecode
        if ((Get-Item -LiteralPath $bytecode).Length -eq 0) { throw "Cook済みステージが空です: $required" }
    }
    Write-Host "[成功] 固定描画パスの全7ステージを確認しました"

    $before = (Get-FileHash -LiteralPath (Join-Path $productRoot ".nemCookManifest.json")).Hash
    $manifest.buildToolExecutable = Join-Path $workRoot "missing.exe"
    Assert-BuildFailure "PreserveProductOnFailure" "SDKの製品ビルドツールが見つかりません"
    $manifest.buildToolExecutable = $buildTool
    $gameScriptsProject = Join-Path $appRoot "Scripts\GameScripts.csproj"
    $disabledGameScriptsProject = $gameScriptsProject + ".disabled"
    Move-Item -LiteralPath $gameScriptsProject -Destination $disabledGameScriptsProject
    try {
        Assert-BuildFailure "MissingGameScriptsProject" "C#ゲームスクリプトのプロジェクトが見つかりません"
    }
    finally {
        Move-Item -LiteralPath $disabledGameScriptsProject -Destination $gameScriptsProject
    }
    $originalFiles = $manifest.files
    $manifest.files = @($originalFiles | Where-Object {
        $_.destination -ne "Engine/Assets/Shaders/Builtin/Lighting/deferredLighting.shader.json"
    })
    Assert-BuildFailure "MissingFixedShader" "製品描画に必須のシェーダーが含まれていません"
    $manifest.files = $originalFiles
    $manifest.files[0].sha256 = "0" * 64
    Assert-BuildFailure "PreserveProductOnHashMismatch" "Cook対象ファイルのハッシュが一致しません"
    $after = (Get-FileHash -LiteralPath (Join-Path $productRoot ".nemCookManifest.json")).Hash
    if ($before -ne $after) { throw "失敗時に既存の製品が変更されています" }
    & $buildTool --verify-cook (Join-Path $productRoot ".nemCookManifest.json") $productRoot
    if ($LASTEXITCODE -ne 0) { throw "失敗時に既存の製品ファイルが変更されています" }

    Write-Host "[完了] SDK単体の製品ビルドと日本語エラーを確認しました: $workRoot"
}
finally {
    $env:NEMENGINE_ROOT = $previousEngineRoot
}
