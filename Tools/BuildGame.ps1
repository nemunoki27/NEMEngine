param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding

# PowerShell7から起動してもWindows標準の関数を使う
if ($PSVersionTable.PSEdition -eq 'Desktop') {
    Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -Force
}

. (Join-Path $PSScriptRoot "ProductBuild\BuildFileOperations.ps1")
. (Join-Path $PSScriptRoot "ProductBuild\CompileProduct.ps1")
. (Join-Path $PSScriptRoot "ProductBuild\StageProduct.ps1")
. (Join-Path $PSScriptRoot "ProductBuild\CookProduct.ps1")
. (Join-Path $PSScriptRoot "ProductBuild\WriteProductSettings.ps1")

$stageDirectory = ""
try {
    if (-not (Test-Path -LiteralPath $ManifestPath)) {
        throw "製品ビルドのマニフェストが見つかりません"
    }

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $projectPath = [System.IO.Path]::GetFullPath([string]$manifest.projectPath)
    $sourceRuntime = [System.IO.Path]::GetFullPath([string]$manifest.sourceRuntime)
    $outputRoot = [System.IO.Path]::GetFullPath([string]$manifest.outputRoot)
    $productName = [string]$manifest.productName
    $runtimeExecutable = [string]$manifest.runtimeExecutable
    $executableName = [string]$manifest.executableName

    if (-not (Test-Path -LiteralPath $projectPath)) {
        throw "ゲームプロジェクトが見つかりません: $projectPath"
    }
    $gameScriptsProject = Join-Path ([System.IO.Path]::GetDirectoryName($projectPath)) "Scripts\GameScripts.csproj"
    if (-not (Test-Path -LiteralPath $gameScriptsProject -PathType Leaf)) {
        throw "C#ゲームスクリプトのプロジェクトが見つかりません: $gameScriptsProject"
    }

    $buildToolProject = [string]$manifest.buildToolProject
    $prebuiltRuntime = $manifest.PSObject.Properties['prebuiltRuntime'] -and [bool]$manifest.prebuiltRuntime
    $toolchainRoot = if ($manifest.PSObject.Properties['toolchainRoot']) { [string]$manifest.toolchainRoot } else { '' }
    $buildToolExecutable = [System.IO.Path]::GetFullPath([string]$manifest.buildToolExecutable)
    if (-not [string]::IsNullOrWhiteSpace($buildToolProject)) {
        $buildToolProject = [System.IO.Path]::GetFullPath($buildToolProject)
        if (-not (Test-Path -LiteralPath $buildToolProject -PathType Leaf)) {
            throw "製品ビルドツールのプロジェクトが見つかりません: $buildToolProject"
        }
    }
    elseif (-not (Test-Path -LiteralPath $buildToolExecutable -PathType Leaf)) {
        throw "製品ビルドツールが見つかりません。エンジンを修復してください: $buildToolExecutable"
    }

    Invoke-ProductCompilation $projectPath $sourceRuntime $buildToolProject $buildToolExecutable $gameScriptsProject ([bool]$prebuiltRuntime) $toolchainRoot
    $managedSource = Join-Path $sourceRuntime "Managed"

    $targetDirectory = Get-ChildPath -Root $outputRoot -Relative $productName
    $buildID = [Guid]::NewGuid().ToString('N')
    $stageName = "." + $productName + ".building-" + $buildID
    $stageDirectory = Get-ChildPath -Root $outputRoot -Relative $stageName
    $backupDirectory = Get-ChildPath -Root $outputRoot -Relative ("." + $productName + ".previous-" + $buildID)

    if (Test-Path -LiteralPath $stageDirectory) {
        Remove-Item -LiteralPath $stageDirectory -Recurse -Force
    }
    if (Test-Path -LiteralPath $backupDirectory) {
        Remove-Item -LiteralPath $backupDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null

    Copy-ProductFiles $manifest $sourceRuntime $runtimeExecutable $stageDirectory $executableName $managedSource
    # ゲームDLLは共有エンジンへ書き込まず製品へ配置する
    $managedBuildOutput = Join-Path ([System.IO.Path]::GetDirectoryName($projectPath)) 'Managed/Release'
    foreach ($fileName in @('GameScripts.dll', 'GameScripts.deps.json', 'GameScripts.pdb')) {
        $source = Join-Path $managedBuildOutput $fileName
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $stageDirectory "Managed/$fileName") -Force
        }
    }
    if ($toolchainRoot) {
        # 製品にはSDKではなく実行用ランタイムだけを含める
        foreach ($relative in @('host/fxr', 'shared/Microsoft.NETCore.App')) {
            $source = Join-Path $toolchainRoot $relative
            if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw ".NETランタイムがありません: $source" }
            $destination = Get-ChildPath -Root (Join-Path $stageDirectory 'DotNet') -Relative $relative
            New-Item -ItemType Directory -Path $destination -Force | Out-Null
            Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
        }
        foreach ($name in @('LICENSE.txt', 'ThirdPartyNotices.txt')) {
            $source = Join-Path $toolchainRoot $name
            if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $stageDirectory 'DotNet') }
        }
    }

    # Cookも配置済みの同じ入力だけを読む
    Write-ProductAssetRoots $manifest $stageDirectory $productName $executableName

    Invoke-ProductCook $stageDirectory $buildToolExecutable $ManifestPath

    Write-ProductSettings $manifest $stageDirectory $productName $executableName

    if (Test-Path -LiteralPath $targetDirectory) {
        $marker = Join-Path $targetDirectory ".nemBuildManifest.json"
        if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
            throw "既存のフォルダーはNEMEngineの製品ビルドではないため上書きできません: $targetDirectory"
        }
        Move-Item -LiteralPath $targetDirectory -Destination $backupDirectory
    }

    try {
        Move-Item -LiteralPath $stageDirectory -Destination $targetDirectory
        $stageDirectory = ""
    }
    catch {
        if (Test-Path -LiteralPath $backupDirectory) {
            Move-Item -LiteralPath $backupDirectory -Destination $targetDirectory
        }
        throw
    }

    if (Test-Path -LiteralPath $backupDirectory) {
        Remove-Item -LiteralPath $backupDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Output "製品ビルドが完了しました: $targetDirectory"
    exit 0
}
catch {
    if ($stageDirectory -and (Test-Path -LiteralPath $stageDirectory)) {
        Remove-Item -LiteralPath $stageDirectory -Recurse -Force
    }
    Write-Output ("[NEM_GAME_BUILD_ERROR] " + $_.Exception.Message)
    exit 1
}
