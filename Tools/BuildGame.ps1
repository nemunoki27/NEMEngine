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
. (Join-Path $PSScriptRoot "ProductBuild\PublishProduct.ps1")

$publishDirectory = ""
$stageDirectory = ""
try {
    if (-not (Test-Path -LiteralPath $ManifestPath)) {
        throw "製品ビルドのマニフェストが見つかりません"
    }

    Write-BuildProgress 2 "ビルド設定とツールを確認しています"
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
    $buildID = [Guid]::NewGuid().ToString("N").Substring(0, 12)
    $publishName = ".pub-$buildID"
    $backupName = ".previous-$buildID"
    $stageDirectory = Get-ChildPath $outputRoot ".cook-$buildID"
    $publishDirectory = Get-ChildPath $outputRoot $publishName
    New-Item -ItemType Directory -Path $stageDirectory | Out-Null
    New-Item -ItemType Directory -Path $publishDirectory | Out-Null

    $managedBuildOutput = Join-Path ([System.IO.Path]::GetDirectoryName($projectPath)) 'Managed/Release'
    $managedScripts = Test-ProductManagedScripts $managedBuildOutput $ManifestPath $toolchainRoot
    Write-BuildProgress 40 "実行ファイルとランタイムを配置しています"
    Copy-ProductFiles $manifest $sourceRuntime $runtimeExecutable $stageDirectory $executableName $managedSource $managedScripts
    if ($managedScripts) {
        Write-BuildProgress 70 "ゲームスクリプトと.NETランタイムを配置しています"
        Copy-ProductGameAssemblies $managedBuildOutput (Join-Path $stageDirectory 'Managed')
    } else { Write-BuildProgress 70 "C#を使用しないゲームとして配置しています" }

    # Cookも配置済みの同じ入力だけを読む
    Write-ProductAssetRoots $manifest $stageDirectory $productName $executableName

    Invoke-ProductCook $stageDirectory $buildToolExecutable $ManifestPath

    Write-BuildProgress 96 "製品の設定を書き込んでいます"
    Write-ProductSettings $manifest $stageDirectory $productName $executableName $managedScripts

    Write-BuildProgress 97 "アセットをバイナリ化して検証しています"
    New-PackedProduct $manifest $stageDirectory $publishDirectory $executableName $buildToolExecutable $ManifestPath $toolchainRoot $managedScripts
    $runtimeBytes = (Get-ChildItem -LiteralPath (Join-Path $publishDirectory 'Data/Runtime') -Recurse -File | Measure-Object Length -Sum).Sum
    $packBytes = (Get-Item -LiteralPath (Join-Path $publishDirectory 'Data/Content.nempak')).Length
    $totalBytes = $runtimeBytes + $packBytes + (Get-Item -LiteralPath (Join-Path $publishDirectory $executableName)).Length
    Write-Output ('[NEM_GAME_BUILD_SIZE] 製品合計 {0:N2} MB（アセット {1:N2} MB、ランタイム {2:N2} MB）' -f ($totalBytes/1e6), ($packBytes/1e6), ($runtimeBytes/1e6))
    if (100000000 -lt $totalBytes) {
        Write-Output ('[NEM_GAME_BUILD_SIZE] 100 MBを {0:N2} MB超えています' -f (($totalBytes-100000000)/1e6))
    }
    Write-BuildProgress 99 "完成したゲームを出力先に配置しています"
    Publish-PackedProduct $outputRoot $productName $publishName $backupName $buildToolExecutable ([string]$manifest.projectGuid) $executableName

    Write-Output "製品ビルドが完了しました: $targetDirectory"
    exit 0
}
catch {
    Write-Output ("[NEM_GAME_BUILD_ERROR] " + $_.Exception.Message)
    exit 1
}

finally {
    foreach ($path in $stageDirectory, $publishDirectory) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Continue
        }
    }
}
