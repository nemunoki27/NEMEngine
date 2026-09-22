param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding

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
    $buildToolExecutable = [System.IO.Path]::GetFullPath([string]$manifest.buildToolExecutable)
    if (-not [string]::IsNullOrWhiteSpace($buildToolProject)) {
        $buildToolProject = [System.IO.Path]::GetFullPath($buildToolProject)
        if (-not (Test-Path -LiteralPath $buildToolProject -PathType Leaf)) {
            throw "製品ビルドツールのプロジェクトが見つかりません: $buildToolProject"
        }
    }
    elseif (-not (Test-Path -LiteralPath $buildToolExecutable -PathType Leaf)) {
        throw "SDKの製品ビルドツールが見つかりません。SDKを更新してください: $buildToolExecutable"
    }

    Invoke-ProductCompilation $projectPath $sourceRuntime $buildToolProject $buildToolExecutable $gameScriptsProject
    $managedSource = Join-Path $sourceRuntime "Managed"

    $targetDirectory = Get-ChildPath -Root $outputRoot -Relative $productName
    $stageName = "." + $productName + ".building-" + $PID
    $stageDirectory = Get-ChildPath -Root $outputRoot -Relative $stageName
    $backupDirectory = Get-ChildPath -Root $outputRoot -Relative ("." + $productName + ".previous-" + $PID)

    if (Test-Path -LiteralPath $stageDirectory) {
        Remove-Item -LiteralPath $stageDirectory -Recurse -Force
    }
    if (Test-Path -LiteralPath $backupDirectory) {
        Remove-Item -LiteralPath $backupDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null

    Copy-ProductFiles $manifest $sourceRuntime $runtimeExecutable $stageDirectory $executableName $managedSource

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
