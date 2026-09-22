function Invoke-ProductCompilation {
    param([string]$projectPath, [string]$sourceRuntime, [string]$buildToolProject, [string]$buildToolExecutable, [string]$gameScriptsProject)

    $msbuild = Get-MSBuildPath
    Write-Output "ゲームのReleaseビルドを開始します"
    & $msbuild $projectPath /t:Build /p:Configuration=Release /p:Platform=x64 /m /nodeReuse:false /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "ゲームのReleaseビルドに失敗しました"
    }

    # 製品には通常出力の最新DLLだけを使うためC#を強制再ビルドする
    Write-Output "C#ゲームスクリプトのReleaseビルドを確認します"
    & dotnet build $gameScriptsProject -c Release --no-incremental -p:NEMScriptMetadataMode=EditorSync
    if ($LASTEXITCODE -ne 0) {
        throw "C#ゲームスクリプトのReleaseビルドに失敗しました"
    }

    $managedBuildOutput = Join-Path ([System.IO.Path]::GetDirectoryName($projectPath)) "Managed\Release"
    $managedSource = Join-Path $sourceRuntime "Managed"
    $gameScriptsDll = Join-Path $managedBuildOutput "GameScripts.dll"
    if (-not (Test-Path -LiteralPath $gameScriptsDll -PathType Leaf)) {
        throw "C#ゲームスクリプトのDLLが見つかりません: $gameScriptsDll"
    }
    New-Item -ItemType Directory -Path $managedSource -Force | Out-Null
    foreach ($fileName in @("GameScripts.dll", "GameScripts.deps.json", "GameScripts.pdb")) {
        $source = Join-Path $managedBuildOutput $fileName
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $managedSource $fileName) -Force
        }
    }
    $deployedGameScriptsDll = Join-Path $managedSource "GameScripts.dll"
    if ((Get-FileHash -LiteralPath $gameScriptsDll -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $deployedGameScriptsDll -Algorithm SHA256).Hash) {
        throw "ゲーム実行用のC#ゲームスクリプトDLLが最新ではありません"
    }

    if (-not [string]::IsNullOrWhiteSpace($buildToolProject)) {
        Write-Output "製品ビルドツールをビルドしています"
        & $msbuild $buildToolProject /t:Build /p:Configuration=Release /p:Platform=x64 /m /nodeReuse:false /v:minimal
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $buildToolExecutable -PathType Leaf)) {
            throw "製品ビルドツールのビルドに失敗しました"
        }
    }
    else {
        Write-Output "SDKに同梱された製品ビルドツールを使用します"
    }

}
